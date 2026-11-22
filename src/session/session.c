/* The session in the direct mode (session.h). */
#include "session.h"

#include "seal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hello, the welcome and the refusal travel in the clear: they come before the two ends share a key. Every
   other message travels sealed (seal.h), inside a datagram that starts with G64_SEAL_TYPE. */
enum { MSG_HELLO = 0x40, MSG_WELCOME, MSG_REFUSE, MSG_START, MSG_PING, MSG_GAME, MSG_CHAT, MSG_KICK };
/* The hello's bytes before the name: the type, "G64", the protocol, the ROM, the build, two colors, and the
   joiner's public key. */
#define HELLO_KEY 21
#define HELLO_SIZE (HELLO_KEY + G64_SEAL_KEY)
/* The welcome: the type, the player's number, the number of players, and the host's public key for this player. */
#define WELCOME_SIZE (3 + G64_SEAL_KEY)
#define CHAT_SENDS 3
#define CHAT_EVERY 150
#define CHAT_GAP 400
#define KICK_HOLD 3000

/* A joiner says hello every RESEND ms until it is welcomed; the host repeats the start every RESEND ms until each
 * player has answered a game message. A peer silent for TIMEOUT ms is gone. */
#define RESEND 250
#define PING_EVERY 500
#define TIMEOUT 10000
#define NAME_MAX 16

typedef struct {
    int used;
    net_addr addr;
    uint32_t heard; /* when a message that only this peer could have made last came */
    /* The sealed link with this peer, the public key this side sent it, and the one it sent. On the host each
       player has a link of their own; on a joiner peer 0 is the host, and sealed is 0 until the welcome. */
    g64_seal seal;
    int sealed;
    uint8_t myKey[G64_SEAL_KEY], theirKey[G64_SEAL_KEY];
    int started;    /* the peer has sent a game message, so it has the start */
    int gone;       /* stopped answering after the start */
    char name[NAME_MAX + 1];
    uint32_t glove, ball; /* the player's colors (session.h, G64_COLOR_SET) */
    /* The host removed this player (g64_session_kick). Their program gets the kick again whenever it sends. Before
       the start the place is held for KICK_HOLD ms from kickTime, then it is free again. */
    int kicked;
    uint32_t kickTime;
    /* The host's limit on this player's chat: the number of their last line, how often it was passed on, and when
       it first came. A new line within CHAT_GAP ms of the last one is dropped, and so is a fourth copy of a line. */
    uint8_t chatId;
    int chatHave, chatCopies;
    uint32_t chatTime;
} peer;

struct g64_session {
    net_socket *sock;
    int host;
    g64_session_state state;
    char error[96];
    uint32_t rom;
    /* This program's build (g64_session_set_build). */
    uint32_t build;
    int players, me, level, deadline, flags;
    uint32_t lastSend, lastPing;
    /* The host: players 1 .. players - 1. A joiner: peer 0 is the host. */
    peer peers[G64_SESSION_MAX_PLAYERS];
    char name[NAME_MAX + 1];
    uint32_t glove, ball; /* this machine's player's colors */
    /* A joiner's secret key, and its hello as first sent: every repeat has the same bytes, which the keys are
       made from. */
    uint8_t secret[G64_SEAL_KEY];
    uint8_t hello[HELLO_SIZE + NAME_MAX];
    size_t helloLen;
    /* After the start: every player's colors, by player number. */
    uint32_t colors[G64_SESSION_MAX_PLAYERS][2];
    /* After the start: every player's name, by player number. */
    char names[G64_SESSION_MAX_PLAYERS][NAME_MAX + 1];
    /* Chat. This machine's latest line goes out CHAT_SENDS times, CHAT_EVERY ms apart: a datagram can be lost. Each
       line has a number, and a line whose number is the player's last one is a repeat. */
    char chatOut[G64_CHAT_MAX + 1];
    uint8_t chatId;
    int chatSends;
    uint32_t chatLast;
    uint8_t chatSeen[G64_SESSION_MAX_PLAYERS], chatHave[G64_SESSION_MAX_PLAYERS];
    g64_chat_fn chatFn;
    void *chatUser;
};

