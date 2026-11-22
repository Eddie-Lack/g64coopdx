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
#define BUILD 0x0b01d001u

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

/* The chat lines each machine got: how many, and the last one with its sender. */
static int chatCount[PLAYERS], chatFrom[PLAYERS];
static char chatLine[PLAYERS][128];
static void on_chat(void *user, int from, const char *text) {
    int i = (int)((machine *)user - m);
    chatCount[i]++;
    chatFrom[i] = from;
    snprintf(chatLine[i], sizeof chatLine[i], "%s", text);
}
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
    net_socket *s3 = net_open(0, err, sizeof err);
    g64_session *wrong = g64_session_join(s1, &hostAddr, ROM + 1, "wrong"), *late = g64_session_join(s2, &hostAddr, ROM, "late");
    /* A joiner with the right ROM whose program is another build. */
    g64_session *other = g64_session_join(s3, &hostAddr, ROM, "other");
    int i;
    g64_session_set_build(late, BUILD);
    g64_session_set_build(wrong, BUILD);
    g64_session_set_build(other, BUILD + 1);
    for (i = 0; i < 200; i++) {
        *clock += 10;
        g64_session_poll(wrong, *clock, NULL, NULL);
        g64_session_poll(late, *clock, NULL, NULL);
        g64_session_poll(other, *clock, NULL, NULL);
        g64_session_poll(m[0].session, *clock, on_game, &m[0]);
    }
    check(g64_session_poll(other, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(other), "version") != NULL,
          "a joiner with another build of the program is refused");
    g64_session_free(other);
    net_close(s3);
    check(g64_session_poll(wrong, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(wrong), "ROM") != NULL,
          "a joiner with another ROM is refused");
    check(g64_session_poll(late, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(late), "full") != NULL,
          "a joiner of a full game is refused");
    g64_session_free(wrong);
    g64_session_free(late);
    net_close(s1);
    net_close(s2);
}

/* Two cases of the welcome, each with a host for two players and one joiner. */
static void test_welcomes(uint32_t *clock) {
    char err[128];
    int round;
    for (round = 0; round < 2; round++) {
        net_socket *hs = net_open(0, err, sizeof err), *js = net_open(0, err, sizeof err);
        net_addr to;
        g64_session *h, *j;
        int i;
        memset(&to, 0, sizeof to);
        to.family = 4, to.ip[0] = 127, to.ip[3] = 1, to.port = net_port(hs);
        h = g64_session_host(hs, 2, ROM, 0, 3);
        j = g64_session_join(js, &to, ROM, "j");
        g64_session_set_build(h, BUILD);
        g64_session_set_build(j, BUILD);
        *clock += 10;
        g64_session_poll(j, *clock, NULL, NULL);
        if (round == 0) {
            /* A forged welcome comes first, with a key of the forger's. The real one must still make the link. */
            uint8_t forged[35] = {0x41, 1, 2};
            memset(forged + 3, 7, 32);
            g64_session_receive(j, &to, forged, sizeof forged, *clock, NULL, NULL);
        } else {
            /* The welcome is lost, and the starts of the next second with it: the host has started, since the
               room is full. The joiner's next hello must get the welcome again. */
            uint8_t d[NET_MAX_DATAGRAM];
            net_addr from;
            for (i = 0; i < 100; i++) {
                *clock += 10;
                g64_session_poll(h, *clock, NULL, NULL);
                while (net_recv(js, &from, d, sizeof d) >= 0) {
                }
            }
            check(g64_session_poll(h, *clock, NULL, NULL) == G64_SESSION_STARTED, "the host starts when the room is full");
        }
        for (i = 0; i < 200; i++) {
            *clock += 10;
            g64_session_poll(h, *clock, NULL, NULL);
            g64_session_poll(j, *clock, NULL, NULL);
        }
        check(g64_session_poll(j, *clock, NULL, NULL) == G64_SESSION_STARTED,
              round == 0 ? "a forged welcome before the real one does not stop the join"
                         : "a joiner whose welcome was lost after the start still gets in");
        g64_session_free(h);
        g64_session_free(j);
        net_close(hs);
        net_close(js);
    }
}

