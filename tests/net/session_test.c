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
    /* A joiner with a level pack, where the host has none. */
    static const uint8_t packId[G64_PACK_ID_SIZE] = {1, 2, 3};
    net_socket *s4 = net_open(0, err, sizeof err);
    g64_session *packed = g64_session_join(s4, &hostAddr, ROM, "packed");
    int i;
    g64_session_set_build(packed, BUILD);
    g64_session_set_pack(packed, packId);
    g64_session_set_build(late, BUILD);
    g64_session_set_build(wrong, BUILD);
    g64_session_set_build(other, BUILD + 1);
    for (i = 0; i < 200; i++) {
        *clock += 10;
        g64_session_poll(wrong, *clock, NULL, NULL);
        g64_session_poll(late, *clock, NULL, NULL);
        g64_session_poll(other, *clock, NULL, NULL);
        g64_session_poll(packed, *clock, NULL, NULL);
        g64_session_poll(m[0].session, *clock, on_game, &m[0]);
    }
    check(g64_session_poll(other, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(other), "version") != NULL,
          "a joiner with another build of the program is refused");
    g64_session_free(other);
    net_close(s3);
    check(g64_session_poll(packed, *clock, NULL, NULL) == G64_SESSION_FAILED &&
              strstr(g64_session_error(packed), "level pack") != NULL,
          "a joiner with another level pack than the host's is refused");
    g64_session_free(packed);
    net_close(s4);
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

/* A bye: a player who leaves is gone at once on the host, and a host that leaves ends the game at once for its
   joiners, before the start and after it. The clock moves far less than the timeout. */
