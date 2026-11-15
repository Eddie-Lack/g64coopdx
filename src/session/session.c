/* The session in the direct mode (session.h). */
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSG_HELLO = 0x40, MSG_WELCOME, MSG_REFUSE, MSG_START, MSG_PING, MSG_GAME };

/* A joiner says hello every RESEND ms until it is welcomed; the host repeats the start every RESEND ms until each
 * player has answered a game message. A peer silent for TIMEOUT ms is gone. */
#define RESEND 250
#define PING_EVERY 500
#define TIMEOUT 10000
#define NAME_MAX 16

typedef struct {
    int used;
    net_addr addr;
    uint32_t heard; /* when a datagram last came from this peer */
    int started;    /* the peer has sent a game message, so it has the start */
    int gone;       /* stopped answering after the start */
    char name[NAME_MAX + 1];
} peer;

struct g64_session {
    net_socket *sock;
    int host;
    g64_session_state state;
    char error[96];
    uint32_t rom;
    int players, me, level, deadline;
    uint32_t lastSend, lastPing;
    /* The host: players 1 .. players - 1. A joiner: peer 0 is the host. */
    peer peers[G64_SESSION_MAX_PLAYERS];
    char name[NAME_MAX + 1];
};

uint32_t g64_rom_hash(const uint8_t *rom, size_t size) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < size; i++) {
        h ^= rom[i];
        h *= 16777619u;
    }
    return h;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void fail(g64_session *g, const char *why) {
    g->state = G64_SESSION_FAILED;
    snprintf(g->error, sizeof g->error, "%s", why);
}

g64_session *g64_session_host(net_socket *s, int players, uint32_t rom, int level, int deadline) {
    g64_session *g;
    if (!s || players < 2 || players > G64_SESSION_MAX_PLAYERS || level < 0 || level > 255 || deadline < 1 ||
        deadline > 255)
        return NULL;
    g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->sock = s;
    g->host = 1;
    g->rom = rom;
    g->players = players;
    g->level = level;
    g->deadline = deadline;
    g->state = G64_SESSION_WAITING;
    return g;
}

g64_session *g64_session_join(net_socket *s, const net_addr *to, uint32_t rom, const char *name) {
    g64_session *g;
    if (!s || !to)
        return NULL;
    g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->sock = s;
    g->rom = rom;
    g->me = -1;
    g->peers[0].used = 1;
    g->peers[0].addr = *to;
    snprintf(g->name, sizeof g->name, "%s", name ? name : "");
    g->state = G64_SESSION_WAITING;
    return g;
}

void g64_session_free(g64_session *g) { free(g); }

static void send_bytes(g64_session *g, const net_addr *to, const uint8_t *d, size_t n) { net_send(g->sock, to, d, n); }

static void send_start(g64_session *g, int p) {
    uint8_t m[4] = {MSG_START, (uint8_t)g->players, (uint8_t)g->level, (uint8_t)g->deadline};
    send_bytes(g, &g->peers[p].addr, m, sizeof m);
}