/* A color from the network: the chosen bit and 24 bits of color, nothing else. */
static int valid_color(uint32_t c) { return (c >> 25) == 0; }

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
    /* Without random bytes there is no key, and without a key no session. */
    if (g64_seal_keypair(g->secret, g->peers[0].myKey) != 0)
        fail(g, "the system gave no random numbers");
    return g;
}

void g64_session_free(g64_session *g) { free(g); }

/* In the clear: a hello, a welcome or a refusal. */
static void send_bytes(g64_session *g, const net_addr *to, const uint8_t *d, size_t n) { net_send(g->sock, to, d, n); }

/* Sealed, to a peer whose link is made. */
static void send_sealed(g64_session *g, peer *q, const uint8_t *d, size_t n) {
    uint8_t m[NET_MAX_DATAGRAM];
    size_t len;
    if (!q->sealed)
        return;
    len = g64_seal_lock(&q->seal, d, n, m, sizeof m);
    if (len)
        net_send(g->sock, &q->addr, m, len);
}

static void send_welcome(g64_session *g, int p) {
    uint8_t m[WELCOME_SIZE] = {MSG_WELCOME, (uint8_t)p, (uint8_t)g->players};
    memcpy(m + 3, g->peers[p].myKey, G64_SEAL_KEY);
    send_bytes(g, &g->peers[p].addr, m, sizeof m);
}

/* The start's bytes for each player: two colors and a name. */
#define START_EACH (8 + NAME_MAX)

/* A name or a chat line from the network or from this machine: printable ASCII, and '?' for anything else. */
static void clean_text(char *out, size_t size, const uint8_t *in, size_t n) {
    size_t i;
    for (i = 0; i + 1 < size && i < n && in[i]; i++)
        out[i] = in[i] >= 32 && in[i] < 127 ? (char)in[i] : '?';
    out[i] = 0;
}

static void send_start(g64_session *g, int p) {
    uint8_t m[5 + START_EACH * G64_SESSION_MAX_PLAYERS] = {MSG_START, (uint8_t)g->players, (uint8_t)g->level,
                                                           (uint8_t)g->deadline, (uint8_t)g->flags};
    int q;
    for (q = 0; q < g->players; q++) {
        uint8_t *e = m + 5 + START_EACH * q;
        const char *name = q == 0 ? g->name : g->peers[q].name;
        put32(e, q == 0 ? g->glove : g->peers[q].glove);
        put32(e + 4, q == 0 ? g->ball : g->peers[q].ball);
        memcpy(e + 8, name, strlen(name) < NAME_MAX ? strlen(name) : NAME_MAX);
    }
    send_sealed(g, &g->peers[p], m, 5 + START_EACH * (size_t)g->players);
}

/* A chat line reaches this machine: once per line. */
static void chat_arrives(g64_session *g, int from, uint8_t id, const uint8_t *text, size_t n) {
    char line[G64_CHAT_MAX + 1];
    if (from < 0 || from >= G64_SESSION_MAX_PLAYERS || (g->chatHave[from] && g->chatSeen[from] == id))
        return;
    g->chatHave[from] = 1;
    g->chatSeen[from] = id;
    clean_text(line, sizeof line, text, n);
    if (line[0] && g->chatFn)
        g->chatFn(g->chatUser, from, line);
}

static void send_chat(g64_session *g, peer *to, int from, uint8_t id, const uint8_t *text, size_t n) {
    uint8_t m[3 + G64_CHAT_MAX];
    if (n > G64_CHAT_MAX)
        n = G64_CHAT_MAX;
    m[0] = MSG_CHAT;
    m[1] = (uint8_t)from;
    m[2] = id;
    memcpy(m + 3, text, n);
    send_sealed(g, to, m, 3 + n);
}

/* The host's handling of one datagram. */
static void host_inner(g64_session *g, int p, const uint8_t *d, int n, uint32_t now, g64_game_fn on_game, void *user);

