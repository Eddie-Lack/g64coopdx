/* The session over the lobby's relay (server/relay.go), on this machine: a host opens a room, two joiners join it by
 * its code, and all three play the toy game through the rollback core. No socket talks to another directly.
 *
 * Usage: relay_test RELAY-ADDRESS (for example 127.0.0.1:27065, with build/lobby running). Without an address it
 * prints that it was skipped and exits with 0. make test-relay starts the lobby and runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/net/udp.h"
#include "../../src/rollback/rollback.h"
#include "../../src/session/session.h"
#include "../toy/toy.h"

#define PLAYERS 3
#define FRAMES 300

static int failures;

/* A failed step ends the test: the later steps need it. */
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if (!ok) {
        printf("FAIL: the relay at the address given did not work\n");
        exit(1);
    }
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
    net_addr relay[2];
    uint32_t clock = 1, f, wrong = 0;
    int i, ready = 0, started = 0, ok = 1;
    if (argc < 2) {
        printf("skipped: no relay address (relay_test HOST:PORT)\n");
        return 0;
    }
    net_init();
    /* The first address answers nothing, like Fly.io's shared address, which drops UDP. Every socket must find the
       relay at the second one. */
    if (net_resolve("127.0.0.1:1", 1, &relay[0], err, sizeof err) != 0 ||
        net_resolve(argv[1], 27065, &relay[1], err, sizeof err) != 0) {
        printf("FAIL %s\n", err);
        return 1;
    }
    for (i = 0; i < PLAYERS; i++)
        m[i].sock = net_open(0, err, sizeof err);
    net_relay_host(m[0].sock, relay, 2);
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
        net_relay_join(s, relay, 2, "ZZZZZZ");
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
        net_relay_join(m[i].sock, relay, 2, net_relay_code(m[0].sock));
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
    {
        /* The list of rooms. The host lists its room for build 77; a player of that build finds it with its title,
           and a player of another build does not. */
        net_socket *asker = net_open(0, err, sizeof err);
        net_room rooms[4];
        int found = -2, foundOther = -2;
        net_relay_announce(m[0].sock, 77, PLAYERS, 3, 1, 1, "The relay\x01test");
        for (f = 0; f < 400 && (found < 0 || foundOther < 0); f++) {
            uint8_t d[NET_MAX_DATAGRAM];
            net_addr from;
            int n;
            if (f % 40 == 0)
                net_rooms_ask(asker, relay, 2, found < 0 ? 77 : 78);
            while ((n = net_recv(asker, &from, d, sizeof d)) >= 0) {
                int got = net_rooms_parse(d, n, rooms, 4);
                if (got >= 0 && found < 0)
                    found = got == 1 && !strcmp(rooms[0].code, net_relay_code(m[0].sock)) &&
                                    !strcmp(rooms[0].title, "The relay?test") && rooms[0].wanted == PLAYERS &&
                                    rooms[0].joined == 3 && rooms[0].flags == 1
                                ? 1
                                : -1;
                else if (got >= 0 && found >= 0)
                    foundOther = got;
            }
            nap();
        }
        check(found == 1, "a player of the host's build finds the listed room, with its title");
        check(foundOther == 0, "a player of another build does not");
        net_close(asker);
    }
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
    {
        /* A ban: the host puts joiner 1 out of the room. That socket is told, and its address cannot join again. */
        net_socket *again = net_open(0, err, sizeof err);
        int banned = 0;
        net_relay_ban(m[0].sock, 1);
        net_relay_join(again, relay, 2, net_relay_code(m[0].sock));
        for (f = 0; f < 2000 && (!banned || net_relay_state(again) == 0); f++) {
            uint8_t d[NET_MAX_DATAGRAM];
            net_addr from;
            clock += 5;
            net_relay_tick(again, clock);
            for (i = 0; i < PLAYERS; i++) {
                net_relay_tick(m[i].sock, clock);
                while (net_recv(m[i].sock, &from, d, sizeof d) >= 0) {
                }
                if (i > 0 && net_relay_state(m[i].sock) < 0 && net_relay_refusal(m[i].sock) == 3)
                    banned = i;
            }
            while (net_recv(again, &from, d, sizeof d) >= 0) {
            }
            nap();
        }
        check(banned != 0, "a banned joiner is told");
        check(net_relay_state(again) < 0 && net_relay_refusal(again) == 3, "a banned address cannot join the room again");
        net_close(again);
    }
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