/* The host's handling of one datagram. */
static void host_datagram(g64_session *g, const net_addr *from, const uint8_t *d, int n, uint32_t now,
                          g64_game_fn on_game, void *user) {
    int p, free_slot = -1;
    for (p = 1; p < g->players; p++)
        if (g->peers[p].used && !g->peers[p].gone && net_addr_equal(&g->peers[p].addr, from))
            break;
    if (p < g->players) {
        g->peers[p].heard = now;
        if (d[0] == MSG_GAME && n > 1) {
            g->peers[p].started = 1;
            if (g->state == G64_SESSION_STARTED && on_game)
                on_game(user, p, d + 1, (size_t)(n - 1));
        } else if (d[0] == MSG_HELLO && g->state == G64_SESSION_WAITING) {
            /* The welcome was lost: send it again. */
            uint8_t m[3] = {MSG_WELCOME, (uint8_t)p, (uint8_t)g->players};
            send_bytes(g, from, m, sizeof m);
        } else if (d[0] == MSG_HELLO) {
            send_start(g, p);
        }
        return;
    }
    if (d[0] != MSG_HELLO)
        return;
    {
        uint8_t refuse[2] = {MSG_REFUSE, 0};
        if (n < 9 || memcmp(d + 1, "G64", 3) != 0 || d[4] != G64_PROTOCOL) {
            refuse[1] = G64_REFUSE_PROTOCOL;
        } else if (get32(d + 5) != g->rom) {
            refuse[1] = G64_REFUSE_ROM;
        } else if (g->state != G64_SESSION_WAITING) {
            refuse[1] = G64_REFUSE_STARTED;
        } else {
            for (p = 1; p < g->players; p++)
                if (!g->peers[p].used) {
                    free_slot = p;
                    break;
                }
            if (free_slot < 0)
                refuse[1] = G64_REFUSE_FULL;
        }
        if (refuse[1]) {
            send_bytes(g, from, refuse, sizeof refuse);
            return;
        }
    }
    {
        peer *q = &g->peers[free_slot];
        int len = n - 9 > NAME_MAX ? NAME_MAX : n - 9, i;
        uint8_t m[3] = {MSG_WELCOME, (uint8_t)free_slot, (uint8_t)g->players};
        memset(q, 0, sizeof *q);
        q->used = 1;
        q->addr = *from;
        q->heard = now;
        /* The name: printable ASCII only. */
        for (i = 0; i < len; i++)
            q->name[i] = d[9 + i] >= 32 && d[9 + i] < 127 ? (char)d[9 + i] : '?';
        send_bytes(g, from, m, sizeof m);
    }
}

/* A joiner's handling of one datagram. */
static void join_datagram(g64_session *g, const net_addr *from, const uint8_t *d, int n, uint32_t now,
                          g64_game_fn on_game, void *user) {
    if (!net_addr_equal(from, &g->peers[0].addr))
        return;
    g->peers[0].heard = now;
    switch (d[0]) {
    case MSG_WELCOME:
        if (n == 3 && d[2] >= 2 && d[2] <= G64_SESSION_MAX_PLAYERS && d[1] >= 1 && d[1] < d[2] &&
            g->state == G64_SESSION_WAITING) {
            g->me = d[1];
            g->players = d[2];
        }
        break;
    case MSG_REFUSE:
        if (g->state == G64_SESSION_WAITING) {
            static const char *why[] = {"refused", "the game is full", "the host has another ROM",
                                        "the host has another version", "the game has started"};
            fail(g, why[n == 2 && d[1] <= 4 ? d[1] : 0]);
        }
        break;
    case MSG_START:
        if (n == 4 && g->me >= 1 && d[1] == g->players && d[3] >= 1 && g->state == G64_SESSION_WAITING) {
            g->level = d[2];
            g->deadline = d[3];
            g->state = G64_SESSION_STARTED;
        }
        break;
    case MSG_GAME:
        if (n > 1 && g->state == G64_SESSION_STARTED && on_game)
            on_game(user, 0, d + 1, (size_t)(n - 1));
        break;
    default:
        break;
    }
}

void g64_session_receive(g64_session *g, const net_addr *from, const uint8_t *data, size_t len, uint32_t now,
                         g64_game_fn on_game, void *user) {
    if (g->state == G64_SESSION_FAILED || len < 1 || len > NET_MAX_DATAGRAM)
        return;
    if (g->host)
        host_datagram(g, from, data, (int)len, now, on_game, user);
    else
        join_datagram(g, from, data, (int)len, now, on_game, user);
}

