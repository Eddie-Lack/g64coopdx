/* The session in the direct mode (session.h). */
#include "session.h"

#include "seal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hello, the welcome and the refusal travel in the clear: they come before the two ends share a key. Every
   other message travels sealed (seal.h), inside a datagram that starts with G64_SEAL_TYPE. */
enum { MSG_HELLO = 0x40, MSG_WELCOME, MSG_REFUSE, MSG_START, MSG_PING, MSG_GAME, MSG_CHAT, MSG_KICK };
/* The level pack's messages (session.h). 0x48 is the sealed datagram's own type. */
enum { MSG_PACK_OFFER = 0x49, MSG_PACK_WANT, MSG_PACK_PIECE, MSG_PACK_READY };
/* The offer: the type, the id, the three sizes (u16, u32, u32), then the name. */
#define OFFER_SIZE (1 + G64_PACK_ID_SIZE + 10)
/* A piece has up to PACK_PIECE bytes. The host answers one request with up to PACK_BURST pieces, serves a player
   at most every PACK_GAP ms, and sends at most PACK_RATE pieces a second to all players together, PACK_POOL at
   once: a download must not crowd out a running game's own messages. */
#define PACK_PIECE 1024
#define PACK_BURST 8
#define PACK_GAP 20
#define PACK_RATE 256
#define PACK_POOL 32
/* A joiner asks again when no piece came for this long, in ms. After a whole burst it asks at once, but not within
   PACK_GAP ms of its last request, which the host would drop. */
#define PACK_ASK 200
/* How long a place is held for a player who has not got the pack yet, in ms. */
#define PACK_WAIT (5 * 60 * 1000)
/* The most places that players without the pack may hold at once, and how many times the pack's size the host
   sends to one of them before it frees the place: a joiner cannot hold the room or the host's line for long. */
#define PACK_PLACES 4
#define PACK_TIMES 3
/* The hello's bytes before the name: the type, "G64", the protocol, the ROM, the build, two colors, and the
   joiner's public key. */
#define HELLO_KEY 21
#define HELLO_PACK (HELLO_KEY + G64_SEAL_KEY)
#define HELLO_SIZE (HELLO_PACK + G64_PACK_ID_SIZE)
/* The welcome: the type, the player's number, the number of players, and the host's public key for this player. */
#define WELCOME_SIZE (3 + G64_SEAL_KEY)
#define CHAT_SENDS 3
#define CHAT_EVERY 150
#define CHAT_GAP 400
#define KICK_HOLD 3000
/* The host sends every player the list of players again this often, in ms. */
#define ROSTER_EVERY 1000
/* The addresses the host remembers of players it removed. */
#define BANS_MAX 32

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
    /* On a joiner: a sealed message from the host has opened, so the link is the real host's. */
    int opened;
    /* On the host: a sealed message from this peer has opened, so the peer holds the key and the address is its
       own. Only then does the host send it the start, which is long and repeats. */
    int proven;
    /* On the host: this player took the place after the start. The place may be one of a player of the start who
       is gone; its new player is not one of the start's. */
    int late;
    /* On the host: this player does not have the host's level pack yet. They hold a place and are not in the game:
       they are not counted, not sent the start, and their game and chat messages are dropped. since is when
       they came; served is when the host last sent them pieces. */
    int needPack;
    uint32_t packSince, packServed, lastOffer;
    size_t packSent; /* the pack's bytes sent to this peer so far */
    int packOver;    /* the peer asked for more than PACK_TIMES the pack: the place is freed */
    int packAsked; /* the peer has asked for a piece or said it is ready, so it has the offer */
    uint32_t lastStart; /* when the host last sent this peer the start or the list of players */
    /* On the host: when a removed player's program was last told, at most every RESEND ms. */
    uint32_t kickTold;
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
    /* The id of this machine's level pack, or zeros (g64_session_set_pack). */
    uint8_t pack[G64_PACK_ID_SIZE];
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
    /* The players who were in at the start, a bit each, and on a joiner the players who are in now. */
    unsigned startMembers, present;
    /* On the host: every player's colors at the start. A player who was in at the start builds the first game with
       these, also when their start message comes after another player has taken a place. */
    uint32_t startColors[G64_SESSION_MAX_PLAYERS][2];
    /* On the host: how often each place got a new player. */
    uint32_t epoch[G64_SESSION_MAX_PLAYERS];
    /* On the host: the addresses of removed players (not for the relay's places, which the relay bans). */
    net_addr banned[BANS_MAX];
    int bannedCount;
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
    /* On the host: the level pack to send to a joiner who lacks it (g64_session_send_pack). The bytes are the
       caller's. packSend is 1 when the host may send its pack, or plays with none. */
    int packSend;
    const uint8_t *packBytes;
    g64_pack_offer packInfo;
    /* On the host: the pieces it may still send now, and when the count was last filled up. */
    uint32_t packPool, packPoolTime;
    /* On a joiner: the host's offer, what the player decided, and the bytes so far (owned by the session). */
    g64_pack_state packState;
    g64_pack_offer offer;
    uint8_t *packData;
    size_t packHave, packMark;
    uint32_t packAsk, lastReady;
};

