// The lobby server of g64coopdx.
//
// It answers health checks and describes itself, and runs the relay (relay.go): a UDP service that carries the
// game's packets between a host and its joiners. With the relay, no player has to forward a port. It lists no
// sessions yet; phase 4 of the roadmap adds that (docs/discovery/roadmap.md).
//
// The server never sees game data: the game's packets hold only inputs, session messages and hashes.
package main

import (
	"log"
	"net"
	"net/http"
	"os"
	"time"
)

func main() {
	cfg := configFromEnv(os.Getenv)
	srv := &http.Server{
		Addr:    ":" + cfg.Port,
		Handler: newLobby(cfg),
		// Every limit below bounds what one client can hold open.
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    8 << 10,
	}
	if cfg.RelayAddr != "" {
		addr, err := net.ResolveUDPAddr("udp", cfg.RelayAddr)
		if err != nil {
			log.Fatalf("relay: %v", err)
		}
		conn, err := net.ListenUDP("udp", addr)
		if err != nil {
			log.Fatalf("relay: %v", err)
		}
		activeRelay = serveRelay(conn)
		activeRelayPort = conn.LocalAddr().(*net.UDPAddr).Port
		log.Printf("relay listens on UDP %s", conn.LocalAddr())
	}
	log.Printf("g64coopdx lobby %s (protocol %d) listens on %s", version, protocolVersion, srv.Addr)
	log.Fatal(srv.ListenAndServe())
}