g64_session_state g64_session_poll(g64_session *g, uint32_t now, g64_game_fn on_game, void *user) {
    uint8_t d[NET_MAX_DATAGRAM];
    net_addr from;
    int n, p;
    if (g->state == G64_SESSION_FAILED)
        return g->state;
    while ((n = net_recv(g->sock, &from, d, sizeof d)) >= 0)
        g64_session_receive(g, &from, d, (size_t)n, now, on_game, user);
    if (g->host) {
        int joined = 1;
        for (p = 1; p < g->players; p++) {
            peer *q = &g->peers[p];
            if (!q->used)
                continue;
            if (q->gone)
                continue;
            if (now - q->heard > TIMEOUT) {
                if (g->state == G64_SESSION_STARTED) {
                    /* The game goes on without them: the host gives their glove no input from now on. */
                    q->gone = 1;
                    continue;
                }
                q->used = 0;
                continue;
            }
            joined++;
        }
        if (g->state == G64_SESSION_WAITING && joined == g->players)
            g->state = G64_SESSION_STARTED;
        if (g->state == G64_SESSION_STARTED && now - g->lastSend >= RESEND) {
            g->lastSend = now;
            for (p = 1; p < g->players; p++)
                if (g->peers[p].used && !g->peers[p].started && !g->peers[p].gone)
                    send_start(g, p);
        }
        if (now - g->lastPing >= PING_EVERY) {
            uint8_t m = MSG_PING;
            g->lastPing = now;
            for (p = 1; p < g->players; p++)
                if (g->peers[p].used && !g->peers[p].gone)
                    send_bytes(g, &g->peers[p].addr, &m, 1);
        }
    } else {
        if (g->peers[0].heard == 0 && g->lastSend == 0)
            g->peers[0].heard = now ? now : 1;
        if (g->state == G64_SESSION_WAITING && (g->lastSend == 0 || now - g->lastSend >= RESEND)) {
            uint8_t m[9 + NAME_MAX];
            size_t len = strlen(g->name);
            g->lastSend = now ? now : 1;
            m[0] = MSG_HELLO;
            memcpy(m + 1, "G64", 3);
            m[4] = G64_PROTOCOL;
            put32(m + 5, g->rom);
            memcpy(m + 9, g->name, len);
            send_bytes(g, &g->peers[0].addr, m, 9 + len);
        }
        if (now - g->lastPing >= PING_EVERY) {
            uint8_t m = MSG_PING;
            g->lastPing = now;
            send_bytes(g, &g->peers[0].addr, &m, 1);
        }
        if (now - g->peers[0].heard > TIMEOUT)
            fail(g, g->state == G64_SESSION_STARTED ? "the host stopped answering" : "no answer from the host");
    }
    return g->state;
}

void g64_session_send_game(g64_session *g, int to, const uint8_t *data, size_t len) {
    uint8_t m[NET_MAX_DATAGRAM];
    const net_addr *a;
    if (len + 1 > sizeof m || g->state != G64_SESSION_STARTED)
        return;
    if (g->host) {
        if (to < 1 || to >= g->players || !g->peers[to].used || g->peers[to].gone)
            return;
        a = &g->peers[to].addr;
    } else {
        a = &g->peers[0].addr;
    }
    m[0] = MSG_GAME;
    memcpy(m + 1, data, len);
    net_send(g->sock, a, m, len + 1);
}

int g64_session_gone(const g64_session *g, int p) {
    return g->host && p >= 1 && p < g->players && g->peers[p].gone;
}

int g64_session_me(const g64_session *g) { return g->host ? 0 : g->me; }
int g64_session_players(const g64_session *g) { return g->players; }
int g64_session_level(const g64_session *g) { return g->level; }
int g64_session_deadline(const g64_session *g) { return g->deadline; }
const char *g64_session_error(const g64_session *g) { return g->error; }

int g64_session_joined(const g64_session *g) {
    int n = 1, p;
    if (!g->host)
        return g->me >= 1 ? 2 : 1;
    for (p = 1; p < g->players; p++)
        n += g->peers[p].used;
    return n;
}
