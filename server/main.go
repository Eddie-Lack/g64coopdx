// The lobby server of g64coopdx.
//
// This is a placeholder. It answers health checks and describes itself. It lists no sessions and passes no messages
// yet; phase 4 of the roadmap adds those (docs/discovery/roadmap.md).
//
// The server never sees game data. Its finished form holds session descriptions and passes connection details
// between a host and a joiner.
package main

import (
	"log"
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
	log.Printf("g64coopdx lobby %s (protocol %d) listens on %s", version, protocolVersion, srv.Addr)
	log.Fatal(srv.ListenAndServe())
}
