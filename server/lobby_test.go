package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"
)

func get(t *testing.T, h http.Handler, method, path string) *httptest.ResponseRecorder {
	t.Helper()
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(method, path, nil))
	return rec
}

func env(m map[string]string) func(string) string {
	return func(k string) string { return m[k] }
}

func TestHealth(t *testing.T) {
	rec := get(t, newLobby(configFromEnv(env(nil))), "GET", "/healthz")
	if rec.Code != 200 || rec.Body.String() != "ok\n" {
		t.Fatalf("got %d %q", rec.Code, rec.Body.String())
	}
}

func TestInfo(t *testing.T) {
	rec := get(t, newLobby(configFromEnv(env(nil))), "GET", "/v1/info")
	var got infoReply
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	want := infoReply{Name: "g64coopdx-lobby", Version: version, Protocol: protocolVersion, Placeholder: true}
	if rec.Code != 200 || got != want {
		t.Fatalf("got %d %+v, want %+v", rec.Code, got, want)
	}
}

func TestSessionsIsAnEmptyList(t *testing.T) {
	rec := get(t, newLobby(configFromEnv(env(nil))), "GET", "/v1/sessions")
	if got := strings.TrimSpace(rec.Body.String()); rec.Code != 200 || got != `{"sessions":[]}` {
		t.Fatalf("got %d %q", rec.Code, got)
	}
}

func TestICEListsTheConfiguredSTUNServers(t *testing.T) {
	cfg := configFromEnv(env(map[string]string{"LOBBY_STUN_URLS": "stun:a.example:3478, stun:b.example:3478"}))
	rec := get(t, newLobby(cfg), "GET", "/v1/ice")
	var got iceReply
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	want := []iceServer{{URLs: []string{"stun:a.example:3478", "stun:b.example:3478"}}}
	if !reflect.DeepEqual(got.ICEServers, want) {
		t.Fatalf("got %+v, want %+v", got.ICEServers, want)
	}
}

func TestOtherMethodsAndPathsAreRefused(t *testing.T) {
	h := newLobby(configFromEnv(env(nil)))
	if rec := get(t, h, "POST", "/v1/sessions"); rec.Code != http.StatusMethodNotAllowed {
		t.Fatalf("POST /v1/sessions: got %d", rec.Code)
	}
	if rec := get(t, h, "GET", "/v1/nothing"); rec.Code != http.StatusNotFound {
		t.Fatalf("GET /v1/nothing: got %d", rec.Code)
	}
}

func TestBadConfigFallsBackToDefaults(t *testing.T) {
	def := configFromEnv(env(nil))
	for _, m := range []map[string]string{
		{"PORT": "0"}, {"PORT": "70000"}, {"PORT": "http"},
		{"LOBBY_STUN_URLS": "turn:a.example, https://b.example"},
	} {
		if got := configFromEnv(env(m)); !reflect.DeepEqual(got, def) {
			t.Fatalf("%v: got %+v, want %+v", m, got, def)
		}
	}
	many := strings.Repeat("stun:a.example:3478,", 20)
	if got := configFromEnv(env(map[string]string{"LOBBY_STUN_URLS": many})); len(got.STUNURLs) != maxSTUNURLs {
		t.Fatalf("got %d STUN URLs, want %d", len(got.STUNURLs), maxSTUNURLs)
	}
}