/* A game that players join after its start: a host for three starts alone. It has a clock of its own: it runs
   for longer than the main test's players may stay silent. */
static void test_join_later(uint32_t start) {
    uint32_t time = start, *clock = &time;
    char err[128];
    net_socket *hs = net_open(0, err, sizeof err), *js[4];
    g64_session *h, *j[4] = {NULL, NULL, NULL, NULL};
    net_addr to;
    uint32_t glove, ball, firstEpoch;
    int i, k;
    memset(&to, 0, sizeof to);
    to.family = 4, to.ip[0] = 127, to.ip[3] = 1, to.port = net_port(hs);
    for (k = 0; k < 4; k++)
        js[k] = net_open(0, err, sizeof err);
    h = g64_session_host(hs, 3, ROM, 0, 3);
    g64_session_set_build(h, BUILD);
    g64_session_set_name(h, "host");
    *clock += 10;
    g64_session_poll(h, *clock, NULL, NULL);
    g64_session_begin(h);
    check(g64_session_poll(h, *clock, NULL, NULL) == G64_SESSION_STARTED && g64_session_start_members(h) == 1 &&
              g64_session_joined(h) == 1,
          "a host starts alone");
#define RUN_ALL(n)                                           \
    for (i = 0; i < (n); i++) {                              \
        *clock += 10;                                        \
        g64_session_poll(h, *clock, NULL, NULL);             \
        for (k = 0; k < 4; k++)                              \
            if (j[k])                                        \
                g64_session_poll(j[k], *clock, NULL, NULL);  \
    }
    j[0] = g64_session_join(js[0], &to, ROM, "first");
    g64_session_set_build(j[0], BUILD);
    g64_session_set_colors(j[0], G64_COLOR_SET | 0x112233, G64_COLOR_SET | 0x445566);
    RUN_ALL(150)
    check(g64_session_poll(j[0], *clock, NULL, NULL) == G64_SESSION_STARTED && g64_session_me(j[0]) == 1 &&
              g64_session_start_members(j[0]) == 1 && g64_session_present(j[0], 1) && !g64_session_present(j[0], 2) &&
              !strcmp(g64_session_name(j[0], 0), "host"),
          "a player joins the started game, and is not one of the start's players");
    g64_session_colors(h, 1, &glove, &ball);
    check(g64_session_present(h, 1) && g64_session_joined(h) == 2 && glove == (G64_COLOR_SET | 0x112233) &&
              ball == (G64_COLOR_SET | 0x445566),
          "the host has the new player, with their colors");
    firstEpoch = g64_session_epoch(h, 1);
    j[1] = g64_session_join(js[1], &to, ROM, "second");
    g64_session_set_build(j[1], BUILD);
    RUN_ALL(150)
    check(g64_session_me(j[1]) == 2 && g64_session_present(j[0], 2) && !strcmp(g64_session_name(j[0], 2), "second"),
          "the first player learns of the second from the host's list of players");
    /* The first player stops. The host counts them as gone, and the next joiner takes the place. */
    g64_session_free(j[0]);
    j[0] = NULL;
    RUN_ALL(1100)
    check(g64_session_gone(h, 1) && !g64_session_present(h, 1) && !g64_session_present(j[1], 1) &&
              g64_session_joined(h) == 2,
          "a player who stops is gone, and the others are told");
    j[2] = g64_session_join(js[2], &to, ROM, "third");
    g64_session_set_build(j[2], BUILD);
    RUN_ALL(150)
    check(g64_session_me(j[2]) == 1 && g64_session_present(h, 1) && !g64_session_gone(h, 1) &&
              g64_session_epoch(h, 1) != firstEpoch && !strcmp(g64_session_name(j[1], 1), "third"),
          "the next joiner takes the place of the player who is gone");
    /* The host removes the third player. Their address stays out of the game; another machine may still come.
       Every socket here is at 127.0.0.1, so the other machine is tried from the IPv6 loopback where there is one. */
    g64_session_kick(h, 1);
    RUN_ALL(50)
    check(g64_session_poll(j[2], *clock, NULL, NULL) == G64_SESSION_FAILED, "a removed player is told");
    g64_session_free(j[2]);
    j[2] = g64_session_join(js[3], &to, ROM, "again");
    g64_session_set_build(j[2], BUILD);
    RUN_ALL(150)
    check(g64_session_poll(j[2], *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(j[2]), "removed") != NULL && !g64_session_present(h, 1),
          "a removed player's address cannot join the game again, from any port");
#undef RUN_ALL
    for (k = 0; k < 4; k++) {
        if (j[k])
            g64_session_free(j[k]);
        net_close(js[k]);
    }
    g64_session_free(h);
    net_close(hs);
    /* The place of a player of the start, taken by a newcomer after that player is gone: the newcomer is not one of
       the start's players, and must not build the first game. */
    {
        net_socket *hs2 = net_open(0, err, sizeof err), *a = net_open(0, err, sizeof err), *b = net_open(0, err, sizeof err);
        g64_session *h2, *first, *next;
        to.port = net_port(hs2);
        h2 = g64_session_host(hs2, 2, ROM, 0, 3);
        first = g64_session_join(a, &to, ROM, "first");
        g64_session_set_build(h2, BUILD);
        g64_session_set_build(first, BUILD);
        for (i = 0; i < 150; i++) {
            *clock += 10;
            g64_session_poll(h2, *clock, NULL, NULL);
            g64_session_poll(first, *clock, NULL, NULL);
        }
        check(g64_session_start_members(h2) == 3 && g64_session_start_members(first) == 3,
              "a game for two starts with both players");
        g64_session_free(first);
        for (i = 0; i < 1100; i++) {
            *clock += 10;
            g64_session_poll(h2, *clock, NULL, NULL);
        }
        next = g64_session_join(b, &to, ROM, "next");
        g64_session_set_build(next, BUILD);
        for (i = 0; i < 150; i++) {
            *clock += 10;
            g64_session_poll(h2, *clock, NULL, NULL);
            g64_session_poll(next, *clock, NULL, NULL);
        }
        check(g64_session_poll(next, *clock, NULL, NULL) == G64_SESSION_STARTED && g64_session_me(next) == 1 &&
                  g64_session_start_members(next) == 1 && g64_session_start_members(h2) == 3 &&
                  g64_session_present(h2, 1),
              "a newcomer in the place of a start's player who is gone is not one of the start's players");
        g64_session_free(next);
        g64_session_free(h2);
        net_close(hs2);
        net_close(a);
        net_close(b);
    }
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
    g64_session_set_build(m[0].session, BUILD);
    g64_session_set_name(m[0].session, "Hosty\x01");
    for (i = 1; i < PLAYERS; i++) {
        m[i].session = g64_session_join(sock[i], &hostAddr, ROM, "friend");
        if (m[i].session)
            g64_session_set_build(m[i].session, BUILD);
    }
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
    /* Names: every machine has every player's, and a byte that is not printable ASCII is a question mark. */
    check(!strcmp(g64_session_name(m[1].session, 0), "Hosty?") &&
              !strcmp(g64_session_name(m[1].session, g64_session_me(m[2].session)), "friend") &&
              !strcmp(g64_session_name(m[0].session, g64_session_me(m[1].session)), "friend") &&
              !strcmp(g64_session_name(m[0].session, 0), "Hosty?") && !strcmp(g64_session_name(m[1].session, 99), ""),
          "every machine has every player's name");
    /* Chat: a joiner's line reaches the host and the other joiner once each, with its sender's number. */
    for (i = 0; i < PLAYERS; i++)
        g64_session_on_chat(m[i].session, on_chat, &m[i]);
    g64_session_chat(m[1].session, "hi\tthere");
    for (f = 0; f < 40; f++) {
        clock += 50;
        for (i = 0; i < PLAYERS; i++)
            g64_session_poll(m[i].session, clock, on_game, &m[i]);
    }
    check(chatCount[0] == 1 && chatCount[2] == 1 && chatCount[1] == 0 && chatFrom[0] == g64_session_me(m[1].session) &&
              chatFrom[2] == g64_session_me(m[1].session) && !strcmp(chatLine[0], "hi?there") &&
              !strcmp(chatLine[2], "hi?there"),
          "a joiner's chat line reaches every other machine once");
    g64_session_chat(m[0].session, "welcome");
    g64_session_chat(m[1].session, "");
    for (f = 0; f < 40; f++) {
        clock += 50;
        for (i = 0; i < PLAYERS; i++)
            g64_session_poll(m[i].session, clock, on_game, &m[i]);
    }
    check(chatCount[0] == 1 && chatCount[1] == 1 && chatCount[2] == 2 && chatFrom[1] == 0 && chatFrom[2] == 0 &&
              !strcmp(chatLine[1], "welcome"),
          "the host's line reaches every joiner once, and an empty line is not sent");
    /* A line in the clear, as a raw datagram from a joiner's own address: the host drops it. Only sealed messages
       count after the welcome. */
    {
        int before0 = chatCount[0], before2 = chatCount[2];
        uint8_t d[8] = {0x46, 0, 99, 'c', 'l', 'e', 'a', 'r'};
        clock += 1000;
        net_send(sock[1], &hostAddr, d, sizeof d);
        for (f = 0; f < 6; f++) {
            clock += 10;
            for (i = 0; i < PLAYERS; i++)
                g64_session_poll(m[i].session, clock, on_game, &m[i]);
        }
        check(chatCount[0] == before0 && chatCount[2] == before2, "a chat line in the clear is dropped");
    }
    /* A kick in the clear, as if from the host's address: the joiner stays. So does it after a sealed-looking
       datagram that does not open. */
    {
        uint8_t kick = 0x47, junk[40] = {0x48, 1};
        g64_session_receive(m[1].session, &hostAddr, &kick, 1, clock, on_game, &m[1]);
        g64_session_receive(m[1].session, &hostAddr, junk, sizeof junk, clock, on_game, &m[1]);
        check(g64_session_poll(m[1].session, clock, on_game, &m[1]) == G64_SESSION_STARTED,
              "a kick in the clear and a datagram that does not open change nothing");
    }
    /* A flood: ten different lines from one joiner at once, as opened messages. The host passes on the first and
       drops the rest, so no player can bury the others in lines. */
    {
        int before0 = chatCount[0], before2 = chatCount[2], k;
        clock += 1000;
        for (i = 0; i < PLAYERS; i++)
            g64_session_poll(m[i].session, clock, on_game, &m[i]);
        for (k = 0; k < 10; k++) {
            uint8_t d[8] = {0x46, 0, (uint8_t)(100 + k), 'f', 'l', 'o', 'o', 'd'};
            g64_session_test_inner(m[0].session, g64_session_me(m[1].session), d, sizeof d, clock, on_game, &m[0]);
        }
        for (f = 0; f < 6; f++) {
            clock += 10;
            for (i = 0; i < PLAYERS; i++)
                g64_session_poll(m[i].session, clock, on_game, &m[i]);
        }
        check(chatCount[0] == before0 + 1 && chatCount[2] == before2 + 1,
              "ten lines at once from one joiner: the host passes on one");
    }
    test_refusals(hostAddr, &clock);
    test_welcomes(&clock);
    test_join_later(clock);
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
    /* The host removes the other joiner: that program is told, and the host counts the player as gone. */
    {
        int k, kicked = g64_session_me(m[1].session);
        g64_session_kick(m[0].session, kicked);
        for (k = 0; k < 20; k++) {
            clock += 50;
            g64_session_poll(m[0].session, clock, on_game, &m[0]);
            g64_session_poll(m[1].session, clock, on_game, &m[1]);
        }
        check(g64_session_poll(m[1].session, clock, on_game, &m[1]) == G64_SESSION_FAILED &&
                  strstr(g64_session_error(m[1].session), "removed") != NULL && g64_session_gone(m[0].session, kicked) &&
                  g64_session_poll(m[0].session, clock, on_game, &m[0]) == G64_SESSION_STARTED,
              "a player the host removes is told and is gone");
    }
    for (i = 0; i < PLAYERS; i++) {
        g64_session_free(m[i].session);
        net_close(sock[i]);
    }
    net_shutdown();
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
