package main

import (
	"bytes"
	"net"
	"net/netip"
	"strings"
	"testing"
	"time"
)

type sent struct {
	b  []byte
	to netip.AddrPort
}

// testRelay is a relay whose datagrams go to a list, with a clock the test moves.
func testRelay() (*relay, *[]sent, *time.Time) {
	var out []sent
	now := time.Unix(1000, 0)
	r := newRelay(func(b []byte, to netip.AddrPort) {
		out = append(out, sent{append([]byte(nil), b...), to})
	})
	r.now = func() time.Time { return now }
	return r, &out, &now
}

func addr(s string) netip.AddrPort { return netip.MustParseAddrPort(s) }

func msg(t byte, body ...byte) []byte { return append(header(t), body...) }

func last(t *testing.T, out *[]sent) sent {
	t.Helper()
	if len(*out) == 0 {
		t.Fatal("nothing was sent")
	}
	s := (*out)[len(*out)-1]
	*out = nil
	return s
}

// openRoom opens a room for host and returns its code.
func openRoom(t *testing.T, r *relay, out *[]sent, host netip.AddrPort) string {
	t.Helper()
	r.handle(msg(msgOpen), host)
	s := last(t, out)
	if s.to != host || len(s.b) != relayHeader+relayCodeLen || s.b[4] != msgRoom {
		t.Fatalf("open: got %v to %v", s.b, s.to)
	}
	return string(s.b[relayHeader:])
}

func joinRoom(t *testing.T, r *relay, out *[]sent, code string, from netip.AddrPort) byte {
	t.Helper()
	r.handle(msg(msgJoin, []byte(code)...), from)
	s := last(t, out)
	if s.to != from || len(s.b) != relayHeader+1 || s.b[4] != msgJoined {
		t.Fatalf("join: got %v", s.b)
	}
	return s.b[relayHeader]
}

func TestRelayForwardsBothWays(t *testing.T) {
	r, out, _ := testRelay()
	host, a, b := addr("1.1.1.1:5000"), addr("2.2.2.2:6000"), addr("3.3.3.3:7000")
	code := openRoom(t, r, out, host)
	if again := openRoom(t, r, out, host); again != code {
		t.Fatalf("a host's second open gave another room: %s and %s", code, again)
	}
	pa, pb := joinRoom(t, r, out, code, a), joinRoom(t, r, out, code, b)
	if pa != 1 || pb != 2 {
		t.Fatalf("peer numbers %d and %d", pa, pb)
	}
	r.handle(msg(msgToHost, 'h', 'i'), b)
	s := last(t, out)
	if s.to != host || !bytes.Equal(s.b, msg(msgFromPeer, 2, 'h', 'i')) {
		t.Fatalf("to host: got %v to %v", s.b, s.to)
	}
	r.handle(msg(msgToPeer, 1, 'y', 'o'), host)
	s = last(t, out)
	if s.to != a || !bytes.Equal(s.b, msg(msgFromHost, 'y', 'o')) {
		t.Fatalf("to peer: got %v to %v", s.b, s.to)
	}
}

func TestRelayRefusesAndIgnores(t *testing.T) {
	r, out, _ := testRelay()
	host, stranger := addr("1.1.1.1:5000"), addr("9.9.9.9:9000")
	code := openRoom(t, r, out, host)
	r.handle(msg(msgJoin, []byte("ZZZZZZ")...), stranger)
	if s := last(t, out); !bytes.Equal(s.b, msg(msgRefused, refuseNoRoom)) {
		t.Fatalf("an unknown room: got %v", s.b)
	}
	// A stranger's packets go nowhere; so do packets to an empty slot, too long, or without the magic.
	r.handle(msg(msgToHost, 1, 2, 3), stranger)
	r.handle(msg(msgToPeer, 3, 'x'), host)
	r.handle(msg(msgToPeer, 0, 'x'), host)
	r.handle(msg(msgToPeer, 200, 'x'), host)
	r.handle([]byte("XXXX\x03abc"), stranger)
	r.handle([]byte("G64"), stranger)
	r.handle(append(msg(msgToPeer, 1), make([]byte, relayMaxGame+1)...), host)
	if len(*out) != 0 {
		t.Fatalf("sent %d datagrams for bad input", len(*out))
	}
	// A full room.
	for i := 0; i < relayMaxPeers; i++ {
		joinRoom(t, r, out, code, netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 0, 0, byte(i)}), 1000))
	}
	r.handle(msg(msgJoin, []byte(code)...), stranger)
	if s := last(t, out); !bytes.Equal(s.b, msg(msgRefused, refuseFull)) {
		t.Fatalf("a full room: got %v", s.b)
	}
}

