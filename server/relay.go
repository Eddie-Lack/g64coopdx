package main

// The relay: it carries the game's UDP packets between a host and its joiners, so no player has to forward a port.
// Every player sends to the relay; the relay passes each packet on inside its room. It never reads the game's
// packets, which hold only inputs, session messages and hashes (docs/discovery/netcode.md).
//
// Datagrams, all numbers little-endian, each starting with the magic "G64R" and a type byte:
//
//	open      0x01                         host -> relay: open a room, or keep it open
//	room      0x81, code (6 ASCII bytes)   relay -> host: the room's code
//	join      0x02, code                   joiner -> relay: join a room, or stay in it
//	joined    0x82, peer (u8)              relay -> joiner: the joiner's number in the room (1 to 15)
//	refused   0x83, reason (u8)            relay -> joiner: no such room (1), or the room is full (2)
//	to host   0x03, payload                joiner -> relay
//	from peer 0x84, peer (u8), payload     relay -> host
//	to peer   0x04, peer (u8), payload     host -> relay
//	from host 0x85, payload                relay -> joiner
//	announce  0x05, build (u32), wanted (u8), joined (u8), flags (u8), open (u8), title (up to 32 bytes)
//	                                       host -> relay: list the room for players of this build (open 1), or
//	                                       take it off the list (open 0). The host repeats it while it waits.
//	list      0x06, build (u32), padding   anyone -> relay: which listed rooms does this build have? The datagram
//	                                       must be relayListMin bytes long: the answer is never longer than the
//	                                       question, so the relay cannot be used to flood a forged address.
//	rooms     0x86, count (u8), then for each room: code (6), wanted (u8), joined (u8), flags (u8), title length
//	                                       (u8), title        relay -> the asker: up to relayListMax rooms
//	ban       0x07, peer (u8)              host -> relay: put the peer out of the room and refuse their address
//	                                       when it joins this room again
//
// A refused join has the reason 3 when the room's host has banned the joiner's address. The relay is the only
// place that sees a joiner's address, so it is the only place that can keep a ban.
//
// A host's or joiner's room is found by the address it sends from. A room that has heard nothing from its host for
// relayTimeout closes; so does a peer that has gone quiet.

import (
	"crypto/rand"
	"log"
	"net"
	"net/netip"
	"sync"
	"time"
)

const (
	relayMagic     = "G64R"
	relayHeader    = 5
	relayMaxGame   = 1200 // the game's own limit for a datagram (src/net/udp.h)
	relayMaxPeers  = 15   // joiners in one room; the host makes 16 players
	relayMaxRooms  = 2000
	relayTimeout   = 30 * time.Second
	relayCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
	relayCodeLen   = 6
	// relayRoomsPerIP bounds the rooms one address may hold open at once.
	relayRoomsPerIP = 4
	// relayTitleMax bounds a listed room's title, relayListMax the rooms in one answer, relayListMin the length
	// of a list request, and relayBansMax the addresses one room may ban.
	relayTitleMax = 32
	relayListMax  = 12
	relayListMin  = relayHeader + 1 + relayListMax*(relayCodeLen+4+relayTitleMax)
	relayBansMax  = 32
	// relayListedFor is how long a room stays on the list after its host last announced it.
	relayListedFor = 10 * time.Second
)

const (
	msgOpen     = 0x01
	msgJoin     = 0x02
	msgToHost   = 0x03
	msgToPeer   = 0x04
	msgAnnounce = 0x05
	msgList     = 0x06
	msgBan      = 0x07
	msgRooms    = 0x86
	msgRoom     = 0x81
	msgJoined   = 0x82
	msgRefused  = 0x83
	msgFromPeer = 0x84
	msgFromHost = 0x85
)

const (
	refuseNoRoom = 1
	refuseFull   = 2
	refuseBanned = 3
)

type relayPeer struct {
	addr  netip.AddrPort
	heard time.Time
}

type relayRoom struct {
	code  string
	host  netip.AddrPort
	heard time.Time
	peers [relayMaxPeers + 1]*relayPeer // index 1 to relayMaxPeers
	// The room's place on the list: listed until this time, for players of this build, with what the host said
	// about it. The title is printable ASCII.
	listedUntil           time.Time
	build                 uint32
	wanted, joined, flags byte
	title                 string
	// The addresses the host has banned from the room.
	banned []netip.Addr
}