static void host_datagram(g64_session *g, const net_addr *from, const uint8_t *d, int n, uint32_t now,
                          g64_game_fn on_game, void *user) {
    int p, free_slot = -1;
    for (p = 1; p < g->players; p++)
        if (g->peers[p].used && g->peers[p].kicked && net_addr_equal(&g->peers[p].addr, from) &&
            (d[0] != MSG_HELLO || g->state == G64_SESSION_WAITING)) {
            /* A removed player's program may have missed the kick. A hello after the start is a newcomer at the
               same address (the relay gives a free place to the next joiner): they get the usual refusal below. */
            uint8_t m = MSG_KICK;
            send_sealed(g, &g->peers[p], &m, 1);
            return;
        }
    for (p = 1; p < g->players; p++)
        if (g->peers[p].used && !g->peers[p].gone && net_addr_equal(&g->peers[p].addr, from))
            break;
    if (p < g->players) {
        peer *q = &g->peers[p];
        if (d[0] == G64_SEAL_TYPE) {
            uint8_t plain[NET_MAX_DATAGRAM];
            int len = g64_seal_unlock(&q->seal, d, (size_t)n, plain, sizeof plain);
            if (len < 1)
                return;
            q->heard = now;
            host_inner(g, p, plain, len, now, on_game, user);
        } else if (d[0] == MSG_HELLO && n >= HELLO_SIZE && memcmp(d + HELLO_KEY, q->theirKey, G64_SEAL_KEY) == 0) {
            /* The welcome or the start was lost: send it again. A hello with another key is someone else at this
               address. They wait until the place is free: a link's key is never replaced. */
            if (g->state == G64_SESSION_WAITING) {
                q->heard = now;
                send_welcome(g, p);
            } else {
                send_start(g, p);
            }
        }
        return;
    }
    if (d[0] != MSG_HELLO)
        return;
    {
        uint8_t refuse[2] = {MSG_REFUSE, 0};
        /* A hello of another protocol has another form, and a program of another build runs another game: both
           get the same answer. */
        if (n < HELLO_SIZE || n > HELLO_SIZE + NAME_MAX || memcmp(d + 1, "G64", 3) != 0 || d[4] != G64_PROTOCOL
            || get32(d + 9) != g->build
            || !valid_color(get32(d + 13)) || !valid_color(get32(d + 17))) {
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
        int len = n - HELLO_SIZE, i;
        uint8_t secret[G64_SEAL_KEY];
        uint8_t welcome[WELCOME_SIZE] = {MSG_WELCOME, (uint8_t)free_slot, (uint8_t)g->players};
        uint8_t refuse[2] = {MSG_REFUSE, 0};
        memset(q, 0, sizeof *q);
        /* The link: a key pair for this player alone, and the keys from it, the joiner's public key and both
           messages. The costly step comes after every cheap check, once for each player who gets a place. */
        if (g64_seal_keypair(secret, q->myKey) != 0) {
            send_bytes(g, from, refuse, sizeof refuse);
            return;
        }
        memcpy(welcome + 3, q->myKey, G64_SEAL_KEY);
        memcpy(q->theirKey, d + HELLO_KEY, G64_SEAL_KEY);
        i = g64_seal_keys(&q->seal, 1, secret, q->theirKey, d, (size_t)n, welcome, sizeof welcome);
        memset(secret, 0, sizeof secret);
        if (i != 0) {
            memset(q, 0, sizeof *q);
            refuse[1] = G64_REFUSE_PROTOCOL;
            send_bytes(g, from, refuse, sizeof refuse);
            return;
        }
        q->sealed = 1;
        q->used = 1;
        q->addr = *from;
        q->heard = now;
        q->glove = get32(d + 13);
        q->ball = get32(d + 17);
        /* The name: printable ASCII only. */
        for (i = 0; i < len; i++)
            q->name[i] = d[HELLO_SIZE + i] >= 32 && d[HELLO_SIZE + i] < 127 ? (char)d[HELLO_SIZE + i] : '?';
        send_bytes(g, from, welcome, sizeof welcome);
    }
}

/* A sealed message from player p, opened. */
static void host_inner(g64_session *g, int p, const uint8_t *d, int n, uint32_t now, g64_game_fn on_game, void *user) {
    if (n < 1)
        return;
    if (d[0] == MSG_CHAT && n >= 3 && n <= 3 + G64_CHAT_MAX) {
        /* A player's line goes to every other player, with its sender's number, which the host knows from the
           link. The sender's repeats are passed on too, and each receiver drops the ones it has. A player
           cannot flood the others: a new line too soon after the last one is dropped, and so are more copies
           of a line than a program sends. */
        peer *c = &g->peers[p];
        int q;
        if (c->chatHave && c->chatId == d[2]) {
            if (c->chatCopies >= CHAT_SENDS)
                return;
            c->chatCopies++;
        } else {
            if (c->chatHave && now - c->chatTime < CHAT_GAP)
                return;
            c->chatHave = 1;
            c->chatId = d[2];
            c->chatCopies = 1;
            c->chatTime = now;
        }
        for (q = 1; q < g->players; q++)
            if (q != p && g->peers[q].used && !g->peers[q].gone)
                send_chat(g, &g->peers[q], p, d[2], d + 3, (size_t)(n - 3));
        chat_arrives(g, p, d[2], d + 3, (size_t)(n - 3));
        return;
    }
    if (d[0] == MSG_GAME && n > 1) {
        g->peers[p].started = 1;
        if (g->state == G64_SESSION_STARTED && on_game)
            on_game(user, p, d + 1, (size_t)(n - 1));
    }
}

/* A joiner's handling of one sealed message from the host, opened. */
static void join_inner(g64_session *g, const uint8_t *d, int n, g64_game_fn on_game, void *user) {
    if (n < 1)
        return;
    switch (d[0]) {
    case MSG_START:
        if (n >= 5 && g->me >= 1 && d[1] == g->players && n == 5 + START_EACH * g->players && d[3] >= 1 &&
            (d[4] & ~G64_FLAGS_KNOWN) == 0 && g->state == G64_SESSION_WAITING) {
            int q, ok = 1;
            for (q = 0; q < g->players; q++)
                ok &= valid_color(get32(d + 5 + START_EACH * q)) && valid_color(get32(d + 9 + START_EACH * q));
            if (!ok)
                break;
            for (q = 0; q < g->players; q++) {
                g->colors[q][0] = get32(d + 5 + START_EACH * q);
                g->colors[q][1] = get32(d + 9 + START_EACH * q);
                clean_text(g->names[q], sizeof g->names[q], d + 13 + START_EACH * q, NAME_MAX);
            }
            g->level = d[2];
            g->deadline = d[3];
            g->flags = d[4];
            g->state = G64_SESSION_STARTED;
        }
        break;
    case MSG_GAME:
        if (n > 1 && g->state == G64_SESSION_STARTED && on_game)
            on_game(user, 0, d + 1, (size_t)(n - 1));
        break;
    case MSG_CHAT:
        /* The host passes on a line with its sender's number. */
        if (n >= 3 && n <= 3 + G64_CHAT_MAX && g->me >= 1 && d[1] < g->players && d[1] != g->me)
            chat_arrives(g, d[1], d[2], d + 3, (size_t)(n - 3));
        break;
    case MSG_KICK:
        if (n == 1)
            fail(g, "the host removed you from the game");
        break;
    default:
        break;
    }
}

/* A joiner's handling of one datagram. */
static void join_datagram(g64_session *g, const net_addr *from, const uint8_t *d, int n, uint32_t now,
                          g64_game_fn on_game, void *user) {
    peer *h = &g->peers[0];
    if (!net_addr_equal(from, &h->addr))
        return;
    switch (d[0]) {
    case MSG_WELCOME:
        /* The first welcome makes the link. A later one changes nothing. */
        if (n == WELCOME_SIZE && d[2] >= 2 && d[2] <= G64_SESSION_MAX_PLAYERS && d[1] >= 1 && d[1] < d[2] &&
            g->state == G64_SESSION_WAITING && !h->sealed && g->helloLen) {
            memcpy(h->theirKey, d + 3, G64_SEAL_KEY);
            if (g64_seal_keys(&h->seal, 0, g->secret, h->theirKey, g->hello, g->helloLen, d, (size_t)n) != 0)
                break;
            memset(g->secret, 0, sizeof g->secret);
            h->sealed = 1;
            h->heard = now;
            g->me = d[1];
            g->players = d[2];
        }
        break;
    case MSG_REFUSE:
        /* A refusal counts only before the welcome: the host never refuses a player it has welcomed. */
        if (g->state == G64_SESSION_WAITING && !h->sealed) {
            static const char *why[] = {"refused", "the game is full", "the host has another ROM",
                                        "the host has another version", "the game has started"};
            fail(g, why[n == 2 && d[1] <= 4 ? d[1] : 0]);
        }
        break;
    case G64_SEAL_TYPE:
        if (h->sealed) {
            uint8_t plain[NET_MAX_DATAGRAM];
            int len = g64_seal_unlock(&h->seal, d, (size_t)n, plain, sizeof plain);
            if (len < 1)
                break;
            h->heard = now;
            join_inner(g, plain, len, on_game, user);
        }
        break;
    default:
        break;
    }
}

void g64_session_test_inner(g64_session *g, int from, const uint8_t *data, size_t len, uint32_t now,
                            g64_game_fn on_game, void *user) {
    if (g->state == G64_SESSION_FAILED || len < 1 || len > NET_MAX_DATAGRAM)
        return;
    if (!g->host)
        join_inner(g, data, (int)len, on_game, user);
    else if (from >= 1 && from < g->players && g->peers[from].used && !g->peers[from].gone)
        host_inner(g, from, data, (int)len, now, on_game, user);
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
            if (q->kicked && g->state == G64_SESSION_WAITING) {
                /* A removed player's place is held for a moment, in which their program is told, then freed. */
                if (q->kickTime == 0)
                    q->kickTime = now ? now : 1;
                else if (now - q->kickTime > KICK_HOLD)
                    memset(q, 0, sizeof *q);
                continue;
            }
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
                    send_sealed(g, &g->peers[p], &m, 1);
        }
        if (g->chatSends > 0 && (g->chatLast == 0 || now - g->chatLast >= CHAT_EVERY)) {
            g->chatLast = now ? now : 1;
            g->chatSends--;
            for (p = 1; p < g->players; p++)
                if (g->peers[p].used && !g->peers[p].gone)
                    send_chat(g, &g->peers[p], 0, g->chatId, (const uint8_t *)g->chatOut, strlen(g->chatOut));
        }
    } else {
        if (g->peers[0].heard == 0 && g->lastSend == 0)
            g->peers[0].heard = now ? now : 1;
        if (g->state == G64_SESSION_WAITING && !g->peers[0].sealed &&
            (g->lastSend == 0 || now - g->lastSend >= RESEND)) {
            g->lastSend = now ? now : 1;
            if (!g->helloLen) {
                /* The hello is made once. Its repeats have the same bytes: the keys are made from them. */
                uint8_t *m = g->hello;
                size_t len = strlen(g->name);
                m[0] = MSG_HELLO;
                memcpy(m + 1, "G64", 3);
                m[4] = G64_PROTOCOL;
                put32(m + 5, g->rom);
                put32(m + 9, g->build);
                put32(m + 13, g->glove);
                put32(m + 17, g->ball);
                memcpy(m + HELLO_KEY, g->peers[0].myKey, G64_SEAL_KEY);
                memcpy(m + HELLO_SIZE, g->name, len);
                g->helloLen = HELLO_SIZE + len;
            }
            send_bytes(g, &g->peers[0].addr, g->hello, g->helloLen);
        }
        if (now - g->lastPing >= PING_EVERY) {
            uint8_t m = MSG_PING;
            g->lastPing = now;
            send_sealed(g, &g->peers[0], &m, 1);
        }
        if (g->chatSends > 0 && g->me >= 1 && (g->chatLast == 0 || now - g->chatLast >= CHAT_EVERY)) {
            g->chatLast = now ? now : 1;
            g->chatSends--;
            send_chat(g, &g->peers[0], g->me, g->chatId, (const uint8_t *)g->chatOut, strlen(g->chatOut));
        }
        if (now - g->peers[0].heard > TIMEOUT)
            fail(g, g->state == G64_SESSION_STARTED ? "the host stopped answering" : "no answer from the host");
    }
    return g->state;
}