static size_t offer_total(const g64_pack_offer *o) { return o->manifestSize + o->levSize + o->scriptSize; }

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

void g64_session_free(g64_session *g) {
    if (g) {
        free(g->packData);
        g64_seal_wipe(g, sizeof *g);
    }
    free(g);
}

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

/* The start's bytes before the players, and for each player: two colors and a name. */
#define START_HEAD 9
#define START_EACH (8 + NAME_MAX)

/* On the host: 1 for a player who is in the game now. */
static int host_present(const g64_session *g, int p) {
    const peer *q = &g->peers[p];
    return p == 0 || (q->used && q->proven && !q->gone && !q->kicked && !q->needPack);
}

/* A name or a chat line from the network or from this machine: printable ASCII, and '?' for anything else. */
static void clean_text(char *out, size_t size, const uint8_t *in, size_t n) {
    size_t i;
    for (i = 0; i + 1 < size && i < n && in[i]; i++)
        out[i] = in[i] >= 32 && in[i] < 127 ? (char)in[i] : '?';
    out[i] = 0;
}

static void send_start(g64_session *g, int p) {
    uint8_t m[START_HEAD + START_EACH * G64_SESSION_MAX_PLAYERS] = {MSG_START, (uint8_t)g->players, (uint8_t)g->level,
                                                                    (uint8_t)g->deadline, (uint8_t)g->flags};
    unsigned present = 0, members = g->startMembers;
    int q;
    for (q = 0; q < g->players; q++)
        present |= (unsigned)host_present(g, q) << q;
    /* A player who came after the start is not one of the start's players, whoever had the place then: their
       program must not build the first game, but ask for one. */
    if (g->peers[p].late)
        members &= ~(1u << p);
    m[5] = (uint8_t)present, m[6] = (uint8_t)(present >> 8);
    m[7] = (uint8_t)members, m[8] = (uint8_t)(members >> 8);
    for (q = 0; q < g->players; q++) {
        uint8_t *e = m + START_HEAD + START_EACH * q;
        const char *name = q == 0 ? g->name : g->peers[q].name;
        /* The first message to a player of the start has the start's colors. The list of players after it has
           the colors of now. */
        int first = !g->peers[p].started && ((members >> p) & 1);
        put32(e, first ? g->startColors[q][0] : q == 0 ? g->glove : g->peers[q].glove);
        put32(e + 4, first ? g->startColors[q][1] : q == 0 ? g->ball : g->peers[q].ball);
        memcpy(e + 8, name, strlen(name) < NAME_MAX ? strlen(name) : NAME_MAX);
    }
    send_sealed(g, &g->peers[p], m, START_HEAD + START_EACH * (size_t)g->players);
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

/* 1 for the address of a player the host removed: the same machine, whatever port it sends from. */
static int is_banned(const g64_session *g, const net_addr *a) {
    int i;
    for (i = 0; i < g->bannedCount; i++)
        if (g->banned[i].family == a->family && memcmp(g->banned[i].ip, a->ip, a->family == 4 ? 4 : 16) == 0)
            return 1;
    return 0;
}

/* The host's handling of one datagram. */
static void host_pack(g64_session *g, int p, const uint8_t *d, int n, uint32_t now);
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
            if (g->peers[p].kickTold == 0 || now - g->peers[p].kickTold >= RESEND) {
                g->peers[p].kickTold = now ? now : 1;
                send_sealed(g, &g->peers[p], &m, 1);
            }
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
            q->proven = 1;
            host_inner(g, p, plain, len, now, on_game, user);
        } else if (d[0] == MSG_HELLO && n >= HELLO_SIZE && memcmp(d + HELLO_KEY, q->theirKey, G64_SEAL_KEY) == 0) {
            /* The welcome was lost: send it again, also after the start, which the joiner cannot open without it.
               The start repeats by itself. A hello with another key is someone else at this address. They wait
               until the place is free: a link's key is never replaced. A hello does not count as heard: anyone
               can repeat one. */
            send_welcome(g, p);
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
        } else if (memcmp(d + HELLO_PACK, g->pack, G64_PACK_ID_SIZE) != 0 && !g->packSend) {
            /* The host has a pack that it may not send: only a joiner who has it already can play. */
            refuse[1] = G64_REFUSE_PACK;
        } else if (is_banned(g, from)) {
            refuse[1] = G64_REFUSE_REMOVED;
        } else {
            /* A free place, or after the start the place of a player who is gone. A game that has started takes
               new players as long as it has places. */
            for (p = 1; p < g->players && free_slot < 0; p++)
                if (!g->peers[p].used)
                    free_slot = p;
            for (p = 1; p < g->players && free_slot < 0 && g->state == G64_SESSION_STARTED; p++)
                if (g->peers[p].gone)
                    free_slot = p;
            if (free_slot < 0)
                refuse[1] = G64_REFUSE_FULL;
            /* Players without the pack hold only so many places at once. */
            if (memcmp(d + HELLO_PACK, g->pack, G64_PACK_ID_SIZE) != 0) {
                int waiting = 0;
                for (p = 1; p < g->players; p++)
                    waiting += g->peers[p].used && !g->peers[p].gone && g->peers[p].needPack;
                if (waiting >= PACK_PLACES)
                    refuse[1] = G64_REFUSE_FULL;
            }
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
        g64_seal_wipe(secret, sizeof secret);
        if (i != 0) {
            memset(q, 0, sizeof *q);
            refuse[1] = G64_REFUSE_PROTOCOL;
            send_bytes(g, from, refuse, sizeof refuse);
            return;
        }
        /* A former player's record at this address (through the relay: this place in the room) ends here. */
        for (i = 1; i < g->players; i++)
            if (i != free_slot && g->peers[i].used && net_addr_equal(&g->peers[i].addr, from)) {
                int wasGone = g->peers[i].gone;
                g64_seal_wipe(&g->peers[i], sizeof g->peers[i]);
                /* After the start the place stays one whose player is gone, until someone takes it. */
                g->peers[i].used = g->peers[i].gone = wasGone;
            }
        g->epoch[free_slot]++;
        q->late = g->state == G64_SESSION_STARTED;
        /* A joiner with another pack, or with none, gets the place and the host's offer behind the seal. */
        q->needPack = memcmp(d + HELLO_PACK, g->pack, G64_PACK_ID_SIZE) != 0;
        q->packSince = now;
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

/* The host's offer to a player who lacks its pack: the pack's id, sizes and name, or zeros for no pack. */
static void send_offer(g64_session *g, peer *q) {
    uint8_t m[OFFER_SIZE + G64_PACK_NAME_MAX];
    size_t len = strlen(g->packInfo.name);
    m[0] = MSG_PACK_OFFER;
    memcpy(m + 1, g->pack, G64_PACK_ID_SIZE);
    m[17] = (uint8_t)g->packInfo.manifestSize, m[18] = (uint8_t)(g->packInfo.manifestSize >> 8);
    put32(m + 19, (uint32_t)g->packInfo.levSize);
    put32(m + 23, (uint32_t)g->packInfo.scriptSize);
    memcpy(m + OFFER_SIZE, g->packInfo.name, len);
    send_sealed(g, q, m, OFFER_SIZE + len);
}

/* A message about the level pack from player p: a request for pieces, or the word that they have the pack. */
static void host_pack(g64_session *g, int p, const uint8_t *d, int n, uint32_t now) {
    peer *q = &g->peers[p];
    if (n < 1 + G64_PACK_ID_SIZE || memcmp(d + 1, g->pack, G64_PACK_ID_SIZE) != 0)
        return;
    if (d[0] == MSG_PACK_READY && n == 1 + G64_PACK_ID_SIZE) {
        /* The player has the host's pack now and is in the game from here on. The answer is the same message. */
        if (q->needPack) {
            q->needPack = 0;
            q->late = g->state == G64_SESSION_STARTED;
        }
        q->packAsked = 1;
        send_sealed(g, q, d, (size_t)n);
        return;
    }
    if (d[0] == MSG_PACK_WANT && n == 5 + G64_PACK_ID_SIZE && q->needPack && g->packBytes) {
        size_t total = offer_total(&g->packInfo), at = get32(d + 1 + G64_PACK_ID_SIZE);
        int i;
        q->packAsked = 1;
        if (at >= total || (q->packServed && now - q->packServed < PACK_GAP))
            return;
        if (q->packSent > total * PACK_TIMES) {
            q->packOver = 1;
            return;
        }
        /* The pieces all players may get: PACK_RATE a second, and never more than PACK_POOL saved up. */
        {
            uint32_t passed = now - g->packPoolTime;
            uint32_t add = passed >= 1000 ? PACK_POOL : passed * PACK_RATE / 1000;
            if (add > 0 || g->packPoolTime == 0) {
                g->packPool = g->packPoolTime == 0 || g->packPool + add > PACK_POOL ? PACK_POOL : g->packPool + add;
                g->packPoolTime = now ? now : 1;
            }
        }
        q->packServed = now ? now : 1;
        for (i = 0; i < PACK_BURST && at < total && g->packPool > 0; i++) {
            uint8_t m[5 + PACK_PIECE];
            size_t len = total - at < PACK_PIECE ? total - at : PACK_PIECE;
            m[0] = MSG_PACK_PIECE;
            put32(m + 1, (uint32_t)at);
            memcpy(m + 5, g->packBytes + at, len);
            send_sealed(g, q, m, 5 + len);
            q->packSent += len;
            at += len;
            g->packPool--;
        }
    }
}

/* A sealed message from player p, opened. */
static void host_inner(g64_session *g, int p, const uint8_t *d, int n, uint32_t now, g64_game_fn on_game, void *user) {
    if (n < 1)
        return;
    if (d[0] == MSG_PACK_WANT || d[0] == MSG_PACK_READY) {
        host_pack(g, p, d, n, now);
        return;
    }
    /* A player without the pack is not in the game yet. */
    if (g->peers[p].needPack)
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
            if (q != p && g->peers[q].used && !g->peers[q].gone && !g->peers[q].needPack)
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
        /* The start, and after it the same message again and again as the list of players: who is in now, with
           their names and colors. */
        /* No start counts while the host's offer is open: this machine does not have the host's pack yet. */
        if (g->packState != G64_PACK_OFFERED && g->packState != G64_PACK_LOADING && g->packState != G64_PACK_LOADED &&
            n >= START_HEAD && g->me >= 1 && d[1] == g->players && n == START_HEAD + START_EACH * g->players &&
            d[3] >= 1 && (d[4] & ~G64_FLAGS_KNOWN) == 0) {
            unsigned all = g->players >= 16 ? 0xFFFFu : (1u << g->players) - 1u;
            unsigned present = (unsigned)d[5] | (unsigned)d[6] << 8, members = (unsigned)d[7] | (unsigned)d[8] << 8;
            int q, ok = 1;
            for (q = 0; q < g->players; q++)
                ok &= valid_color(get32(d + START_HEAD + START_EACH * q)) &&
                      valid_color(get32(d + START_HEAD + 4 + START_EACH * q));
            if (!ok || (present & ~all) != 0 || (members & ~all) != 0 || !(present & 1) || !(members & 1))
                break;
            for (q = 0; q < g->players; q++) {
                g->colors[q][0] = get32(d + START_HEAD + START_EACH * q);
                g->colors[q][1] = get32(d + START_HEAD + 4 + START_EACH * q);
                clean_text(g->names[q], sizeof g->names[q], d + START_HEAD + 8 + START_EACH * q, NAME_MAX);
            }
            g->present = present;
            if (g->state == G64_SESSION_WAITING) {
                g->startMembers = members;
                for (q = 0; q < g->players; q++) {
                    g->startColors[q][0] = g->colors[q][0];
                    g->startColors[q][1] = g->colors[q][1];
                }
                g->level = d[2];
                g->deadline = d[3];
                g->flags = d[4];
                g->state = G64_SESSION_STARTED;
            }
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
    case MSG_PACK_OFFER:
        /* The host plays with another pack than this machine's, or with none. Only the first offer counts: a host
           does not change its pack, and a second offer must not move a download's bounds. */
        if (n >= OFFER_SIZE && n <= OFFER_SIZE + G64_PACK_NAME_MAX && g->state == G64_SESSION_WAITING && g->me >= 1) {
            g64_pack_offer o;
            int i, none = 1;
            memset(&o, 0, sizeof o);
            memcpy(o.id, d + 1, G64_PACK_ID_SIZE);
            o.manifestSize = (size_t)d[17] | (size_t)d[18] << 8;
            o.levSize = get32(d + 19);
            o.scriptSize = get32(d + 23);
            clean_text(o.name, sizeof o.name, d + OFFER_SIZE, (size_t)(n - OFFER_SIZE));
            for (i = 0; i < G64_PACK_ID_SIZE; i++)
                none &= o.id[i] == 0;
            if (none ? (offer_total(&o) != 0 || n != OFFER_SIZE)
                     : (o.manifestSize < 1 || o.manifestSize > G64_PACK_MANIFEST_LIMIT || o.levSize < 8 ||
                        o.levSize > G64_PACK_LEVEL_LIMIT || o.scriptSize > G64_PACK_SCRIPT_LIMIT || !o.name[0]))
                break;
            /* An offer of this machine's own pack is not one: the hello said which pack it has. */
            if (memcmp(o.id, g->pack, G64_PACK_ID_SIZE) == 0 && g->packState == G64_PACK_NONE)
                break;
            if (g->packState == G64_PACK_NONE) {
                g->offer = o;
                g->packState = G64_PACK_OFFERED;
            } else if (memcmp(o.id, g->offer.id, sizeof o.id) != 0 || strcmp(o.name, g->offer.name) != 0 ||
                       o.manifestSize != g->offer.manifestSize || o.levSize != g->offer.levSize ||
                       o.scriptSize != g->offer.scriptSize) {
                fail(g, "the host changed its level pack");
            }
        }
        break;
    case MSG_PACK_PIECE:
        /* The next bytes of the pack, in order only: a piece that is not the next one is dropped and asked for
           again. Nothing is kept before the player has said yes. */
        if (n > 5 && n <= 5 + PACK_PIECE && g->packState == G64_PACK_LOADING && g->packData) {
            size_t total = offer_total(&g->offer), at = get32(d + 1), len = (size_t)(n - 5);
            if (at != g->packHave || len > total - g->packHave)
                break;
            memcpy(g->packData + at, d + 5, len);
            g->packHave += len;
            if (g->packHave == total)
                g->packState = G64_PACK_LOADED;
        }
        break;
    case MSG_PACK_READY:
        /* The host's answer: it counts this player in. */
        if (n == 1 + G64_PACK_ID_SIZE && g->packState == G64_PACK_READY &&
            memcmp(d + 1, g->pack, G64_PACK_ID_SIZE) == 0)
            g->packState = G64_PACK_DONE;
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
        /* A welcome makes the link. Until a sealed message from the host has opened, a welcome with another key
           makes it anew: a forged welcome that came first must not shut the real host out. A welcome with the
           same key changes nothing. */
        if (n == WELCOME_SIZE && d[2] >= 2 && d[2] <= G64_SESSION_MAX_PLAYERS && d[1] >= 1 && d[1] < d[2] &&
            g->state == G64_SESSION_WAITING && !h->opened && g->helloLen &&
            (!h->sealed || memcmp(h->theirKey, d + 3, G64_SEAL_KEY) != 0)) {
            g64_seal fresh;
            if (g64_seal_keys(&fresh, 0, g->secret, d + 3, g->hello, g->helloLen, d, (size_t)n) != 0)
                break;
            /* The counter goes on from where it was: if an earlier key comes back, no counter is used twice
               with it. */
            if (h->sealed)
                fresh.sent = h->seal.sent;
            h->seal = fresh;
            g64_seal_wipe(&fresh, sizeof fresh);
            memcpy(h->theirKey, d + 3, G64_SEAL_KEY);
            h->sealed = 1;
            h->heard = now;
            /* A ping goes out at once: the host sends the start only to a player whose sealed message opened. */
            g->lastPing = now - PING_EVERY;
            g->me = d[1];
            g->players = d[2];
        }
        break;
    case MSG_REFUSE:
        /* A refusal counts only before the welcome: the host never refuses a player it has welcomed. */
        if (g->state == G64_SESSION_WAITING && !h->sealed) {
            static const char *why[] = {"refused",
                                        "the game is full",
                                        "the host has another ROM",
                                        "the host has another version",
                                        "the game has started",
                                        "the host removed you from this game",
                                        "the host plays with another level pack, or with none"};
            fail(g, why[n == 2 && d[1] <= 6 ? d[1] : 0]);
        }
        break;
    case G64_SEAL_TYPE:
        if (h->sealed) {
            uint8_t plain[NET_MAX_DATAGRAM];
            int len = g64_seal_unlock(&h->seal, d, (size_t)n, plain, sizeof plain);
            if (len < 1)
                break;
            if (!h->opened) {
                /* The link is the real host's: the secret key has done its work. */
                h->opened = 1;
                g64_seal_wipe(g->secret, sizeof g->secret);
            }
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
    /* As a datagram that opened would, it counts as heard from its sender. */
    if (!g->host) {
        g->peers[0].heard = now;
        join_inner(g, data, (int)len, on_game, user);
    } else if (from >= 1 && from < g->players && g->peers[from].used && !g->peers[from].gone) {
        g->peers[from].heard = now;
        host_inner(g, from, data, (int)len, now, on_game, user);
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
            /* A player who lacks the pack gets the offer until they answer it. Their place is held only so long. */
            if (q->needPack && q->proven && !q->packAsked && (q->lastOffer == 0 || now - q->lastOffer >= RESEND)) {
                q->lastOffer = now ? now : 1;
                send_offer(g, q);
            }
            if (now - q->heard > TIMEOUT || (q->needPack && (q->packOver || now - q->packSince > PACK_WAIT))) {
                if (g->state == G64_SESSION_STARTED) {
                    /* The game goes on without them: the host gives their glove no input from now on. */
                    q->gone = 1;
                    continue;
                }
                q->used = 0;
                continue;
            }
            if (q->needPack)
                continue;
            joined++;
        }
        if (g->state == G64_SESSION_WAITING && joined == g->players)
            g64_session_begin(g);
        /* The start goes to each player whose sealed message has opened, every RESEND ms until they answer with a
           game message. After that the same message is the list of players, sent every ROSTER_EVERY ms. */
        for (p = 1; p < g->players && g->state == G64_SESSION_STARTED; p++) {
            peer *q = &g->peers[p];
            if (!q->used || q->gone || !q->proven || q->needPack)
                continue;
            if (q->lastStart == 0 || now - q->lastStart >= (uint32_t)(q->started ? ROSTER_EVERY : RESEND)) {
                q->lastStart = now ? now : 1;
                send_start(g, p);
            }
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
                if (g->peers[p].used && !g->peers[p].gone && !g->peers[p].needPack)
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
                memcpy(m + HELLO_PACK, g->pack, G64_PACK_ID_SIZE);
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
        if (g->packState == G64_PACK_LOADING) {
            /* Ask for the next pieces: at once after the last burst came whole, and again after a pause. */
            if (g->packAsk == 0 || now - g->packAsk >= PACK_ASK ||
                (g->packHave >= g->packMark + (size_t)PACK_BURST * PACK_PIECE && now - g->packAsk >= PACK_GAP + 5)) {
                uint8_t m[5 + G64_PACK_ID_SIZE] = {MSG_PACK_WANT};
                memcpy(m + 1, g->offer.id, G64_PACK_ID_SIZE);
                put32(m + 1 + G64_PACK_ID_SIZE, (uint32_t)g->packHave);
                g->packAsk = now ? now : 1;
                g->packMark = g->packHave;
                send_sealed(g, &g->peers[0], m, sizeof m);
            }
        } else if (g->packState == G64_PACK_READY && (g->lastReady == 0 || now - g->lastReady >= RESEND)) {
            uint8_t m[1 + G64_PACK_ID_SIZE] = {MSG_PACK_READY};
            memcpy(m + 1, g->pack, G64_PACK_ID_SIZE);
            g->lastReady = now ? now : 1;
            send_sealed(g, &g->peers[0], m, sizeof m);
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
        if (to < 1 || to >= g->players || !g->peers[to].used || g->peers[to].gone || g->peers[to].needPack)
            return;
    } else {
        to = 0;
    }
    m[0] = MSG_GAME;
    memcpy(m + 1, data, len);
    send_sealed(g, &g->peers[to], m, len + 1);
}

void g64_session_begin(g64_session *g) {
    int p;
    if (!g->host || g->state != G64_SESSION_WAITING)
        return;
    g->startMembers = 1;
    g->startColors[0][0] = g->glove;
    g->startColors[0][1] = g->ball;
    for (p = 1; p < g->players; p++)
        if (g->peers[p].used && !g->peers[p].kicked && !g->peers[p].needPack) {
            g->startMembers |= 1u << p;
            g->startColors[p][0] = g->peers[p].glove;
            g->startColors[p][1] = g->peers[p].ball;
        }
    g->state = G64_SESSION_STARTED;
}

unsigned g64_session_start_members(const g64_session *g) { return g->startMembers; }

int g64_session_present(const g64_session *g, int p) {
    if (p < 0 || p >= g->players || g->state != G64_SESSION_STARTED)
        return 0;
    return g->host ? host_present(g, p) : (int)((g->present >> p) & 1);
}

void g64_session_start_colors(const g64_session *g, int p, uint32_t *glove, uint32_t *ball) {
    *glove = *ball = 0;
    if (p < 0 || p >= g->players || !((g->startMembers >> p) & 1))
        return;
    /* A joiner of the start has them from its first start message. */
    *glove = g->startColors[p][0];
    *ball = g->startColors[p][1];
}

uint32_t g64_session_epoch(const g64_session *g, int p) {
    return g->host && p >= 1 && p < g->players ? g->epoch[p] : 0;
}

uint32_t g64_session_host_silent(const g64_session *g, uint32_t now) {
    return g->host || !g->peers[0].heard ? 0 : now - g->peers[0].heard;
}

int g64_session_gone(const g64_session *g, int p) {
    return g->host && p >= 1 && p < g->players && g->peers[p].gone;
}

int g64_session_me(const g64_session *g) { return g->host ? 0 : g->me; }
int g64_session_players(const g64_session *g) { return g->players; }
int g64_session_level(const g64_session *g) { return g->level; }
int g64_session_deadline(const g64_session *g) { return g->deadline; }
void g64_session_set_build(g64_session *g, uint32_t build) { g->build = build; }
void g64_session_set_pack(g64_session *g, const uint8_t *id) {
    if (id)
        memcpy(g->pack, id, G64_PACK_ID_SIZE);
    else
        memset(g->pack, 0, G64_PACK_ID_SIZE);
}

void g64_session_send_pack(g64_session *g, const g64_pack_offer *info, const uint8_t *bytes) {
    if (!g->host)
        return;
    memset(&g->packInfo, 0, sizeof g->packInfo);
    g->packBytes = NULL;
    g->packSend = 1;
    if (info && bytes && info->manifestSize >= 1 && info->manifestSize <= G64_PACK_MANIFEST_LIMIT &&
        info->levSize >= 8 && info->levSize <= G64_PACK_LEVEL_LIMIT && info->scriptSize <= G64_PACK_SCRIPT_LIMIT) {
        g->packInfo = *info;
        clean_text(g->packInfo.name, sizeof g->packInfo.name, (const uint8_t *)info->name, G64_PACK_NAME_MAX);
        memcpy(g->packInfo.id, g->pack, G64_PACK_ID_SIZE);
        g->packBytes = bytes;
    }
}

g64_pack_state g64_session_pack_state(const g64_session *g) { return g->packState; }
const g64_pack_offer *g64_session_pack_offer(const g64_session *g) { return &g->offer; }
size_t g64_session_pack_have(const g64_session *g) { return g->packHave; }
const uint8_t *g64_session_pack_bytes(const g64_session *g) {
    return g->packState == G64_PACK_LOADED ? g->packData : NULL;
}

int g64_session_pack_accept(g64_session *g) {
    size_t total = offer_total(&g->offer);
    if (g->host || g->packState != G64_PACK_OFFERED || total == 0)
        return -1;
    /* The offer's sizes were held to the pack's limits when it came. */
    g->packData = malloc(total);
    if (!g->packData)
        return -1;
    g->packHave = g->packMark = 0;
    g->packAsk = 0;
    g->packState = G64_PACK_LOADING;
    return 0;
}

void g64_session_pack_ready(g64_session *g) {
    if (g->host || (g->packState != G64_PACK_OFFERED && g->packState != G64_PACK_LOADED))
        return;
    free(g->packData);
    g->packData = NULL;
    memcpy(g->pack, g->offer.id, G64_PACK_ID_SIZE);
    g->lastReady = 0;
    g->packState = G64_PACK_READY;
}

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
    /* The address stays out of this game. Through the relay an address is a place in the room, which the next
       joiner may get: there the relay keeps the removed player out (net_relay_ban). */
    if (g->peers[p].addr.family != NET_FAMILY_RELAY && !is_banned(g, &g->peers[p].addr) && g->bannedCount < BANS_MAX)
        g->banned[g->bannedCount++] = g->peers[p].addr;
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
    /* After the start a player counts once a sealed message from them has opened: a hello alone may be anyone's. */
    for (p = 1; p < g->players; p++)
        n += g->peers[p].used && !g->peers[p].kicked && !g->peers[p].gone && !g->peers[p].needPack &&
             (g->state != G64_SESSION_STARTED || g->peers[p].proven);
    return n;
}