func TestRelayLimitsRoomsPerAddress(t *testing.T) {
	r, out, _ := testRelay()
	for port := 1; port <= relayRoomsPerIP+2; port++ {
		r.handle(msg(msgOpen), netip.AddrPortFrom(netip.MustParseAddr("4.4.4.4"), uint16(port)))
	}
	if rooms, _ := r.counts(); rooms != relayRoomsPerIP {
		t.Fatalf("%d rooms for one address", rooms)
	}
	_ = out
}

func TestRelayExpires(t *testing.T) {
	r, out, now := testRelay()
	host, a, b := addr("1.1.1.1:5000"), addr("2.2.2.2:6000"), addr("3.3.3.3:7000")
	code := openRoom(t, r, out, host)
	joinRoom(t, r, out, code, a)
	joinRoom(t, r, out, code, b)
	// a keeps talking, b goes quiet, the host keeps its room.
	*now = now.Add(relayTimeout / 2)
	r.handle(msg(msgToHost, 1), a)
	r.handle(msg(msgOpen), host)
	*now = now.Add(relayTimeout/2 + time.Second)
	r.expire()
	if rooms, players := r.counts(); rooms != 1 || players != 2 {
		t.Fatalf("after b went quiet: %d rooms, %d players", rooms, players)
	}
	*now = now.Add(2 * relayTimeout)
	r.expire()
	if rooms, players := r.counts(); rooms != 0 || players != 0 {
		t.Fatalf("after the host went quiet: %d rooms, %d players", rooms, players)
	}
	*out = nil
	r.handle(msg(msgToHost, 1), a)
	if len(*out) != 0 {
		t.Fatal("a closed room still forwards")
	}
}

// TestRelayOverUDP runs the relay on a real socket on this machine.
func TestRelayOverUDP(t *testing.T) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	serveRelay(conn)
	relayAddr := conn.LocalAddr().(*net.UDPAddr)
	dial := func() *net.UDPConn {
		c, err := net.DialUDP("udp", nil, relayAddr)
		if err != nil {
			t.Fatal(err)
		}
		_ = c.SetReadDeadline(time.Now().Add(2 * time.Second))
		return c
	}
	read := func(c *net.UDPConn) []byte {
		b := make([]byte, 2000)
		n, err := c.Read(b)
		if err != nil {
			t.Fatal(err)
		}
		return b[:n]
	}
	host, peer := dial(), dial()
	defer host.Close()
	defer peer.Close()
	_, _ = host.Write(msg(msgOpen))
	room := read(host)
	_, _ = peer.Write(msg(msgJoin, room[relayHeader:]...))
	if j := read(peer); !bytes.Equal(j, msg(msgJoined, 1)) {
		t.Fatalf("joined: %v", j)
	}
	_, _ = peer.Write(msg(msgToHost, 'p', 'i', 'n', 'g'))
	if got := read(host); !bytes.Equal(got, msg(msgFromPeer, 1, 'p', 'i', 'n', 'g')) {
		t.Fatalf("host got %v", got)
	}
	_, _ = host.Write(msg(msgToPeer, 1, 'p', 'o', 'n', 'g'))
	if got := read(peer); !bytes.Equal(got, msg(msgFromHost, 'p', 'o', 'n', 'g')) {
		t.Fatalf("peer got %v", got)
	}
}

func TestRelayConfig(t *testing.T) {
	if got := configFromEnv(env(nil)).RelayAddr; got != ":27065" {
		t.Fatalf("default relay address %q", got)
	}
	if got := configFromEnv(env(map[string]string{"LOBBY_RELAY_PORT": "0"})).RelayAddr; got != "" {
		t.Fatalf("port 0 should turn the relay off, got %q", got)
	}
	if got := configFromEnv(env(map[string]string{"FLY_APP_NAME": "x"})).RelayAddr; got != "fly-global-services:27065" {
		t.Fatalf("on Fly.io: %q", got)
	}
}