// listedRoom is one room on the list, for the web route.
type listedRoom struct {
	Code   string `json:"code"`
	Title  string `json:"title"`
	Wanted int    `json:"wanted"`
	Joined int    `json:"joined"`
}

// cleanTitle keeps the printable ASCII of a title, up to relayTitleMax bytes.
func cleanTitle(b []byte) string {
	if len(b) > relayTitleMax {
		b = b[:relayTitleMax]
	}
	out := make([]byte, 0, len(b))
	for _, c := range b {
		if c < 32 || c > 126 {
			c = '?'
		}
		out = append(out, c)
	}
	return string(out)
}

func (room *relayRoom) isBanned(a netip.Addr) bool {
	for _, b := range room.banned {
		if b == a {
			return true
		}
	}
	return false
}

// relay holds the rooms. Its methods are safe for one reader goroutine and the janitor.
type relay struct {
	mu     sync.Mutex
	rooms  map[string]*relayRoom
	byHost map[netip.AddrPort]*relayRoom
	byPeer map[netip.AddrPort]*relayRoom
	now    func() time.Time
	// send writes one datagram; a field so the tests can watch it.
	send func(b []byte, to netip.AddrPort)
}

func newRelay(send func(b []byte, to netip.AddrPort)) *relay {
	return &relay{
		rooms:  map[string]*relayRoom{},
		byHost: map[netip.AddrPort]*relayRoom{},
		byPeer: map[netip.AddrPort]*relayRoom{},
		now:    time.Now,
		send:   send,
	}
}

func newCode() string {
	b := make([]byte, relayCodeLen)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	for i := range b {
		b[i] = relayCodeChars[int(b[i])%len(relayCodeChars)]
	}
	return string(b)
}

func header(t byte) []byte {
	return append([]byte(relayMagic), t)
}

// handle deals with one datagram from addr.
func (r *relay) handle(d []byte, from netip.AddrPort) {
	if len(d) < relayHeader || string(d[:4]) != relayMagic {
		return
	}
	t, body := d[4], d[relayHeader:]
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.now()
	switch t {
	case msgOpen:
		room := r.byHost[from]
		if room == nil {
			if len(r.rooms) >= relayMaxRooms || r.roomsOf(from.Addr()) >= relayRoomsPerIP || r.byPeer[from] != nil {
				return
			}
			code := newCode()
			for r.rooms[code] != nil {
				code = newCode()
			}
			room = &relayRoom{code: code, host: from}
			r.rooms[code] = room
			r.byHost[from] = room
		}
		room.heard = now
		r.send(append(header(msgRoom), room.code...), from)
	case msgJoin:
		if len(body) != relayCodeLen {
			return
		}
		if room := r.byPeer[from]; room != nil {
			for i, p := range room.peers {
				if p != nil && p.addr == from {
					p.heard = now
					r.send(append(header(msgJoined), byte(i)), from)
				}
			}
			return
		}
		room := r.rooms[string(body)]
		if room == nil || r.byHost[from] != nil {
			r.send(append(header(msgRefused), refuseNoRoom), from)
			return
		}
		if room.isBanned(from.Addr()) {
			r.send(append(header(msgRefused), refuseBanned), from)
			return
		}
		for i := 1; i <= relayMaxPeers; i++ {
			if room.peers[i] == nil {
				room.peers[i] = &relayPeer{addr: from, heard: now}
				r.byPeer[from] = room
				r.send(append(header(msgJoined), byte(i)), from)
				return
			}
		}
		r.send(append(header(msgRefused), refuseFull), from)
	case msgToHost:
		room := r.byPeer[from]
		if room == nil || len(body) > relayMaxGame {
			return
		}
		for i, p := range room.peers {
			if p != nil && p.addr == from {
				p.heard = now
				r.send(append(append(header(msgFromPeer), byte(i)), body...), room.host)
				return
			}
		}
	case msgToPeer:
		room := r.byHost[from]
		if room == nil || len(body) < 1 || len(body)-1 > relayMaxGame {
			return
		}
		room.heard = now
		i := int(body[0])
		if i < 1 || i > relayMaxPeers || room.peers[i] == nil {
			return
		}
		r.send(append(header(msgFromHost), body[1:]...), room.peers[i].addr)
	case msgAnnounce:
		room := r.byHost[from]
		if room == nil || len(body) < 8 || len(body) > 8+relayTitleMax {
			return
		}
		room.heard = now
		room.build = uint32(body[0]) | uint32(body[1])<<8 | uint32(body[2])<<16 | uint32(body[3])<<24
		room.wanted, room.joined, room.flags = body[4], body[5], body[6]
		room.title = cleanTitle(body[8:])
		if body[7] == 1 {
			room.listedUntil = now.Add(relayListedFor)
		} else {
			room.listedUntil = time.Time{}
		}
	case msgList:
		if len(d) < relayListMin || len(body) < 4 {
			return
		}
		build := uint32(body[0]) | uint32(body[1])<<8 | uint32(body[2])<<16 | uint32(body[3])<<24
		reply := append(header(msgRooms), 0)
		n := 0
		for _, room := range r.rooms {
			if n == relayListMax {
				break
			}
			if room.build != build || !now.Before(room.listedUntil) {
				continue
			}
			reply = append(reply, room.code...)
			reply = append(reply, room.wanted, room.joined, room.flags, byte(len(room.title)))
			reply = append(reply, room.title...)
			n++
		}
		reply[relayHeader] = byte(n)
		r.send(reply, from)
	case msgBan:
		room := r.byHost[from]
		if room == nil || len(body) != 1 {
			return
		}
		room.heard = now
		i := int(body[0])
		if i < 1 || i > relayMaxPeers || room.peers[i] == nil {
			return
		}
		p := room.peers[i]
		if !room.isBanned(p.addr.Addr()) && len(room.banned) < relayBansMax {
			room.banned = append(room.banned, p.addr.Addr())
		}
		delete(r.byPeer, p.addr)
		room.peers[i] = nil
		r.send(append(header(msgRefused), refuseBanned), p.addr)
	}
}

