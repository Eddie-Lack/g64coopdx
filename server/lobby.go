package main

import (
	"encoding/json"
	"net/http"
	"strconv"
	"strings"
)

// version names the build. The build sets it with -ldflags "-X main.version=...".
var version = "dev"

// protocolVersion is the version of the lobby's messages. A client and a lobby must agree on it. It stays 0 while
// the server is a placeholder.
const protocolVersion = 0

// maxSTUNURLs bounds the list that the configuration may hold.
const maxSTUNURLs = 8

// config holds the settings read from the environment.
type config struct {
	// Port is the TCP port to listen on.
	Port string
	// STUNURLs lists the STUN servers that clients are told to use. A STUN server tells a machine its public
	// address.
	STUNURLs []string
	// RelayAddr is the UDP address the relay listens on; empty turns the relay off.
	RelayAddr string
}

// configFromEnv reads the settings. A missing or bad value falls back to its default.
func configFromEnv(getenv func(string) string) config {
	cfg := config{Port: "8080", STUNURLs: []string{"stun:stun.cloudflare.com:3478"}, RelayAddr: ":27065"}
	if p, err := strconv.Atoi(getenv("LOBBY_RELAY_PORT")); err == nil && p >= 0 && p < 65536 {
		cfg.RelayAddr = ":" + strconv.Itoa(p)
		if p == 0 {
			cfg.RelayAddr = ""
		}
	}
	// Fly.io delivers UDP only to sockets bound to this name (docs.fly.io/networking/udp-and-tcp).
	if getenv("FLY_APP_NAME") != "" && cfg.RelayAddr != "" {
		cfg.RelayAddr = "fly-global-services" + cfg.RelayAddr
	}
	if p, err := strconv.Atoi(getenv("PORT")); err == nil && p > 0 && p < 65536 {
		cfg.Port = strconv.Itoa(p)
	}
	if v := getenv("LOBBY_STUN_URLS"); v != "" {
		var urls []string
		for _, u := range strings.Split(v, ",") {
			u = strings.TrimSpace(u)
			if strings.HasPrefix(u, "stun:") && len(u) <= 255 && len(urls) < maxSTUNURLs {
				urls = append(urls, u)
			}
		}
		if len(urls) > 0 {
			cfg.STUNURLs = urls
		}
	}
	return cfg
}

type infoReply struct {
	Name     string `json:"name"`
	Version  string `json:"version"`
	Protocol int    `json:"protocol"`
	// Placeholder is true while the server is unfinished. It lists sessions, but /v1/ice gives STUN servers only
	// (server/README.md).
	Placeholder bool `json:"placeholder"`
	// Relay describes the relay when it runs.
	Relay *relayInfo `json:"relay,omitempty"`
}

type relayInfo struct {
	Port    int `json:"port"`
	Rooms   int `json:"rooms"`
	Players int `json:"players"`
}

// activeRelay is the running relay, which /v1/info describes; nil when it is off.
var (
	activeRelay     *relay
	activeRelayPort int
)

type sessionsReply struct {
	// Sessions lists the rooms that hosts have put on the relay's list: the games that wait for players.
	Sessions []listedRoom `json:"sessions"`
}

type iceServer struct {
	URLs []string `json:"urls"`
}

type iceReply struct {
	// ICEServers lists the servers a client uses to find a path to another player. The placeholder lists STUN
	// servers only. Relay credentials come with phase 4.
	ICEServers []iceServer `json:"iceServers"`
}

// newLobby returns the handler for every route.
func newLobby(cfg config) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("GET /v1/info", func(w http.ResponseWriter, r *http.Request) {
		reply := infoReply{Name: "g64coopdx-lobby", Version: version, Protocol: protocolVersion, Placeholder: true}
		if activeRelay != nil {
			rooms, players := activeRelay.counts()
			reply.Relay = &relayInfo{Port: activeRelayPort, Rooms: rooms, Players: players}
		}
		writeJSON(w, reply)
	})
	mux.HandleFunc("GET /v1/sessions", func(w http.ResponseWriter, r *http.Request) {
		reply := sessionsReply{Sessions: []listedRoom{}}
		if activeRelay != nil {
			reply.Sessions = activeRelay.listed()
		}
		writeJSON(w, reply)
	})
	mux.HandleFunc("GET /v1/ice", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, iceReply{ICEServers: []iceServer{{URLs: cfg.STUNURLs}}})
	})
	return mux
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	json.NewEncoder(w).Encode(v)
}