static void test_bye(uint32_t *clock) {
    char err[128];
    int round;
    for (round = 0; round < 2; round++) {
        net_socket *hs = net_open(0, err, sizeof err), *js[2];
        g64_session *h, *j[2];
        net_addr to;
        int i, k, p;
        memset(&to, 0, sizeof to);
        to.family = 4, to.ip[0] = 127, to.ip[3] = 1, to.port = net_port(hs);
        /* Round 0 waits for a third player that never comes; round 1 starts. */
        h = g64_session_host(hs, round == 0 ? 4 : 3, ROM, 0, 3);
        g64_session_set_build(h, BUILD);
        for (i = 0; i < 2; i++) {
            js[i] = net_open(0, err, sizeof err);
            j[i] = g64_session_join(js[i], &to, ROM, "j");
            g64_session_set_build(j[i], BUILD);
        }
        for (k = 0; k < 100; k++) {
            *clock += 10;
            g64_session_poll(h, *clock, NULL, NULL);
            for (i = 0; i < 2; i++)
                g64_session_poll(j[i], *clock, NULL, NULL);
        }
        check(g64_session_joined(h) == 3, round == 0 ? "bye before the start: two joiners are in"
                                                     : "bye after the start: two joiners are in");
        p = g64_session_me(j[0]);
        g64_session_bye(j[0]);
        for (k = 0; k < 5; k++) {
            *clock += 10;
            g64_session_poll(h, *clock, NULL, NULL);
            g64_session_poll(j[1], *clock, NULL, NULL);
        }
        if (round == 0)
            check(g64_session_joined(h) == 2, "a joiner's bye before the start frees its place at once");
        else
            check(g64_session_gone(h, p) && g64_session_poll(h, *clock, NULL, NULL) == G64_SESSION_STARTED,
                  "a joiner's bye after the start: the player is gone at once, and the game goes on");
        g64_session_bye(h);
        for (k = 0; k < 5; k++) {
            *clock += 10;
            g64_session_poll(j[1], *clock, NULL, NULL);
        }
        check(g64_session_poll(j[1], *clock, NULL, NULL) == G64_SESSION_FAILED &&
                  strstr(g64_session_error(j[1]), round == 0 ? "no answer" : "stopped answering") != NULL,
              round == 0 ? "the host's bye before the start ends the wait at once"
                         : "the host's bye after the start ends the game at once");
        g64_session_free(h);
        net_close(hs);
        for (i = 0; i < 2; i++) {
            g64_session_free(j[i]);
            net_close(js[i]);
        }
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
    /* The colors carry the glove's and the ball's models (bits 25 to 28). */
    g64_session_set_colors(j[0], G64_COLOR_SET | 0x112233 | (1u << 25), G64_COLOR_SET | 0x445566 | (3u << 25));
    RUN_ALL(150)
    check(g64_session_poll(j[0], *clock, NULL, NULL) == G64_SESSION_STARTED && g64_session_me(j[0]) == 1 &&
              g64_session_start_members(j[0]) == 1 && g64_session_present(j[0], 1) && !g64_session_present(j[0], 2) &&
              !strcmp(g64_session_name(j[0], 0), "host"),
          "a player joins the started game, and is not one of the start's players");
    g64_session_colors(h, 1, &glove, &ball);
    check(g64_session_present(h, 1) && g64_session_joined(h) == 2 && glove == (G64_COLOR_SET | 0x112233 | (1u << 25)) &&
              ball == (G64_COLOR_SET | 0x445566 | (3u << 25)),
          "the host has the new player, with their colors and models");
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
    /* A host with a level pack takes a joiner with the same pack, and refuses one with none. */
    {
        static const uint8_t packId[G64_PACK_ID_SIZE] = {9, 8, 7, 6};
        net_socket *hs3 = net_open(0, err, sizeof err), *a = net_open(0, err, sizeof err), *b = net_open(0, err, sizeof err);
        g64_session *h3, *same, *none;
        to.port = net_port(hs3);
        h3 = g64_session_host(hs3, 3, ROM, 0, 3);
        same = g64_session_join(a, &to, ROM, "same");
        none = g64_session_join(b, &to, ROM, "none");
        g64_session_set_build(h3, BUILD);
        g64_session_set_build(same, BUILD);
        g64_session_set_build(none, BUILD);
        g64_session_set_pack(h3, packId);
        g64_session_set_pack(same, packId);
        for (i = 0; i < 150; i++) {
            *clock += 10;
            g64_session_poll(h3, *clock, NULL, NULL);
            g64_session_poll(same, *clock, NULL, NULL);
            g64_session_poll(none, *clock, NULL, NULL);
        }
        check(g64_session_me(same) == 1 && g64_session_joined(h3) == 2, "a joiner with the host's level pack gets in");
        check(g64_session_poll(none, *clock, NULL, NULL) == G64_SESSION_FAILED &&
                  strstr(g64_session_error(none), "level pack") != NULL,
              "a joiner without the host's level pack is refused");
        g64_session_free(same);
        g64_session_free(none);
        g64_session_free(h3);
        net_close(hs3);
        net_close(a);
        net_close(b);
    }
    /* The place of a player of the start, taken by a newcomer after that player is gone: the newcomer isn't one of
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

/* A level pack from the host. The pack's bytes here are made up: the session moves bytes and doesn't read them. They
   stand for four files: a manifest of 40 bytes, a level file of 20000, a script of 660 and a models.bin of 3000. */
#define PACK_TOTAL (40 + 20000 + 660 + 3000)
static uint8_t packBytes[PACK_TOTAL];

/* An offer as the host sends it: the type, the id, the four sizes, then the name. */
static size_t make_offer(uint8_t *m, const uint8_t *id, unsigned manifest, uint32_t lev, uint32_t script,
                         uint32_t models, const char *name) {
    size_t i, n = strlen(name);
    m[0] = 0x49;
    memcpy(m + 1, id, G64_PACK_ID_SIZE);
    m[17] = (uint8_t)manifest, m[18] = (uint8_t)(manifest >> 8);
    for (i = 0; i < 4; i++) {
        m[19 + i] = (uint8_t)(lev >> (8 * i));
        m[23 + i] = (uint8_t)(script >> (8 * i));
        m[27 + i] = (uint8_t)(models >> (8 * i));
    }
    memcpy(m + 31, name, n);
    return 31 + n;
}

/* A hello of protocol proto to host from a made-up address. 1 when the host took the player in. */
static int hello_joins(uint8_t proto, uint32_t *clock) {
    char err[128];
    net_socket *hs = net_open(0, err, sizeof err);
    g64_session *host = g64_session_host(hs, 2, ROM, 0, 3);
    uint8_t hello[69 + 5] = {0x40, 'G', '6', '4', proto, ROM & 0xFF, (ROM >> 8) & 0xFF, (ROM >> 16) & 0xFF, ROM >> 24,
                             BUILD & 0xFF, (BUILD >> 8) & 0xFF, (BUILD >> 16) & 0xFF, BUILD >> 24};
    net_addr from;
    int joined;
    memset(&from, 0, sizeof from);
    from.family = 4, from.ip[0] = 127, from.ip[3] = 1, from.port = 9;
    memset(hello + 21, 9, 32);
    memcpy(hello + 69, "proto", 5);
    g64_session_set_build(host, BUILD);
    g64_session_poll(host, *clock, NULL, NULL);
    g64_session_receive(host, &from, hello, sizeof hello, *clock, NULL, NULL);
    joined = g64_session_joined(host) == 2;
    g64_session_free(host);
    net_close(hs);
    return joined;
}

static void pack_pump(g64_session *host, g64_session *join, uint32_t *clock, int rounds) {
    int i;
    for (i = 0; i < rounds; i++) {
        *clock += 10;
        g64_session_poll(join, *clock, NULL, NULL);
        g64_session_poll(host, *clock, NULL, NULL);
    }
}

static void test_pack(uint32_t *clock) {
    static const uint8_t hostId[G64_PACK_ID_SIZE] = {7, 7, 7, 1}, otherId[G64_PACK_ID_SIZE] = {9, 9};
    char err[128];
    g64_pack_offer info;
    net_addr hostAddr;
    size_t i, total = sizeof packBytes;
    for (i = 0; i < total; i++)
        packBytes[i] = (uint8_t)(i * 31 + i / 251);
    memset(&info, 0, sizeof info);
    snprintf(info.name, sizeof info.name, "A test\x02pack");
    info.manifestSize = 40;
    info.levSize = 20000;
    info.scriptSize = 660;
    info.modelsSize = 3000;
    check(G64_PROTOCOL == 15 && hello_joins(G64_PROTOCOL, clock) && !hello_joins(14, clock),
          "the protocol is 15, and a hello of protocol 14 is refused");
    {
        /* A joiner without the pack, and one with another pack, before the start. */
        net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err), *s2 = net_open(0, err, sizeof err);
        g64_session *host = g64_session_host(hs, 3, ROM, 0, 3), *a, *b;
        const g64_pack_offer *o;
        uint8_t piece[5 + 1024] = {0x4B};
        int me;
        net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
        g64_session_set_build(host, BUILD);
        g64_session_set_pack(host, hostId);
        g64_session_send_pack(host, &info, packBytes);
        a = g64_session_join(s1, &hostAddr, ROM, "none");
        g64_session_set_build(a, BUILD);
        pack_pump(host, a, clock, 60);
        o = g64_session_pack_offer(a);
        check(g64_session_pack_state(a) == G64_PACK_OFFERED && memcmp(o->id, hostId, sizeof hostId) == 0 &&
                  !strcmp(o->name, "A test?pack") && o->manifestSize == 40 && o->levSize == 20000 &&
                  o->scriptSize == 660 && o->modelsSize == 3000,
              "a joiner without the host's pack gets the offer");
        me = g64_session_me(a);
        check(me >= 1 && g64_session_joined(host) == 1 && g64_session_poll(host, *clock, NULL, NULL) == G64_SESSION_WAITING,
              "a joiner without the pack has a place and is not counted");
        /* A piece before the player said yes: nothing is kept. */
        memcpy(piece + 5, packBytes, 1024);
        g64_session_test_inner(a, 0, piece, sizeof piece, *clock, NULL, NULL);
        check(g64_session_pack_have(a) == 0 && g64_session_pack_bytes(a) == NULL, "a piece before the player's yes is dropped");
        g64_session_begin(host);
        pack_pump(host, a, clock, 60);
        check(g64_session_poll(host, *clock, NULL, NULL) == G64_SESSION_STARTED && !g64_session_present(host, me) &&
                  g64_session_start_members(host) == 1 && g64_session_poll(a, *clock, NULL, NULL) == G64_SESSION_WAITING,
              "the game starts without the player who lacks the pack");
        check(g64_session_pack_accept(a) == 0 && g64_session_pack_state(a) == G64_PACK_LOADING, "the player says yes");
        /* Pieces that aren't the next one, or that go past the end. */
        piece[1] = 0, piece[2] = 4;
        g64_session_test_inner(a, 0, piece, sizeof piece, *clock, NULL, NULL);
        check(g64_session_pack_have(a) == 0, "a piece from a later offset is dropped");
        for (i = 0; i < 2000 && g64_session_pack_state(a) == G64_PACK_LOADING; i++)
            pack_pump(host, a, clock, 1);
        check(g64_session_pack_state(a) == G64_PACK_LOADED && g64_session_pack_have(a) == total &&
                  g64_session_pack_bytes(a) && memcmp(g64_session_pack_bytes(a), packBytes, total) == 0,
              "every byte of the pack comes, in order, models.bin last");
        piece[1] = piece[2] = 0;
        g64_session_test_inner(a, 0, piece, sizeof piece, *clock, NULL, NULL);
        check(g64_session_pack_have(a) == total, "a piece after the last one is dropped");
        g64_session_pack_ready(a);
        pack_pump(host, a, clock, 60);
        check(g64_session_pack_state(a) == G64_PACK_DONE && g64_session_present(host, me) &&
                  g64_session_poll(a, *clock, NULL, NULL) == G64_SESSION_STARTED &&
                  !((g64_session_start_members(a) >> me) & 1),
              "the player with the pack joins the running game as one who came later");
        /* A second joiner, with another pack: the same offer. A host that then offers another pack ends the session. */
        b = g64_session_join(s2, &hostAddr, ROM, "other");
        g64_session_set_build(b, BUILD);
        g64_session_set_pack(b, otherId);
        for (i = 0; i < 60; i++) {
            pack_pump(host, a, clock, 1);
            g64_session_poll(b, *clock, NULL, NULL);
        }
        check(g64_session_pack_state(b) == G64_PACK_OFFERED, "a joiner with another pack gets the offer");
        {
            uint8_t offer[31 + G64_PACK_NAME_MAX];
            size_t n = make_offer(offer, hostId, 40, 20000, 660, 3000, "A test?pack");
            g64_session_test_inner(b, 0, offer, n, *clock, NULL, NULL);
            check(g64_session_poll(b, *clock, NULL, NULL) == G64_SESSION_WAITING &&
                      g64_session_pack_state(b) == G64_PACK_OFFERED,
                  "the same offer again changes nothing");
            n = make_offer(offer, hostId, 40, 20000, 660, 3008, "A test?pack");
            g64_session_test_inner(b, 0, offer, n, *clock, NULL, NULL);
            check(g64_session_poll(b, *clock, NULL, NULL) == G64_SESSION_FAILED &&
                      strstr(g64_session_error(b), "changed") != NULL,
                  "a second offer with another size of models.bin ends the session");
        }
        g64_session_free(a), g64_session_free(b), g64_session_free(host);
        net_close(hs), net_close(s1), net_close(s2);
    }
    {
        /* A joiner who asks for the first pieces again and again gets the pack three times over and no more, and
           loses the place. A start that comes while the offer is open doesn't count. */
        net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err);
        g64_session *host = g64_session_host(hs, 2, ROM, 0, 3), *a;
        uint8_t want[21] = {0x4A, 7, 7, 7, 1}, start[9 + 25 * 2] = {0x43, 2, 0, 3, 0, 3, 0, 3, 0};
        int k;
        net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
        g64_session_set_build(host, BUILD);
        g64_session_set_pack(host, hostId);
        g64_session_send_pack(host, &info, packBytes);
        a = g64_session_join(s1, &hostAddr, ROM, "greedy");
        g64_session_set_build(a, BUILD);
        pack_pump(host, a, clock, 60);
        g64_session_test_inner(a, 0, start, sizeof start, *clock, NULL, NULL);
        check(g64_session_poll(a, *clock, NULL, NULL) == G64_SESSION_WAITING, "a start while the offer is open does not count");
        check(g64_session_peer_addr(host, 1) != NULL, "the joiner holds a place before it asks");
        for (k = 0; k < 400 && g64_session_peer_addr(host, 1) != NULL; k++) {
            *clock += 30;
            g64_session_test_inner(host, 1, want, sizeof want, *clock, NULL, NULL);
            g64_session_poll(a, *clock, NULL, NULL);
            g64_session_poll(host, *clock, NULL, NULL);
        }
        check(g64_session_peer_addr(host, 1) == NULL && k > 3 * PACK_TOTAL / 8192 && k < 60,
              "a joiner who asks for the pack over and over loses the place");
        g64_session_free(a), g64_session_free(host);
        net_close(hs), net_close(s1);
    }
    {
        /* Offers that aren't as they must be are dropped. */
        static const uint8_t oneId[G64_PACK_ID_SIZE] = {1};
        static const uint8_t noId[G64_PACK_ID_SIZE] = {0};
        static const struct {
            uint32_t models;
            int taken;
            const char *what;
        } models[] = {
            {15, 0, "an offer of a models.bin of 15 bytes is dropped"},
            {G64_PACK_MODELS_LIMIT + 1, 0, "an offer of a models.bin of more than 4 MiB and 24 bytes is dropped"},
            {16, 1, "an offer of a models.bin of 16 bytes is taken"},
            {G64_PACK_MODELS_LIMIT, 1, "an offer of a models.bin of 4 MiB and 24 bytes is taken"},
        };
        net_socket *s1 = net_open(0, err, sizeof err);
        g64_session *a = g64_session_join(s1, &hostAddr, ROM, "x");
        uint8_t welcome[35] = {0x41, 1, 2}, offer[31 + 33];
        net_addr from = hostAddr;
        size_t n, k;
        memset(welcome + 3, 7, 32);
        g64_session_poll(a, *clock, NULL, NULL);
        g64_session_receive(a, &from, welcome, sizeof welcome, *clock, NULL, NULL);
        n = make_offer(offer, oneId, 40, 8, 0, 0, "nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn");
        check(n == 31 + 33, "the offer's name starts at byte 31");
        g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer with a name of 33 bytes is dropped");
        g64_session_test_inner(a, 0, offer, 31, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of a pack without a name is dropped");
        g64_session_test_inner(a, 0, offer, 30, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of the old protocol's form is dropped");
        n = make_offer(offer, oneId, 40, 0x110000, 0, 0, "n");
        g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of a level file that is too large is dropped");
        n = make_offer(offer, oneId, 40, 7, 0, 0, "n");
        g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of a level file that is too small is dropped");
        n = make_offer(offer, noId, 40, 8, 0, 0, "n");
        g64_session_test_inner(a, 0, offer, 31, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of no pack that has sizes is dropped");
        n = make_offer(offer, noId, 0, 0, 0, 16, "");
        g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_NONE, "an offer of no pack with a models.bin is dropped");
        n = make_offer(offer, oneId, 40, 8, 0, 0, "n");
        g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
        check(g64_session_pack_state(a) == G64_PACK_OFFERED && g64_session_pack_offer(a)->levSize == 8 &&
                  g64_session_pack_offer(a)->modelsSize == 0,
              "an offer within the limits is taken");
        {
            static const uint8_t evilId[G64_PACK_ID_SIZE] = {1, 2, 3};
            n = make_offer(offer, evilId, 40, 8, 0, 0, "n");
            g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
            check(g64_session_poll(a, *clock, NULL, NULL) == G64_SESSION_FAILED &&
                      strstr(g64_session_error(a), "changed") != NULL,
                  "a second offer of another pack ends the session");
        }
        g64_session_free(a);
        /* The limits of models.bin, each on a joiner of its own: an offer that is taken stays. */
        for (k = 0; k < sizeof models / sizeof models[0]; k++) {
            a = g64_session_join(s1, &hostAddr, ROM, "x");
            g64_session_poll(a, *clock, NULL, NULL);
            g64_session_receive(a, &from, welcome, sizeof welcome, *clock, NULL, NULL);
            n = make_offer(offer, oneId, 40, 8, 0, models[k].models, "n");
            g64_session_test_inner(a, 0, offer, n, *clock, NULL, NULL);
            check(models[k].taken ? g64_session_pack_state(a) == G64_PACK_OFFERED &&
                                        g64_session_pack_offer(a)->modelsSize == models[k].models
                                  : g64_session_pack_state(a) == G64_PACK_NONE,
                  models[k].what);
            g64_session_free(a);
        }
        net_close(s1);
    }
    {
        /* A host without a pack, and a joiner who came with one: the joiner plays without it. */
        net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err);
        g64_session *host = g64_session_host(hs, 2, ROM, 0, 3), *a;
        net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
        g64_session_set_build(host, BUILD);
        g64_session_send_pack(host, NULL, NULL);
        a = g64_session_join(s1, &hostAddr, ROM, "packed");
        g64_session_set_build(a, BUILD);
        g64_session_set_pack(a, otherId);
        pack_pump(host, a, clock, 60);
        check(g64_session_pack_state(a) == G64_PACK_OFFERED && g64_session_pack_offer(a)->levSize == 0 &&
                  g64_session_pack_accept(a) != 0,
              "a host without a pack offers none, and there is nothing to download");
        g64_session_pack_ready(a);
        pack_pump(host, a, clock, 60);
        check(g64_session_poll(a, *clock, NULL, NULL) == G64_SESSION_STARTED &&
                  g64_session_poll(host, *clock, NULL, NULL) == G64_SESSION_STARTED &&
                  ((g64_session_start_members(host) >> 1) & 1),
              "the joiner who dropped its pack starts with the host");
        g64_session_free(a), g64_session_free(host);
        net_close(hs), net_close(s1);
    }
    {
        /* A joiner who never answers the offer loses the place after five minutes. */
        net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err);
        g64_session *host = g64_session_host(hs, 2, ROM, 0, 3), *a;
        int k;
        net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
        g64_session_set_build(host, BUILD);
        g64_session_set_pack(host, hostId);
        g64_session_send_pack(host, &info, packBytes);
        a = g64_session_join(s1, &hostAddr, ROM, "slow");
        g64_session_set_build(a, BUILD);
        pack_pump(host, a, clock, 60);
        check(g64_session_peer_addr(host, 1) != NULL, "a joiner who reads the offer holds a place");
        for (k = 0; k < 5 * 60 * 2 + 20; k++) {
            *clock += 490;
            g64_session_poll(a, *clock, NULL, NULL);
            g64_session_poll(host, *clock, NULL, NULL);
        }
        check(g64_session_peer_addr(host, 1) == NULL && g64_session_poll(host, *clock, NULL, NULL) == G64_SESSION_WAITING,
              "a joiner who does not answer the offer loses the place");
        g64_session_free(a), g64_session_free(host);
        net_close(hs), net_close(s1);
    }
    {
        /* A pack of the largest size on a slow link. The host answers once a second, which stands for a round trip
           of one second. A joiner gets one burst of 8 pieces for each answer, so the download takes about 11
           minutes. The host holds the place for longer than the five minutes of a small pack. A joiner who never
           answers the offer of such a pack still loses the place, after about 12.5 minutes. */
        static uint8_t big[G64_PACK_MANIFEST_LIMIT + G64_PACK_LEVEL_LIMIT + G64_PACK_SCRIPT_LIMIT +
                           G64_PACK_MODELS_LIMIT];
        g64_pack_offer large;
        net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err);
        g64_session *host = g64_session_host(hs, 2, ROM, 0, 3), *a;
        size_t bigTotal = sizeof big, before, most = 0;
        uint32_t t0;
        int k, kept = 1;
        for (i = 0; i < bigTotal; i += 4096)
            big[i] = (uint8_t)(i >> 12);
        memset(&large, 0, sizeof large);
        snprintf(large.name, sizeof large.name, "Big");
        large.manifestSize = G64_PACK_MANIFEST_LIMIT;
        large.levSize = G64_PACK_LEVEL_LIMIT;
        large.scriptSize = G64_PACK_SCRIPT_LIMIT;
        large.modelsSize = G64_PACK_MODELS_LIMIT;
        net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
        g64_session_set_build(host, BUILD);
        g64_session_set_pack(host, hostId);
        g64_session_send_pack(host, &large, big);
        a = g64_session_join(s1, &hostAddr, ROM, "far");
        g64_session_set_build(a, BUILD);
        pack_pump(host, a, clock, 60);
        check(g64_session_pack_accept(a) == 0, "a joiner says yes to a pack of the largest size");
        t0 = *clock;
        for (k = 0; k < 100000 && g64_session_pack_state(a) == G64_PACK_LOADING; k++) {
            *clock += 10;
            g64_session_poll(a, *clock, NULL, NULL);
            if (k % 100 == 99) {
                before = g64_session_pack_have(a);
                g64_session_poll(host, *clock, NULL, NULL);
                /* The pieces arrive on the joiner's next poll. */
                *clock += 10, k++;
                g64_session_poll(a, *clock, NULL, NULL);
                if (g64_session_pack_have(a) - before > most)
                    most = g64_session_pack_have(a) - before;
                if (g64_session_peer_addr(host, 1) == NULL)
                    kept = 0;
            }
        }
        check(most == 8 * 1024, "the host sends one burst of 8 pieces for each request");
        check(kept && g64_session_pack_state(a) == G64_PACK_LOADED && g64_session_pack_have(a) == bigTotal &&
                  memcmp(g64_session_pack_bytes(a), big, bigTotal) == 0 && *clock - t0 > 10 * 60 * 1000 &&
                  *clock - t0 < 12 * 60 * 1000,
              "a pack of the largest size comes whole over a slow link, and the joiner keeps the place");
        g64_session_free(a), g64_session_free(host);
        host = g64_session_host(hs, 2, ROM, 0, 3);
        g64_session_set_build(host, BUILD);
        g64_session_set_pack(host, hostId);
        g64_session_send_pack(host, &large, big);
        a = g64_session_join(s1, &hostAddr, ROM, "silent");
        g64_session_set_build(a, BUILD);
        pack_pump(host, a, clock, 60);
        for (k = 0; k < 6 * 60 * 2; k++) {
            *clock += 500;
            g64_session_poll(a, *clock, NULL, NULL);
            g64_session_poll(host, *clock, NULL, NULL);
        }
        check(g64_session_peer_addr(host, 1) != NULL, "after six minutes the place for a large pack is still held");
        for (k = 0; k < 7 * 60 * 2; k++) {
            *clock += 500;
            g64_session_poll(a, *clock, NULL, NULL);
            g64_session_poll(host, *clock, NULL, NULL);
        }
        check(g64_session_peer_addr(host, 1) == NULL, "after 13 minutes it is free");
        g64_session_free(a), g64_session_free(host);
        net_close(hs), net_close(s1);
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
    /* Names: every machine has every player's, and a byte that isn't printable ASCII is a question mark. */
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
    /* A kick in the clear, as if from the host's address: the joiner stays. It also stays after a sealed-looking
       datagram that doesn't open. */
    {
        uint8_t kick = 0x47, junk[40] = {0x48, 1};
        g64_session_receive(m[1].session, &hostAddr, &kick, 1, clock, on_game, &m[1]);
        g64_session_receive(m[1].session, &hostAddr, junk, sizeof junk, clock, on_game, &m[1]);
        check(g64_session_poll(m[1].session, clock, on_game, &m[1]) == G64_SESSION_STARTED,
              "a kick in the clear and a datagram that does not open change nothing");
    }
    /* A flood: ten different lines from one joiner at once, as opened messages. The host passes on the first and
       drops the rest. */
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
    /* A joiner's new colors, model and name after the start: they reach the host, and through its list of players
       the other joiner. An older profile message, and one with a color that isn't valid, change nothing. */
    {
        int me1 = g64_session_me(m[1].session);
        uint32_t glove = 0, ball = 0, glove2 = 0, ball2 = 0;
        uint32_t newGlove = G64_COLOR_SET | 0x112233u | (3u << 25), newBall = G64_COLOR_SET | 0x445566u;
        g64_session_set_profile(m[1].session, newGlove, newBall, "Renamed");
        for (f = 0; f < 60; f++) {
            clock += 50;
            for (i = 0; i < PLAYERS; i++)
                g64_session_poll(m[i].session, clock, on_game, &m[i]);
        }
        g64_session_colors(m[0].session, me1, &glove, &ball);
        g64_session_colors(m[2].session, me1, &glove2, &ball2);
        check(glove == newGlove && ball == newBall && glove2 == newGlove && ball2 == newBall &&
                  !strcmp(g64_session_name(m[0].session, me1), "Renamed") &&
                  !strcmp(g64_session_name(m[2].session, me1), "Renamed"),
              "a joiner's new colors and name after the start reach the host and the other joiner");
        {
            /* An old number (the one before the last), then a color with bits that no color has. */
            uint8_t old[14] = {0x4E, 0, 0, 0, 0, 1, 0, 0, 0, 1, 'o', 'l', 'd', 0};
            uint8_t bad[12] = {0x4E, 200, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 'x', 'y'};
            g64_session_test_inner(m[0].session, me1, old, 13, clock, on_game, &m[0]);
            g64_session_test_inner(m[0].session, me1, bad, sizeof bad, clock, on_game, &m[0]);
            g64_session_colors(m[0].session, me1, &glove, &ball);
            check(glove == newGlove && ball == newBall && !strcmp(g64_session_name(m[0].session, me1), "Renamed"),
                  "an old profile message and one with a bad color change nothing");
        }
    }
    /* The list of players carries each player's level: the host sets it, and every joiner gets it. A level
       that isn't set is unknown. */
    {
        int k, p1 = g64_session_me(m[1].session);
        g64_session_set_level(m[0].session, 0, 10);
        g64_session_set_level(m[0].session, p1, 42);
        g64_session_set_level(m[0].session, g64_session_me(m[2].session), 99);
        for (k = 0; k < 40; k++) {
            clock += 50;
            for (i = 0; i < PLAYERS; i++)
                g64_session_poll(m[i].session, clock, on_game, &m[i]);
        }
        check(g64_session_player_level(m[2].session, 0) == 10 && g64_session_player_level(m[2].session, p1) == 42 &&
                  g64_session_player_level(m[1].session, g64_session_me(m[2].session)) == -1 &&
                  g64_session_player_level(m[2].session, 99) == -1,
              "every joiner has each player's level, and a level out of range is unknown");
    }
    test_refusals(hostAddr, &clock);
    test_welcomes(&clock);
    test_bye(&clock);
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
    test_pack(&clock);
    net_shutdown();
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