void g64_session_send_game(g64_session *g, int to, const uint8_t *data, size_t len) {
    uint8_t m[NET_MAX_DATAGRAM];
    if (len + 1 + G64_SEAL_OVERHEAD > sizeof m || g->state != G64_SESSION_STARTED)
        return;
    if (g->host) {
        if (to < 1 || to >= g->players || !g->peers[to].used || g->peers[to].gone)
            return;
    } else {
        to = 0;
    }
    m[0] = MSG_GAME;
    memcpy(m + 1, data, len);
    send_sealed(g, &g->peers[to], m, len + 1);
}

int g64_session_gone(const g64_session *g, int p) {
    return g->host && p >= 1 && p < g->players && g->peers[p].gone;
}

int g64_session_me(const g64_session *g) { return g->host ? 0 : g->me; }
int g64_session_players(const g64_session *g) { return g->players; }
int g64_session_level(const g64_session *g) { return g->level; }
int g64_session_deadline(const g64_session *g) { return g->deadline; }
void g64_session_set_build(g64_session *g, uint32_t build) { g->build = build; }

int g64_session_flags(const g64_session *g) { return g->flags; }

void g64_session_set_colors(g64_session *g, uint32_t glove, uint32_t ball) {
    if (g->state == G64_SESSION_WAITING && valid_color(glove) && valid_color(ball)) {
        g->glove = glove;
        g->ball = ball;
    }
}