// FuzzRelayHandle feeds the relay arbitrary datagrams from a few addresses. It must not panic, must send only to
// addresses it has heard from, and must keep its maps consistent.
func FuzzRelayHandle(f *testing.F) {
	f.Add([]byte("G64R\x01"), byte(0))
	f.Add([]byte("G64R\x02ABCDEF"), byte(1))
	f.Add([]byte("G64R\x03hello"), byte(2))
	f.Add([]byte("G64R\x04\x01hi"), byte(0))
	r, out, now := testRelay()
	addrs := []netip.AddrPort{addr("1.1.1.1:1"), addr("2.2.2.2:2"), addr("3.3.3.3:3"), addr("1.1.1.1:4")}
	f.Fuzz(func(t *testing.T, d []byte, who byte) {
		from := addrs[int(who)%len(addrs)]
		// A room code that exists, now and then, so joins succeed.
		if len(d) == relayHeader+relayCodeLen && who&0x80 != 0 {
			for code := range r.rooms {
				copy(d[relayHeader:], code)
				break
			}
		}
		r.handle(d, from)
		*now = now.Add(time.Second)
		r.expire()
		for _, s := range *out {
			known := false
			for _, a := range addrs {
				known = known || a == s.to
			}
			if !known {
				t.Fatalf("sent to %v", s.to)
			}
		}
		*out = nil
		for h, room := range r.byHost {
			if r.rooms[room.code] != room || room.host != h {
				t.Fatal("byHost and rooms disagree")
			}
		}
	})
}

func TestInfoDescribesTheRelay(t *testing.T) {
	r, out, _ := testRelay()
	activeRelay, activeRelayPort = r, 27065
	defer func() { activeRelay, activeRelayPort = nil, 0 }()
	openRoom(t, r, out, addr("1.1.1.1:1"))
	rec := get(t, newLobby(configFromEnv(env(nil))), "GET", "/v1/info")
	if body := rec.Body.String(); !strings.Contains(body, `"relay":{"port":27065,"rooms":1,"players":1}`) {
		t.Fatalf("info: %s", body)
	}
}

// listRequest is a list request for a build, padded to the length the relay asks for.
func listRequest(build uint32) []byte {
	d := msg(msgList, byte(build), byte(build>>8), byte(build>>16), byte(build>>24))
	return append(d, make([]byte, relayListMin-len(d))...)
}

func announce(build uint32, wanted, joined, flags, open byte, title string) []byte {
	return msg(msgAnnounce, append([]byte{byte(build), byte(build >> 8), byte(build >> 16), byte(build >> 24), wanted,
		joined, flags, open}, title...)...)
}

func TestRelayList(t *testing.T) {
	r, out, now := testRelay()
	host, other, asker := addr("10.0.0.1:1000"), addr("10.0.0.2:1000"), addr("10.0.0.9:9")
	code := openRoom(t, r, out, host)
	openRoom(t, r, out, other)
	// A room is not on the list until its host announces it.
	r.handle(listRequest(7), asker)
	if s := last(t, out); s.to != asker || s.b[4] != msgRooms || s.b[relayHeader] != 0 {
		t.Fatalf("an empty list: got %v", s.b)
	}
	r.handle(announce(7, 4, 1, 2, 1, "Fraser's \x01game"), host)
	r.handle(announce(8, 2, 1, 0, 1, "another build"), other)
	if len(*out) != 0 {
		t.Fatalf("an announce is not answered: %v", *out)
	}
	r.handle(listRequest(7), asker)
	s := last(t, out)
	title := "Fraser's ?game"
	want := append(append(msg(msgRooms, 1), code...), 4, 1, 2, byte(len(title)))
	want = append(want, title...)
	if !bytes.Equal(s.b, want) {
		t.Fatalf("the list for build 7: got %q, want %q", s.b, want)
	}
	if len(s.b) > relayListMin {
		t.Fatalf("the answer is longer than the question")
	}
	// A short request gets no answer: the relay must not send more than it was sent.
	r.handle(listRequest(7)[:relayListMin-1], asker)
	if len(*out) != 0 {
		t.Fatalf("a short list request was answered")
	}
	// The web route's list has every build's rooms.
	if l := r.listed(); len(l) != 2 {
		t.Fatalf("listed: got %v", l)
	}
	// The host takes the room off the list.
	r.handle(announce(7, 4, 2, 2, 0, "Fraser's game"), host)
	r.handle(listRequest(7), asker)
	if s := last(t, out); s.b[relayHeader] != 0 {
		t.Fatalf("a room taken off the list is still on it")
	}
	// A room whose host stops announcing leaves the list by itself.
	r.handle(announce(7, 4, 2, 2, 1, "Fraser's game"), host)
	*now = now.Add(relayListedFor + time.Second)
	r.handle(listRequest(7), asker)
	if s := last(t, out); s.b[relayHeader] != 0 {
		t.Fatalf("a room that is no longer announced is still on the list")
	}
	// Only a room's host can announce it, and a title cannot be longer than the limit.
	r.handle(announce(7, 4, 2, 2, 1, "x"), asker)
	r.handle(announce(7, 4, 2, 2, 1, strings.Repeat("x", relayTitleMax+1)), host)
	r.handle(listRequest(7), asker)
	if s := last(t, out); s.b[relayHeader] != 0 {
		t.Fatalf("a bad announce listed a room")
	}
}

