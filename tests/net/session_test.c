/* Tests of the session (src/session/session.c) over real UDP sockets on 127.0.0.1: a host and two joiners meet,
 * start, and play the toy game through the rollback core; a joiner with another ROM and a joiner of a full game are
 * refused.
 *
 * Usage: session_test. Exits with 1 if a test fails. */
#include <stdio.h>
#include <string.h>

#include "../../src/net/udp.h"
#include "../../src/rollback/rollback.h"
#include "../../src/session/session.h"
#include "../toy/toy.h"

#define PLAYERS 3
#define FRAMES 300
#define ROM 0x1234abcdu

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

typedef struct {
    g64_session *session;
    rb_client *client; /* on a joiner */
    rb_host *host;     /* on the host */
    toy_game game;
    uint64_t final[FRAMES + 300];
    uint32_t done;
} machine;

static machine m[PLAYERS];
static toy_game hostRef;

static void on_game(void *user, int from, const uint8_t *data, size_t len) {
    machine *x = user;
    if (x->host)
        rb_host_receive(x->host, from, data, len);
    else
        rb_client_receive(x->client, data, len);
}

static void send_fn(void *user, int to, const uint8_t *data, size_t len) {
    machine *x = user;
    g64_session_send_game(x->session, to, data, len);
}

/* The host machine also plays: its client is player 0, and talks to its own host directly. */
static rb_client *hostClient;
static void host_local_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    if (to == 0)
        rb_client_receive(hostClient, data, len);
    else
        g64_session_send_game(m[0].session, to, data, len);
}

static void host_client_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to;
    rb_host_receive(m[0].host, 0, data, len);
}

static void on_final(void *user, uint32_t frame, const void *state) {
    machine *x = user;
    if (frame < FRAMES + 300)
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

static void test_refusals(net_addr hostAddr, uint32_t *clock) {
    char err[128];
    net_socket *s1 = net_open(0, err, sizeof err), *s2 = net_open(0, err, sizeof err);
    g64_session *wrong = g64_session_join(s1, &hostAddr, ROM + 1, "wrong"), *late = g64_session_join(s2, &hostAddr, ROM, "late");
    int i;
    for (i = 0; i < 200; i++) {
        *clock += 10;
        g64_session_poll(wrong, *clock, NULL, NULL);
        g64_session_poll(late, *clock, NULL, NULL);
        g64_session_poll(m[0].session, *clock, on_game, &m[0]);
    }
    check(g64_session_poll(wrong, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(wrong), "ROM") != NULL,
          "a joiner with another ROM is refused");
    check(g64_session_poll(late, *clock, NULL, NULL) == G64_SESSION_FAILED, "a joiner of a started game is refused");
    g64_session_free(wrong);
    g64_session_free(late);
    net_close(s1);
    net_close(s2);
}

int main(void) {
    char err[128];
    net_socket *sock[PLAYERS];
    net_addr hostAddr;
    uint32_t clock = 1, f, wrong = 0;
    int i, started = 0, ok = 1;
    static int dummy;
    (void)dummy;
    net_init();
    for (i = 0; i < PLAYERS; i++)
        sock[i] = net_open(0, err, sizeof err);
    net_resolve("127.0.0.1", net_port(sock[0]), &hostAddr, err, sizeof err);
    m[0].session = g64_session_host(sock[0], PLAYERS, ROM, 0, 3);
    for (i = 1; i < PLAYERS; i++)
        m[i].session = g64_session_join(sock[i], &hostAddr, ROM, "friend");
    /* Meet and start. */
    for (f = 0; f < 500 && started < PLAYERS; f++) {
        clock += 10;
        started = 0;
        for (i = 0; i < PLAYERS; i++)
            started += g64_session_poll(m[i].session, clock, on_game, &m[i]) == G64_SESSION_STARTED;
    }
    check(started == PLAYERS, "the host and two joiners meet and start");
    check(g64_session_me(m[1].session) != g64_session_me(m[2].session) && g64_session_me(m[1].session) >= 1 &&
              g64_session_me(m[2].session) >= 1 && g64_session_players(m[1].session) == PLAYERS,
          "each joiner has its own player number");
    /* Play: the toy game through the rollback core and the session. */
    toy_init(&hostRef, PLAYERS);
    m[0].host = rb_host_new(&toy_ops, &hostRef, PLAYERS, (uint32_t)g64_session_deadline(m[1].session), host_local_send,
                            NULL, NULL, NULL);
    toy_init(&m[0].game, PLAYERS);
    hostClient = rb_client_new(&toy_ops, &m[0].game, PLAYERS, 0, 0, host_client_send, on_final, &m[0]);
    for (i = 1; i < PLAYERS; i++) {
        toy_init(&m[i].game, PLAYERS);
        m[i].client = rb_client_new(&toy_ops, &m[i].game, PLAYERS, g64_session_me(m[i].session), 0, send_fn, on_final,
                                    &m[i]);
    }
    for (f = 0; f < FRAMES + 100; f++) {
        clock += 50;
        for (i = 0; i < PLAYERS; i++)
            g64_session_poll(m[i].session, clock, on_game, &m[i]);
        rb_host_tick(m[0].host);
        for (i = 0; i < PLAYERS; i++) {
            rb_client *c = i == 0 ? hostClient : m[i].client;
            int me = i == 0 ? 0 : g64_session_me(m[i].session);
            if (rb_client_frame(c) < FRAMES)
                rb_client_step(c, controller(me, rb_client_frame(c)));
            else
                rb_client_idle(c);
        }
    }
    for (i = 0; i < PLAYERS; i++) {
        if (m[i].done < FRAMES)
            ok = 0;
        for (f = 0; f < FRAMES; f++)
            if (m[i].final[f] != m[0].final[f])
                wrong++;
    }
    check(ok && wrong == 0 && rb_host_hash_mismatches(m[0].host) == 0 && rb_host_hash_matches(m[0].host) > 0,
          "the toy game through the session: every machine has the same state on every frame");
    test_refusals(hostAddr, &clock);
    /* A joiner who stops answering: the host's game goes on without it. */
    {
        int k;
        for (k = 0; k < 300; k++) {
            clock += 50;
            g64_session_poll(m[0].session, clock, on_game, &m[0]);
            g64_session_poll(m[1].session, clock, on_game, &m[1]);
        }
        check(g64_session_poll(m[0].session, clock, on_game, &m[0]) == G64_SESSION_STARTED &&
                  g64_session_gone(m[0].session, g64_session_me(m[2].session)) &&
                  !g64_session_gone(m[0].session, g64_session_me(m[1].session)),
              "a joiner who stops answering is gone, and the game goes on");
    }
    for (i = 0; i < PLAYERS; i++) {
        g64_session_free(m[i].session);
        net_close(sock[i]);
    }
    net_shutdown();
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