// listed gives the rooms on the list, whatever their build, for the web route.
func (r *relay) listed() []listedRoom {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.now()
	out := []listedRoom{}
	for _, room := range r.rooms {
		if now.Before(room.listedUntil) && len(out) < 200 {
			out = append(out, listedRoom{Code: room.code, Title: room.title, Wanted: int(room.wanted), Joined: int(room.joined)})
		}
	}
	return out
}

func (r *relay) roomsOf(a netip.Addr) int {
	n := 0
	for h := range r.byHost {
		if h.Addr() == a {
			n++
		}
	}
	return n
}

// expire closes quiet rooms and drops quiet peers.
func (r *relay) expire() {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.now()
	for code, room := range r.rooms {
		if now.Sub(room.heard) > relayTimeout {
			for _, p := range room.peers {
				if p != nil {
					delete(r.byPeer, p.addr)
				}
			}
			delete(r.byHost, room.host)
			delete(r.rooms, code)
			continue
		}
		for i, p := range room.peers {
			if p != nil && now.Sub(p.heard) > relayTimeout {
				delete(r.byPeer, p.addr)
				room.peers[i] = nil
			}
		}
	}
}

// rooms counts the open rooms and their players, for /v1/info.
func (r *relay) counts() (rooms, players int) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for _, room := range r.rooms {
		players++
		for _, p := range room.peers {
			if p != nil {
				players++
			}
		}
	}
	return len(r.rooms), players
}

// serveRelay reads datagrams from conn until it fails. It runs the janitor too.
func serveRelay(conn *net.UDPConn) *relay {
	r := newRelay(func(b []byte, to netip.AddrPort) {
		_, _ = conn.WriteToUDPAddrPort(b, to)
	})
	go func() {
		for range time.Tick(5 * time.Second) {
			r.expire()
		}
	}()
	go func() {
		buf := make([]byte, relayHeader+1+relayMaxGame+1)
		for {
			n, from, err := conn.ReadFromUDPAddrPort(buf)
			if err != nil {
				log.Printf("relay: %v", err)
				return
			}
			// An IPv4 peer on a dual-stack socket arrives as ::ffff:a.b.c.d; keep one form per peer.
			from = netip.AddrPortFrom(from.Addr().Unmap(), from.Port())
			r.handle(buf[:n], from)
		}
	}()
	return r
}