func TestRelayListIsBounded(t *testing.T) {
	r, out, _ := testRelay()
	for i := 0; i < relayListMax+5; i++ {
		host := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 1, byte(i), 1}), 1000)
		openRoom(t, r, out, host)
		r.handle(announce(7, 16, 1, 0, 1, strings.Repeat("t", relayTitleMax)), host)
	}
	r.handle(listRequest(7), addr("10.0.0.9:9"))
	s := last(t, out)
	if s.b[relayHeader] != relayListMax || len(s.b) > relayListMin {
		t.Fatalf("the list: %d rooms in %d bytes, at most %d in %d", s.b[relayHeader], len(s.b), relayListMax, relayListMin)
	}
}

func TestRelayBan(t *testing.T) {
	r, out, now := testRelay()
	host, bad, good := addr("10.0.0.1:1000"), addr("10.0.0.2:2000"), addr("10.0.0.3:3000")
	code := openRoom(t, r, out, host)
	peer := joinRoom(t, r, out, code, bad)
	joinRoom(t, r, out, code, good)
	// Only the host can ban.
	r.handle(msg(msgBan, peer), good)
	if len(*out) != 0 {
		t.Fatalf("a joiner's ban was answered")
	}
	r.handle(msg(msgBan, peer), host)
	if s := last(t, out); s.to != bad || s.b[4] != msgRefused || s.b[relayHeader] != refuseBanned {
		t.Fatalf("the banned peer is told: got %v to %v", s.b, s.to)
	}
	// The banned address cannot join again, from any port. Another address still can.
	for _, from := range []netip.AddrPort{bad, addr("10.0.0.2:2001")} {
		r.handle(msg(msgJoin, []byte(code)...), from)
		if s := last(t, out); s.b[4] != msgRefused || s.b[relayHeader] != refuseBanned {
			t.Fatalf("a banned address joined: %v", s.b)
		}
	}
	// A second ban of the same place does nothing.
	r.handle(msg(msgBan, peer), host)
	if len(*out) != 0 {
		t.Fatalf("a second ban was answered")
	}
	// The banned peer's packets no longer reach the host. The host's last messages still reach the peer.
	r.handle(msg(msgToHost, 1, 2, 3), bad)
	if len(*out) != 0 {
		t.Fatalf("a banned peer's packet was passed on")
	}
	r.handle(msg(msgToPeer, peer, 9), host)
	if s := last(t, out); s.to != bad || s.b[4] != msgFromHost {
		t.Fatalf("the host's message to the banned peer: got %v to %v", s.b, s.to)
	}
	// The place stays taken for a while, then a newcomer gets it. The ban stays.
	if p := joinRoom(t, r, out, code, addr("10.0.0.4:4000")); p == peer {
		t.Fatalf("a newcomer got the banned peer's place at once")
	}
	*now = now.Add(relayBanHold + time.Second)
	r.handle(msg(msgToPeer, 2, 9), host)
	*out = nil
	r.expire()
	if p := joinRoom(t, r, out, code, addr("10.0.0.5:5000")); p != peer {
		t.Fatalf("the banned peer's place is free after the hold: got %d, want %d", p, peer)
	}
	r.handle(msg(msgJoin, []byte(code)...), bad)
	if s := last(t, out); s.b[4] != msgRefused || s.b[relayHeader] != refuseBanned {
		t.Fatalf("the ban ended with the hold: %v", s.b)
	}
	// An IPv6 ban covers the /64.
	six := addr("[2001:db8:1:2::10]:6000")
	p6 := joinRoom(t, r, out, code, six)
	r.handle(msg(msgBan, p6), host)
	*out = nil
	r.handle(msg(msgJoin, []byte(code)...), addr("[2001:db8:1:2:aaaa::1]:6001"))
	if s := last(t, out); s.b[4] != msgRefused || s.b[relayHeader] != refuseBanned {
		t.Fatalf("another address of the banned /64 joined: %v", s.b)
	}
	r.handle(msg(msgJoin, []byte(code)...), addr("[2001:db8:1:3::10]:6000"))
	if s := last(t, out); s.b[4] != msgJoined {
		t.Fatalf("another /64 was refused: %v", s.b)
	}
}