const net_addr *g64_session_peer_addr(const g64_session *g, int p) {
    if (g->me != 0 || p < 1 || p >= g->players || !g->peers[p].used)
        return NULL;
    return &g->peers[p].addr;
}

void g64_session_set_name(g64_session *g, const char *name) {
    clean_text(g->name, sizeof g->name, (const uint8_t *)(name ? name : ""), NAME_MAX);
}

const char *g64_session_name(const g64_session *g, int p) {
    if (p < 0 || p >= g->players)
        return "";
    if (g->me == 0)
        return p == 0 ? g->name : g->peers[p].name;
    return g->names[p];
}

void g64_session_on_chat(g64_session *g, g64_chat_fn fn, void *user) {
    g->chatFn = fn;
    g->chatUser = user;
}

void g64_session_chat(g64_session *g, const char *text) {
    char line[G64_CHAT_MAX + 1];
    clean_text(line, sizeof line, (const uint8_t *)(text ? text : ""), G64_CHAT_MAX);
    if (!line[0] || g->state == G64_SESSION_FAILED)
        return;
    memcpy(g->chatOut, line, sizeof line);
    g->chatId++;
    g->chatSends = CHAT_SENDS;
    g->chatLast = 0;
}

int g64_session_kick(g64_session *g, int p) {
    uint8_t m = MSG_KICK;
    if (g->me != 0 || p < 1 || p >= g->players || !g->peers[p].used || g->peers[p].gone || g->peers[p].kicked)
        return 0;
    send_sealed(g, &g->peers[p], &m, 1);
    g->peers[p].kicked = 1;
    g->peers[p].kickTime = 0;
    /* After the start the player counts as gone for good. Before it the poll frees the place (KICK_HOLD). */
    if (g->state != G64_SESSION_WAITING)
        g->peers[p].gone = 1;
    return 1;
}

void g64_session_colors(const g64_session *g, int p, uint32_t *glove, uint32_t *ball) {
    *glove = *ball = 0;
    if (p < 0 || p >= g->players)
        return;
    if (g->me == 0) {
        *glove = p == 0 ? g->glove : g->peers[p].glove;
        *ball = p == 0 ? g->ball : g->peers[p].ball;
    } else {
        *glove = g->colors[p][0];
        *ball = g->colors[p][1];
    }
}

void g64_session_set_flags(g64_session *g, int flags) {
    if (g->me == 0 && g->state == G64_SESSION_WAITING)
        g->flags = flags & G64_FLAGS_KNOWN;
}
const char *g64_session_error(const g64_session *g) { return g->error; }

int g64_session_joined(const g64_session *g) {
    int n = 1, p;
    if (!g->host)
        return g->me >= 1 ? 2 : 1;
    for (p = 1; p < g->players; p++)
        n += g->peers[p].used && !g->peers[p].kicked;
    return n;
}
