/* The session over the lobby's relay (server/relay.go), on this machine: a host opens a room, two joiners join it by
 * its code, and all three play the toy game through the rollback core. No socket talks to another directly.
 *
 * Usage: relay_test RELAY-ADDRESS (for example 127.0.0.1:27065, with build/lobby running). Without an address it
 * prints that it was skipped and exits with 0. make test-relay starts the lobby and runs it. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../src/net/udp.h"
#include "../../src/rollback/rollback.h"
#include "../../src/session/session.h"
#include "../toy/toy.h"

#define PLAYERS 3
#define FRAMES 300

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static void nap(void) {
    struct timespec t = {0, 5 * 1000000L};
    nanosleep(&t, NULL);
}

typedef struct {
    net_socket *sock;
    g64_session *session;
    rb_client *client;
    toy_game game;
    uint64_t final[FRAMES + 400];
    uint32_t done;
} machine;

static machine m[PLAYERS];
static rb_host *host;
static toy_game hostGame;

static void on_game(void *user, int from, const uint8_t *data, size_t len) {
    machine *x = user;
    if (x == &m[0])
        rb_host_receive(host, from, data, len);
    else
        rb_client_receive(x->client, data, len);
}

static void joiner_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)to;
    g64_session_send_game(((machine *)user)->session, 0, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    if (to == 0)
        rb_client_receive(m[0].client, data, len);
    else
        g64_session_send_game(m[0].session, to, data, len);
}

static void host_client_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to;
    rb_host_receive(host, 0, data, len);
}

static void on_final(void *user, uint32_t frame, const void *state) {
    machine *x = user;
    if (frame < FRAMES + 400)
        x->final[frame] = toy_ops.hash_state(state);
    x->done = frame + 1;
}

static rb_input controller(int p, uint32_t f) {
    rb_input in;
    in.x = (int8_t)(((int)(f / 7) * 37 + p * 11) % 161 - 80);
    in.y = (int8_t)(((int)(f / 5) * 53 + p * 17) % 161 - 80);
    in.buttons = (f / 9 + (uint32_t)p) % 4 == 0 ? 0x8000 : 0;
    return in;
}

int main(int argc, char **argv) {
    char err[128];
    net_addr relay;
    uint32_t clock = 1, f, wrong = 0;
    int i, ready = 0, started = 0, ok = 1;
    if (argc < 2) {
        printf("skipped: no relay address (relay_test HOST:PORT)\n");
        return 0;
    }
    net_init();
    if (net_resolve(argv[1], 27065, &relay, err, sizeof err) != 0) {
        printf("FAIL %s\n", err);
        return 1;
    }
    for (i = 0; i < PLAYERS; i++)
        m[i].sock = net_open(0, err, sizeof err);
    net_relay_host(m[0].sock, &relay);
    for (f = 0; f < 2000 && net_relay_state(m[0].sock) != 1; f++) {
        uint8_t d[NET_MAX_DATAGRAM];
        net_addr from;
        net_relay_tick(m[0].sock, clock += 5);
        while (net_recv(m[0].sock, &from, d, sizeof d) >= 0) {
        }
        nap();
    }
    check(net_relay_state(m[0].sock) == 1 && strlen(net_relay_code(m[0].sock)) == NET_ROOM_CODE,
          "the host gets a room code");
    {
        /* A code that names no room is refused. */
        net_socket *s = net_open(0, err, sizeof err);
        net_relay_join(s, &relay, "ZZZZZZ");
        for (f = 0; f < 2000 && net_relay_state(s) == 0; f++) {
            uint8_t d[NET_MAX_DATAGRAM];
            net_addr from;
            net_relay_tick(s, clock += 5);
            while (net_recv(s, &from, d, sizeof d) >= 0) {
            }
            nap();
        }
        check(net_relay_state(s) < 0 && net_relay_refusal(s) == 1, "a code that names no room is refused");
        net_close(s);
    }
    for (i = 1; i < PLAYERS; i++)
        net_relay_join(m[i].sock, &relay, net_relay_code(m[0].sock));
    for (f = 0; f < 2000 && ready < PLAYERS - 1; f++) {
        ready = 0;
        for (i = 1; i < PLAYERS; i++) {
            uint8_t d[NET_MAX_DATAGRAM];
            net_addr from;
            net_relay_tick(m[i].sock, clock += 5);
            while (net_recv(m[i].sock, &from, d, sizeof d) >= 0) {
            }
            ready += net_relay_state(m[i].sock) == 1;
        }
        nap();
    }
    check(ready == PLAYERS - 1, "two joiners join the room by its code");
    m[0].session = g64_session_host(m[0].sock, PLAYERS, 7, 0, 4);
    for (i = 1; i < PLAYERS; i++) {
        net_addr h = net_relay_player(0);
        m[i].session = g64_session_join(m[i].sock, &h, 7, "relay");
    }
    for (f = 0; f < 4000 && started < PLAYERS; f++) {
        started = 0;
        clock += 5;
        for (i = 0; i < PLAYERS; i++) {
            net_relay_tick(m[i].sock, clock);
            started += g64_session_poll(m[i].session, clock, on_game, &m[i]) == G64_SESSION_STARTED;
        }
        nap();
    }
    check(started == PLAYERS, "the session starts through the relay");
    toy_init(&hostGame, PLAYERS);
    host = rb_host_new(&toy_ops, &hostGame, PLAYERS, 4, host_send, NULL, NULL, NULL);
    for (i = 0; i < PLAYERS; i++) {
        toy_init(&m[i].game, PLAYERS);
        m[i].client = rb_client_new(&toy_ops, &m[i].game, PLAYERS, i == 0 ? 0 : g64_session_me(m[i].session), 0,
                                    i == 0 ? host_client_send : joiner_send, on_final, &m[i]);
    }
    for (f = 0; f < FRAMES + 200; f++) {
        clock += 50;
        for (i = 0; i < PLAYERS; i++) {
            net_relay_tick(m[i].sock, clock);
            g64_session_poll(m[i].session, clock, on_game, &m[i]);
        }
        rb_host_tick(host);
        for (i = 0; i < PLAYERS; i++) {
            int me = i == 0 ? 0 : g64_session_me(m[i].session);
            if (rb_client_frame(m[i].client) < FRAMES)
                rb_client_step(m[i].client, controller(me, rb_client_frame(m[i].client)));
            else
                rb_client_idle(m[i].client);
        }
        nap();
    }
    for (i = 0; i < PLAYERS; i++) {
        if (m[i].done < FRAMES)
            ok = 0;
        for (f = 0; f < FRAMES; f++)
            if (m[i].final[f] != m[0].final[f])
                wrong++;
    }
    check(ok && wrong == 0 && rb_host_hash_mismatches(host) == 0 && rb_host_hash_matches(host) > 0,
          "the toy game through the relay: every machine has the same state on every frame");
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
