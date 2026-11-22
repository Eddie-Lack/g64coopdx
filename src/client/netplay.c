/* The online mode of the window program (netplay.h). */
#include "netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
static uint32_t now_ms(void) { return (uint32_t)GetTickCount(); }
#else
#include <time.h>
static void sleep_ms(int ms) {
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}
static uint32_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
#endif

#include "../net/udp.h"
#include "../rollback/engine_game.h"
#include "../rollback/rollback.h"
#include "../session/session.h"
#include "instances.h"
#include "sound.h"
#include "soundout.h"
#include "../../weemitt/src/port/soundtable.h"

/* weemitt's src/port/frame.h: the player's ROM. */
extern unsigned char *port_rom;
extern unsigned int port_rom_size;

/* weemitt's src/game/g64players.c and src/port/frame.c. */
int G64PlayerPosition(int p, float *x, float *y, float *z);
void G64SwapPlayer(int p);
void G64TipView(int p);
void G64NudgePlayer(int p, float dx, float dy, float dz);
int port_game_draw_view(void *tasks, int max);
/* weemitt's log (src/port/log.c): the online program has no console on Windows, so its lines go to a file too. */
void plog_printf(const char *fmt, ...);
typedef struct {
    int level, frame, player, type;
} G64TestExit;
#define TEST_EXITS 8 /* weemitt's G64_TEST_EXITS */
extern G64TestExit g64TestExits[TEST_EXITS];

/* The game starts at the first hub level. */
#define START_LEVEL 0
/* The host confirms a frame this many frames after its own clock: 5 frames are 250 ms. */
#define DEADLINE 5
/* A client keeps 32 savestates and predicts at most that far. */
#define WINDOW 32
/* How long the host waits for players, and a joiner for the start. */
#define CONNECT_TIMEOUT_MS (10 * 60 * 1000)

static net_socket *sock;
static g64_session *session;
static int active, failed, me, players;
/* The instances (instances.c): the host's part on the host, and every machine's own player. */
static g64_ihost *ihost;
static g64_iclient *iclient;
static void *boot; /* the state right after power-on: every level is built from it */
static char status[160];
/* The local player's view: it is drawn from the local game with that player swapped into the globals, from a saved
 * state that is loaded again at the next frame. */
static Engine *engine;
static void *viewState;
static int viewPending;
static uint32_t lastPrinted;
static int dropped[G64_SESSION_MAX_PLAYERS];

int netplay_active(void) { return active; }
int netplay_failed(void) { return failed; }

/* Connecting, step by step (netplay_start, then netplay_poll until it is not 0). */
enum { C_IDLE, C_RELAY, C_SESSION, C_STARTED, C_FAILED };
static int cstate = C_IDLE;
static int chost; /* this machine hosts */
static char cerror[320], cstatus[160];
static uint32_t cstart, romHash;
static int cwanted, cdeadline, chostPort, cjoined;
static char croom[NET_ROOM_CODE + 1], crelay[256], cjoin[256];
static int clevel;

static int fail_connect(const char *why) {
    snprintf(cerror, sizeof cerror, "%s", why);
    cstate = C_FAILED;
    return -1;
}

/* The host's options for the session (session.h, G64_FLAG_), set before it hosts. */
static int hostFlags;

void netplay_set_normal_ball(int on) { hostFlags = on ? hostFlags | G64_FLAG_NORMAL_BALL : hostFlags & ~G64_FLAG_NORMAL_BALL; }

int netplay_normal_ball(void) { return session && (g64_session_flags(session) & G64_FLAG_NORMAL_BALL) != 0; }

void netplay_set_any_ball(int on) { hostFlags = on ? hostFlags | G64_FLAG_ANY_BALL : hostFlags & ~G64_FLAG_ANY_BALL; }

int netplay_any_ball(void) { return session && (g64_session_flags(session) & G64_FLAG_ANY_BALL) != 0; }

/* This player's colors (netplay_set_colors), and weemitt's table of every player's (g64players.c). */
static uint32_t myGlove, myBall;
extern unsigned g64PlayerColor[16][2];

void netplay_set_colors(uint32_t glove, uint32_t ball) {
    myGlove = glove & 0x1FFFFFFu;
    myBall = ball & 0x1FFFFFFu;
}

void netplay_apply_colors(void) {
    int p;
    for (p = 0; p < 16; p++)
        g64PlayerColor[p][0] = g64PlayerColor[p][1] = 0;
    for (p = 0; session && p < players && p < 16; p++) {
        uint32_t g, b;
        g64_session_colors(session, p, &g, &b);
        g64PlayerColor[p][0] = g;
        g64PlayerColor[p][1] = b;
    }
}

/* The level the host starts the game in: the first hub section, or G64_START_LEVEL for tests. */
static int start_level(void) {
    const char *v = getenv("G64_START_LEVEL");
    int level = v ? atoi(v) : START_LEVEL;
    return level >= 0 && level < G64_NUM_LEVELS ? level : START_LEVEL;
}

/* The host's choice to put its room on the relay's list, with a title (netplay_set_public), and when the room was
 * last announced. The list of rooms that the start screen shows comes from a socket of its own (netplay_rooms_). */
static int publicRoom;
static char publicTitle[NET_ROOM_TITLE + 1];
static uint32_t announced;
static net_socket *listSock;
static net_addr listRelays[NET_RELAY_ADDRS];
static int listRelayCount, listCount = -1;
static net_room listRooms[12];
static uint32_t listAsked;

/* This player's name (netplay_set_name), and the chat's latest lines with the time each came. Names and chat are
 * the players' own words: they stay out of the game's state and out of its inputs. */
static char myName[G64_NAME_MAX + 1] = "Player";
/* 1 after netplay_set_name: the start screen then keeps the given name and does not read the saved one. */
static int nameChosen;
/* The players whose removal the chat has already said (netplay_kick). */
static int kickNoted[G64_SESSION_MAX_PLAYERS];
static int banPeer, banSends;
static uint32_t banTime, listTried;
static void send_ban(void);
#define CHAT_LINES 32
#define CHAT_TEXT (G64_NAME_MAX + G64_CHAT_MAX + 4)
static char chatLines[CHAT_LINES][CHAT_TEXT];
static uint32_t chatTimes[CHAT_LINES];
static int chatCount;

void netplay_set_name(const char *name) {
    size_t i;
    for (i = 0; i < G64_NAME_MAX && name && name[i]; i++)
        myName[i] = name[i] >= 32 && name[i] < 127 ? name[i] : '?';
    myName[i] = 0;
    if (!myName[0])
        snprintf(myName, sizeof myName, "Player");
    nameChosen = 1;
}

int netplay_name_chosen(void) { return nameChosen; }

const char *netplay_name(void) { return myName; }

/* Adds a line to the chat's list: a player's words with their name, or a notice (from < 0). */
static void chat_add(int from, const char *text) {
    char *line;
    if (chatCount == CHAT_LINES) {
        memmove(chatLines[0], chatLines[1], sizeof chatLines[0] * (CHAT_LINES - 1));
        memmove(&chatTimes[0], &chatTimes[1], sizeof chatTimes[0] * (CHAT_LINES - 1));
        chatCount--;
    }
    line = chatLines[chatCount];
    if (from < 0) {
        snprintf(line, CHAT_TEXT, "* %s", text);
    } else {
        const char *name = session ? g64_session_name(session, from) : "";
        if (name[0])
            snprintf(line, CHAT_TEXT, "%s: %s", name, text);
        else
            snprintf(line, CHAT_TEXT, "Player %d: %s", from + 1, text);
    }
    chatTimes[chatCount++] = now_ms();
    plog_printf("g64coopdx: chat: %s", line);
}

static void on_chat(void *user, int from, const char *text) {
    (void)user;
    chat_add(from, text);
}

void netplay_chat_send(const char *text) {
    char line[G64_CHAT_MAX + 1];
    size_t i;
    if (!session || failed || !text || !text[0] || g64_session_me(session) < 0)
        return;
    /* The player sees their own line as the others will: with the same characters replaced. */
    for (i = 0; i < G64_CHAT_MAX && text[i]; i++)
        line[i] = text[i] >= 32 && text[i] < 127 ? text[i] : '?';
    line[i] = 0;
    g64_session_chat(session, line);
    chat_add(g64_session_me(session), line);
}

int netplay_chat_count(void) { return chatCount; }
const char *netplay_chat_line(int i) { return i >= 0 && i < chatCount ? chatLines[i] : ""; }
unsigned netplay_chat_age(int i) { return i >= 0 && i < chatCount ? now_ms() - chatTimes[i] : 0xFFFFFFFFu; }

static uint32_t build_hash(void);

void netplay_set_public(int on, const char *title) {
    size_t i;
    publicRoom = on != 0;
    for (i = 0; i < NET_ROOM_TITLE && title && title[i]; i++)
        publicTitle[i] = title[i] >= 32 && title[i] < 127 ? title[i] : '?';
    publicTitle[i] = 0;
}

int netplay_public(void) { return publicRoom; }
const char *netplay_public_title(void) { return publicTitle; }

/* The host tells the relay about its room: every two seconds while it waits, and once more when the game starts,
   which takes the room off the list. */
static void announce_room(int open) {
    uint32_t t = now_ms();
    if (!publicRoom || !sock || !session || !chost || !crelay[0])
        return;
    if (open && announced && t - announced < 2000)
        return;
    announced = t ? t : 1;
    net_relay_announce(sock, build_hash(), cwanted, g64_session_joined(session), g64_session_flags(session), open,
                       publicTitle[0] ? publicTitle : myName);
}

/* The list of rooms for the start screen. netplay_rooms_refresh asks the relay again (at most once a second) and
   reads what has come; netplay_rooms_count is -1 until the relay has answered. */
void netplay_rooms_refresh(const char *relay) {
    uint8_t d[NET_MAX_DATAGRAM];
    net_addr from;
    char err[160];
    uint32_t t = now_ms();
    int n;
    if (!relay || !relay[0])
        return;
    if (!listSock) {
        /* A failed try (no network, an unknown name) is made again after three seconds. */
        if (listTried && t - listTried < 3000)
            return;
        listTried = t ? t : 1;
        if (net_init() != 0)
            return;
        listSock = net_open(0, err, sizeof err);
        listRelayCount = listSock ? net_resolve_all(relay, 27065, listRelays, NET_RELAY_ADDRS, err, sizeof err) : -1;
        if (listSock && listRelayCount < 1) {
            net_close(listSock);
            listSock = NULL;
        }
    }
    if (!listSock)
        return;
    while ((n = net_recv(listSock, &from, d, sizeof d)) >= 0) {
        int i, known = 0;
        /* Only the relay's own answer counts. */
        for (i = 0; i < listRelayCount; i++)
            known |= net_addr_equal(&from, &listRelays[i]);
        if (known) {
            /* A list that does not parse leaves the last good one as it was. */
            net_room fresh[sizeof listRooms / sizeof listRooms[0]];
            int got = net_rooms_parse(d, n, fresh, (int)(sizeof fresh / sizeof fresh[0]));
            if (got >= 0) {
                memcpy(listRooms, fresh, sizeof listRooms);
                listCount = got;
            }
        }
    }
    if (listAsked == 0 || t - listAsked >= 1000) {
        listAsked = t ? t : 1;
        net_rooms_ask(listSock, listRelays, listRelayCount, build_hash());
    }
}

int netplay_rooms_count(void) { return listCount; }
const net_room *netplay_room(int i) { return i >= 0 && i < listCount ? &listRooms[i] : NULL; }

void netplay_rooms_close(void) {
    if (listSock)
        net_close(listSock);
    listSock = NULL;
    listCount = -1;
    listAsked = 0;
    listTried = 0;
}

/* The players of the session, for the list in the menu. Before the start only the host knows them. */
int netplay_me(void) { return session ? g64_session_me(session) : -1; }
const char *netplay_player_name(int p) { return session ? g64_session_name(session, p) : ""; }
int netplay_player_gone(int p) {
    return session && g64_session_me(session) == 0 && p > 0 && g64_session_gone(session, p);
}

/* The ban goes to the relay five times, half a second apart: one lost packet does not lose it. The relay holds the
   banned player's place for ten seconds, so the repeats cannot reach a newcomer. */
static void send_ban(void) {
    uint32_t t = now_ms();
    if (!banSends || !sock || (banTime && t - banTime < 500))
        return;
    banTime = t ? t : 1;
    banSends--;
    net_relay_ban(sock, banPeer);
}

/* The host removes a player: their program is told, and the game goes on without them. */
void netplay_kick(int p) {
    char note[64];
    const net_addr *a;
    int place = 0;
    if (!session || g64_session_me(session) != 0 || p < 1 || p >= g64_session_players(session))
        return;
    snprintf(note, sizeof note, "%s was removed by the host",
             g64_session_name(session, p)[0] ? g64_session_name(session, p) : "A player");
    a = g64_session_peer_addr(session, p);
    if (a && a->family == NET_FAMILY_RELAY)
        place = a->ip[0];
    /* A second removal of the same place does nothing: it would reach whoever holds the place next. */
    if (!g64_session_kick(session, p))
        return;
    /* Through the relay the removal is also a ban: the relay refuses that player's address for this room. Only
       the relay sees addresses. The kick message went first; the relay still passes it on. */
    if (place && sock) {
        banPeer = place;
        banSends = 5;
        banTime = 0;
        send_ban();
    }
    kickNoted[p] = 1;
    chat_add(-1, note);
}

/* This program's build id: the commit it was built from (weemitt/studio.mk writes g64build.h), or "dev" for a build
 * without one. Every change to the game's code plays another game, so players of different builds cannot play
 * together: the host refuses a joiner whose build differs. The Windows and Linux programs of one commit have the same
 * id. */
#ifdef G64_BUILD_HEADER
#include "g64build.h"
#endif
#ifndef G64_BUILD_ID
#define G64_BUILD_ID "dev"
#endif

const char *netplay_build(void) { return G64_BUILD_ID; }

static uint32_t build_hash(void) { return g64_rom_hash((const uint8_t *)G64_BUILD_ID, strlen(G64_BUILD_ID)); }

/* Makes the session once the socket is ready: at once in the direct mode, after the relay's answer through it. */
static int make_session(void) {
    if (crelay[0] && !chost) {
        net_addr hostAddr = net_relay_player(0);
        session = g64_session_join(sock, &hostAddr, romHash, myName);
    } else if (crelay[0]) {
        session = g64_session_host(sock, cwanted, romHash, start_level(), cdeadline > 0 ? cdeadline : DEADLINE);
    } else if (cjoin[0]) {
        net_addr to;
        char err[160];
        if (net_resolve(cjoin, G64_DEFAULT_PORT, &to, err, sizeof err) != 0)
            return fail_connect(err);
        session = g64_session_join(sock, &to, romHash, myName);
    } else {
        session = g64_session_host(sock, cwanted, romHash, start_level(), cdeadline > 0 ? cdeadline : DEADLINE);
    }
    if (session) {
        g64_session_set_flags(session, hostFlags);
        g64_session_set_colors(session, myGlove, myBall);
        g64_session_set_build(session, build_hash());
        g64_session_set_name(session, myName);
        g64_session_on_chat(session, on_chat, NULL);
    }
    if (!session) {
        char why[96];
        snprintf(why, sizeof why, "cannot start the session (2 to %d players)", G64_SESSION_MAX_PLAYERS);
        return fail_connect(why);
    }
    cstate = C_SESSION;
    cstart = now_ms();
    return 0;
}

int netplay_start(const char *join, int hostPort, const char *relay, const char *room, int wanted, int deadline,
                  const uint8_t *rom, size_t romSize, char *err, size_t errSize) {
    netplay_cancel();
    cerror[0] = 0;
    cjoined = 0;
    chatCount = 0;
    memset(kickNoted, 0, sizeof kickNoted);
    announced = 0;
    netplay_rooms_close();
    romHash = g64_rom_hash(rom, romSize);
    cwanted = wanted;
    cdeadline = deadline;
    chost = !join && !room;
    chostPort = hostPort;
    snprintf(crelay, sizeof crelay, "%s", relay ? relay : "");
    snprintf(cjoin, sizeof cjoin, "%s", join ? join : "");
    snprintf(croom, sizeof croom, "%s", room ? room : "");
    if (net_init() != 0) {
        snprintf(err, errSize, "the network did not start");
        return fail_connect(err);
    }
    sock = net_open(join || relay ? 0 : (uint16_t)hostPort, err, errSize);
    if (!sock)
        return fail_connect(err);
    cstart = now_ms();
    if (relay) {
        net_addr relays[NET_RELAY_ADDRS];
        int count = net_resolve_all(relay, 27065, relays, NET_RELAY_ADDRS, err, errSize);
        if (count < 0)
            return fail_connect(err);
        if (room ? net_relay_join(sock, relays, count, room) : net_relay_host(sock, relays, count)) {
            snprintf(err, errSize, "a room code has %d letters and digits", NET_ROOM_CODE);
            return fail_connect(err);
        }
        cstate = C_RELAY;
        return 0;
    }
    if (make_session() != 0) {
        snprintf(err, errSize, "%s", cerror);
        return -1;
    }
    return 0;
}

int netplay_poll(void) {
    uint32_t t = now_ms();
    if (cstate == C_FAILED)
        return -1;
    if (cstate == C_STARTED)
        return 1;
    if (cstate == C_IDLE)
        return fail_connect("not connecting");
    net_relay_tick(sock, t);
    if (cstate == C_RELAY) {
        uint8_t d[NET_MAX_DATAGRAM];
        net_addr from;
        /* The relay's own answers are handled inside net_recv; nothing else comes before the room is ready. */
        while (net_recv(sock, &from, d, sizeof d) >= 0) {
        }
        if (net_relay_state(sock) < 0)
            return fail_connect(net_relay_refusal(sock) == 2   ? "that room is full"
                                : net_relay_refusal(sock) == 3 ? "the host has removed you from that room"
                                                               : "there is no room with that code");
        if (net_relay_state(sock) == 0) {
            if ((int32_t)(t - cstart) > 15000) {
                char why[300];
                snprintf(why, sizeof why, "no answer from the relay at %s", crelay);
                return fail_connect(why);
            }
            snprintf(cstatus, sizeof cstatus, "Connecting to the relay...");
            return 0;
        }
        if (!croom[0])
            snprintf(croom, sizeof croom, "%s", net_relay_code(sock));
        if (make_session() != 0)
            return -1;
    }
    {
        g64_session_state st = g64_session_poll(session, t, NULL, NULL);
        if (st == G64_SESSION_FAILED)
            return fail_connect(g64_session_error(session));
        /* The relay's word of a ban, for a joiner whose kick message was lost. The relay's word is not sealed, so
           it counts only once the host has also been silent for three seconds. */
        if (!chost && crelay[0] && net_relay_state(sock) < 0 && g64_session_host_silent(session, t) > 3000)
            return fail_connect("the host has removed you from that room");
        send_ban();
        announce_room(st == G64_SESSION_WAITING);
        if (st == G64_SESSION_STARTED) {
            me = g64_session_me(session);
            players = g64_session_players(session);
            clevel = g64_session_level(session);
            cstate = C_STARTED;
            active = 1;
            return 1;
        }
        /* The session can be made in this same call, a moment after t was read: the difference is signed. */
        if ((int32_t)(t - cstart) > CONNECT_TIMEOUT_MS)
            return fail_connect("nobody came");
        cjoined = g64_session_joined(session);
        if (chost)
            snprintf(cstatus, sizeof cstatus, "%d of %d players are in", cjoined, cwanted);
        else
            snprintf(cstatus, sizeof cstatus, "Waiting for the host to start...");
    }
    return 0;
}

void netplay_cancel(void) {
    /* A host that gives up takes its room off the list. */
    if (session && sock && chost && publicRoom && crelay[0] && cstate != C_STARTED)
        net_relay_announce(sock, build_hash(), cwanted, 0, 0, 0, publicTitle[0] ? publicTitle : myName);
    netplay_rooms_close();
    if (session) {
        g64_session_free(session);
        session = NULL;
    }
    if (sock) {
        net_close(sock);
        sock = NULL;
    }
    cstate = C_IDLE;
    active = 0;
}

const char *netplay_wait_status(void) { return cstatus; }
const char *netplay_error(void) { return cerror; }
const char *netplay_room_code(void) { return cstate >= C_SESSION && crelay[0] ? croom : ""; }
int netplay_joined(void) { return cjoined; }
int netplay_level(void) { return clevel; }
int netplay_players(void) { return players; }
int netplay_is_host(void) { return chost; }

int netplay_connect(const char *join, int hostPort, const char *relay, const char *room, int wanted, int deadline,
                    const uint8_t *rom, size_t romSize, int *level, int *outPlayers, char *err, size_t errSize) {
    int r, lastJoined = 0, shownCode = 0;
    if (relay)
        printf("g64coopdx: connecting to the relay at %s\n", relay);
    else if (join)
        printf("g64coopdx: joining %s\n", join);
    else
        printf("g64coopdx: hosting on UDP port %d for %d players\n", hostPort, wanted);
    fflush(stdout);
    if (netplay_start(join, hostPort, relay, room, wanted, deadline, rom, romSize, err, errSize) != 0)
        return -1;
    while ((r = netplay_poll()) == 0) {
        if (relay && !room && !shownCode && netplay_room_code()[0]) {
            shownCode = 1;
            printf("g64coopdx: hosting for %d players. Room code: %s\n", wanted, netplay_room_code());
            printf("g64coopdx: the others join with --online-join %s\n", netplay_room_code());
            fflush(stdout);
        }
        if (!join && !room && netplay_joined() != lastJoined) {
            lastJoined = netplay_joined();
            printf("g64coopdx: %d of %d players\n", lastJoined, wanted);
            fflush(stdout);
        }
        sleep_ms(10);
    }
    if (r < 0) {
        snprintf(err, errSize, "%s", netplay_error());
        return -1;
    }
    *level = netplay_level();
    *outPlayers = players;
    if (room)
        printf("g64coopdx: joined room %s\n", room);
    printf("g64coopdx: the game starts; you are player %d of %d\n", me + 1, players);
    fflush(stdout);
    return 0;
}

/* On the host, its own player and the host's part talk through a queue, in this program: each message waits until
 * the next netplay_frame. */
#define LOCAL_QUEUE 256
typedef struct {
    int to_host;
    size_t len;
    uint8_t data[1200];
} local_message;
static local_message localQueue[LOCAL_QUEUE];
static int localCount;

static void queue_local(int to_host, const uint8_t *data, size_t len) {
    if (localCount >= LOCAL_QUEUE || len > sizeof localQueue[0].data)
        return;
    localQueue[localCount].to_host = to_host;
    localQueue[localCount].len = len;
    memcpy(localQueue[localCount].data, data, len);
    localCount++;
}

static void flush_local(void) {
    static local_message batch[LOCAL_QUEUE];
    int i, n = localCount;
    /* Handling a message can queue new ones: they wait for the next flush. */
    memcpy(batch, localQueue, sizeof(local_message) * (size_t)n);
    localCount = 0;
    for (i = 0; i < n; i++) {
        if (batch[i].to_host)
            g64_ihost_receive(ihost, 0, batch[i].data, batch[i].len);
        else
            g64_iclient_receive(iclient, batch[i].data, batch[i].len);
    }
}

/* The session's game messages go to the instances. */
static void on_game(void *user, int from, const uint8_t *data, size_t len) {
    (void)user;
    if (ihost)
        g64_ihost_receive(ihost, from, data, len);
    else if (iclient)
        g64_iclient_receive(iclient, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    if (to == 0)
        queue_local(0, data, len);
    else
        g64_session_send_game(session, to, data, len);
}

static void client_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to;
    if (ihost)
        queue_local(1, data, len);
    else
        g64_session_send_game(session, 0, data, len);
}

/* For tests of the whole program: G64_TEST_EXITS="level:frame:player:type,..." gives weemitt's g64TestExits. Every
 * machine of the session must have the same value. */
static void read_test_exits(void) {
    const char *v = getenv("G64_TEST_EXITS");
    int i = 0;
    while (v && *v && i < TEST_EXITS) {
        G64TestExit t;
        if (sscanf(v, "%d:%d:%d:%d", &t.level, &t.frame, &t.player, &t.type) != 4)
            break;
        g64TestExits[i++] = t;
        v = strchr(v, ',');
        if (v)
            v++;
    }
}

/* The host's self-check. On the host, its own player's game and the host's reference copy of the same instance run
 * in one process. Every 10th confirmed frame, their states must be equal. When they are not, the log lists where the
 * game's memory differs, as N64 addresses (no contents): the place to look for what is not deterministic. */
#define CHECK_EVERY 10
#define CHECK_KEEP 4
#define CHECK_REPORTS 5
#define RAM_BYTES 0x1800000u
typedef struct {
    int used, instance;
    uint32_t frame;
    void *state;
} kept_state;
static kept_state kept[CHECK_KEEP];
static unsigned char *ramCopy;
static int reports;

static void on_reference_final(void *user, int instance, uint32_t frame, const void *state) {
    kept_state *k = &kept[(frame / CHECK_EVERY) % CHECK_KEEP];
    (void)user;
    if (frame % CHECK_EVERY != 0 || instance != g64_iclient_instance(iclient) || !k->state)
        return;
    memcpy(k->state, state, engine_state_used(state));
    k->used = 1;
    k->instance = instance;
    k->frame = frame;
}

/* Lists the ranges of the game's memory where two states differ (at most 40, merged across gaps under 16 bytes). */
static void report_difference(const void *reference, const void *local, int instance, uint32_t frame) {
    const unsigned char *ram = engine_ram(engine);
    uint32_t i, start = 0, end = 0, total = 0;
    int ranges = 0, open = 0;
    char line[1024];
    size_t n;
    rb_engine_release();
    engine_load(engine, reference);
    memcpy(ramCopy, ram, RAM_BYTES);
    engine_load(engine, local);
    n = (size_t)snprintf(line, sizeof line, "g64coopdx: self-check: level %d (instance %d), frame %u differs at",
                         g64_iclient_level(iclient), instance, frame);
    for (i = 0; i < RAM_BYTES; i++) {
        if (ram[i] == ramCopy[i])
            continue;
        total++;
        if (open && i - end < 16) {
            end = i + 1;
            continue;
        }
        if (open && ranges < 40 && n < sizeof line - 40)
            n += (size_t)snprintf(line + n, sizeof line - n, " %08x+%u", 0x80000000u + start, end - start);
        if (open)
            ranges++;
        open = 1;
        start = i;
        end = i + 1;
    }
    if (open && ranges < 40 && n < sizeof line - 40)
        n += (size_t)snprintf(line + n, sizeof line - n, " %08x+%u", 0x80000000u + start, end - start);
    if (open)
        ranges++;
    plog_printf("%s; %u bytes in %d ranges", line, total, ranges);
}

static void on_move(void *user, int player, int exit_type, int from_level, int level, int world, uint32_t join,
                    const g64_progress *g) {
    (void)user;
    plog_printf("g64coopdx: player %d took exit type %d: level %d to level %d (world %d), joining at frame %u; "
                "progress: levels %08x, all garibs %08x, hub target %u, hub level %u, worlds done %02x",
                player + 1, exit_type, from_level, level, world, join, g->levels, g->all_garibs, g->hub_target,
                g->hub_level, g->worlds_done);
}

static void on_local_final(void *user, int instance, uint32_t frame, const void *state) {
    kept_state *k = &kept[(frame / CHECK_EVERY) % CHECK_KEEP];
    const rb_game_ops *ops = rb_engine_ops(engine);
    (void)user;
    if (frame % CHECK_EVERY != 0 || reports >= CHECK_REPORTS || !k->used || k->instance != instance ||
        k->frame != frame || !ramCopy)
        return;
    k->used = 0;
    if (ops->hash_state(k->state) == ops->hash_state(state))
        return;
    reports++;
    report_difference(k->state, state, instance, frame);
}

/* The local player's input delay (netplay_set_input_delay). */
#define INPUT_DELAY 1
static int inputDelay = INPUT_DELAY;

void netplay_set_input_delay(int frames) { inputDelay = frames < 0 ? 0 : frames > 8 ? 8 : frames; }
int netplay_input_delay(void) { return inputDelay; }
int netplay_host_normal_ball(void) { return (hostFlags & G64_FLAG_NORMAL_BALL) != 0; }
int netplay_host_any_ball(void) { return (hostFlags & G64_FLAG_ANY_BALL) != 0; }

/* The sound (sound.h, soundout.h): the reader of the local game's sound table, and the game it last read. */
static g64_sound *sound;
static int soundInstance = -1, soundMoved;
static int soundVolumes[3] = {-1, -1, -1};

void netplay_set_volumes(int master, int music, int effects) {
    if (master == soundVolumes[0] && music == soundVolumes[1] && effects == soundVolumes[2])
        return;
    soundVolumes[0] = master;
    soundVolumes[1] = music;
    soundVolumes[2] = effects;
    soundout_volumes(master, music, effects);
}

int netplay_begin(Engine *e, char *err, size_t errSize) {
    uint32_t deadline = (uint32_t)g64_session_deadline(session);
    engine = e;
    read_test_exits();
    boot = malloc(engine_state_size(e));
    viewState = malloc(engine_state_size(e));
    if (!boot || !viewState) {
        snprintf(err, errSize, "out of memory");
        return -1;
    }
    /* The engine holds the state right after power-on (platform.c calls port_game_boot when online). Power-on sets the
       game's variables, so every player's colors go in after it, and the saved state holds them. */
    netplay_apply_colors();
    /* The game records its sound in its sound table from the start: every game built from this state does. */
    soundtable_enable(1);
    engine_save(e, boot);
    if (soundout_open(port_rom, port_rom_size) == 0)
        sound = g64_sound_new(soundout_backend());
    /* The host's rule for the ball, before any game is built. */
    g64_instances_any_ball(netplay_any_ball());
    if (me == 0) {
        ihost = g64_ihost_new(e, boot, players, clevel, deadline, host_send, NULL);
        if (!ihost) {
            snprintf(err, errSize, "cannot start the host's instances");
            return -1;
        }
    }
    iclient = g64_iclient_new(e, boot, players, me, clevel, WINDOW, deadline, client_send, NULL);
    if (!iclient) {
        snprintf(err, errSize, "cannot start the game");
        return -1;
    }
    g64_iclient_set_input_delay(iclient, inputDelay);
    if (ihost) {
        int i;
        ramCopy = malloc(RAM_BYTES);
        for (i = 0; i < CHECK_KEEP && ramCopy; i++)
            kept[i].state = malloc(engine_state_size(e));
        g64_ihost_on_final(ihost, on_reference_final, NULL);
        g64_ihost_on_move(ihost, on_move, NULL);
        g64_iclient_on_final(iclient, on_local_final, NULL);
    }
    plog_printf("g64coopdx: build %s", netplay_build());
    plog_printf("g64coopdx: player %d of %d, level %d, deadline %u, input delay %d", me + 1, players, clevel, deadline,
                inputDelay);
    {
        int p;
        for (p = 0; p < players && p < 16; p++)
            plog_printf("g64coopdx: player %d's colors: glove %07x, ball %07x", p + 1, g64PlayerColor[p][0],
                        g64PlayerColor[p][1]);
    }
    return 0;
}

/* Smoothing of the other gloves. A correction after a wrong prediction moves another glove at once. The drawn glove
 * keeps the old place, and the gap closes over a few frames: offset is the gap still to close, per player. Only the
 * drawn picture changes, never the game. */
#define SMOOTH_KEEP 0.7f    /* the part of the gap that remains after each game frame */
#define SMOOTH_LIMIT 400.0f /* a larger jump is a real move (a respawn): it is not smoothed */
static float before[G64_SESSION_MAX_PLAYERS][3], offset[G64_SESSION_MAX_PLAYERS][3];
static int haveBefore[G64_SESSION_MAX_PLAYERS];
static float corrected; /* the distance corrections moved other gloves since the last status line */
static rb_engine_game *smoothGame;

/* Where each other player's glove is in the local game's newest frame. */
static void read_positions(rb_engine_game *game, float pos[][3], int *have) {
    int p;
    rb_engine_game_enter(game);
    for (p = 0; p < players && p < G64_SESSION_MAX_PLAYERS; p++)
        have[p] = p != me && G64PlayerPosition(p, &pos[p][0], &pos[p][1], &pos[p][2]);
}

/* After the messages: the newest frame again. A glove that moved since before was moved by a correction. */
static void measure_corrections(void) {
    rb_engine_game *game = g64_iclient_game(iclient);
    float after[G64_SESSION_MAX_PLAYERS][3];
    int have[G64_SESSION_MAX_PLAYERS], p, k;
    if (!game || game != smoothGame) {
        memset(offset, 0, sizeof offset);
        memset(haveBefore, 0, sizeof haveBefore);
        smoothGame = game;
        return;
    }
    read_positions(game, after, have);
    for (p = 0; p < players && p < G64_SESSION_MAX_PLAYERS; p++) {
        float d[3], len2 = 0;
        if (!have[p] || !haveBefore[p]) {
            memset(offset[p], 0, sizeof offset[p]);
            continue;
        }
        for (k = 0; k < 3; k++) {
            d[k] = before[p][k] - after[p][k];
            len2 += d[k] * d[k];
        }
        if (len2 > SMOOTH_LIMIT * SMOOTH_LIMIT) {
            memset(offset[p], 0, sizeof offset[p]);
            continue;
        }
        for (k = 0; k < 3; k++)
            offset[p][k] += d[k];
        corrected += sqrtf(len2);
    }
}

/* Puts the game back from the local player's view (netplay_frame) to the state the local game left. */
static void restore_view(void) {
    if (viewPending) {
        engine_load(engine, viewState);
        viewPending = 0;
    }
}

/* Each player's own pause menu. It does not stop the game: while it is open the player's glove gets no input. A
 * choice that acts in the game goes in the player's input (weemitt's G64_ACTION_BUTTON, g64players.h) for a few frames,
 * so the host confirms it even if one input is lost; doing it twice does nothing more. */
#define ACTION_BUTTON 0x0080
#define ACTION_FRAMES 3
enum { PAUSE_RESUME, PAUSE_CHECKPOINT, PAUSE_HUB, PAUSE_LEAVE, PAUSE_ITEMS };
static const char *const pauseItems[PAUSE_ITEMS] = {"Resume", "Back to checkpoint", "Back to the hub",
                                                    "Leave the online game"};
static int pauseOpen, pauseSelection, pauseQuit, actionFrames, action;
static uint16_t lastButtons;
/* The buttons held when the menu opened or closed. The game gets none of them until each is released: the press that
   closes the menu must not also jump or grab. */
static uint16_t heldBack;
static int8_t lastStickY;

void netplay_pause_toggle(void) {
    pauseOpen = !pauseOpen;
    pauseSelection = 0;
    heldBack = lastButtons;
}

int netplay_pause_open(void) { return pauseOpen; }
int netplay_pause_selection(void) { return pauseSelection; }
int netplay_pause_items(void) { return PAUSE_ITEMS; }
const char *netplay_pause_item(int i) { return i >= 0 && i < PAUSE_ITEMS ? pauseItems[i] : ""; }
int netplay_quit_requested(void) { return pauseQuit; }

void netplay_pause_pick(int item) {
    switch (item) {
    case PAUSE_CHECKPOINT:
        action = 1;
        actionFrames = ACTION_FRAMES;
        break;
    case PAUSE_HUB:
        action = 2;
        actionFrames = ACTION_FRAMES;
        break;
    case PAUSE_LEAVE:
        pauseQuit = 1;
        break;
    default:
        break;
    }
}

/* The menu, driven by the local controller: Start opens it; up and down choose; A picks; B or Start closes it. */
static void pause_menu(uint16_t buttons, int8_t stickY) {
    uint16_t pressed = (uint16_t)(buttons & ~lastButtons);
    int up = (pressed & 0x0800) || (stickY > 40 && lastStickY <= 40);
    int down = (pressed & 0x0400) || (stickY < -40 && lastStickY >= -40);
    lastButtons = buttons;
    lastStickY = stickY;
    if (!pauseOpen) {
        if (pressed & 0x1000)
            netplay_pause_toggle();
        return;
    }
    if (pressed & (0x1000 | 0x4000)) {
        netplay_pause_toggle();
        return;
    }
    if (up)
        pauseSelection = (pauseSelection + PAUSE_ITEMS - 1) % PAUSE_ITEMS;
    if (down)
        pauseSelection = (pauseSelection + 1) % PAUSE_ITEMS;
    if (!(pressed & 0x8000))
        return;
    netplay_pause_pick(pauseSelection);
    netplay_pause_toggle();
}

/* The tip that the local player reads (weemitt's g64players.c, "Tips"): the page's lines, copied from the local
 * game's memory while the engine holds it. The game holds the reader's glove and takes their A; the window only
 * draws the box. The text never leaves this machine. */
#define TIP_LINES 8
#define TIP_CHARS 96
int G64TipPage(int player, const char **lines, int max, int *more);
static char tipText[TIP_LINES][TIP_CHARS];
static int tipLines, tipMore;

/* The game's font has signs for the controller's buttons above the ASCII range (the tips name them): the window's
   font has none, so the box names the button in words. Any other sign becomes a space. */
static const char *tip_sign(unsigned char c) {
    switch (c) {
    case 0xA3: return "A";
    case 0xA4: return "B";
    case 0xA5: return "Z";
    case 0xA6: return "THE STICK";
    case 0xA9: return "R";
    case 0xAA: return "C-LEFT";
    case 0xAB: return "C-RIGHT";
    default: return " ";
    }
}

static void read_tip(void) {
    const char *lines[TIP_LINES];
    int i, k, n;
    tipMore = 0;
    tipLines = G64TipPage(me, lines, TIP_LINES, &tipMore);
    for (i = 0; i < tipLines; i++) {
        n = 0;
        for (k = 0; lines[i] && lines[i][k]; k++) {
            unsigned char c = (unsigned char)lines[i][k];
            const char *word = tip_sign(c);
            if (c >= 0x20 && c < 0x7F) {
                if (n < TIP_CHARS - 1)
                    tipText[i][n++] = (char)c;
                continue;
            }
            while (*word && n < TIP_CHARS - 1)
                tipText[i][n++] = *word++;
        }
        tipText[i][n] = 0;
    }
}

int netplay_tip_lines(void) { return tipLines; }
const char *netplay_tip_line(int i) { return i >= 0 && i < tipLines ? tipText[i] : ""; }
int netplay_tip_more(void) { return tipMore; }

int netplay_frame(uint16_t buttons, int8_t stickX, int8_t stickY, void *tasks, int max) {
    rb_input in;
    rb_engine_game *game;
    if (!active)
        return -1;
    restore_view();
    if (g64_iclient_game(iclient) && g64_iclient_game(iclient) == smoothGame)
        read_positions(smoothGame, before, haveBefore);
    else
        memset(haveBefore, 0, sizeof haveBefore);
    net_relay_tick(sock, now_ms());
    send_ban();
    if (g64_session_poll(session, now_ms(), on_game, NULL) == G64_SESSION_FAILED) {
        failed = 1;
        tipLines = 0;
        snprintf(status, sizeof status, "g64coopdx: %s", g64_session_error(session));
        return -1;
    }
    /* The relay's word of a ban, for a joiner whose kick message was lost. The relay's word is not sealed, so it
       counts only once the host has also been silent for three seconds. */
    if (!chost && crelay[0] && net_relay_state(sock) < 0 && g64_session_host_silent(session, now_ms()) > 3000) {
        failed = 1;
        tipLines = 0;
        snprintf(status, sizeof status, "g64coopdx: the host removed you from the game");
        return -1;
    }
    if (ihost) {
        int p;
        for (p = 1; p < players; p++)
            if (g64_session_gone(session, p) && !dropped[p]) {
                dropped[p] = 1;
                g64_ihost_drop(ihost, p);
                plog_printf(kickNoted[p] ? "g64coopdx: player %d was removed; the game goes on without them"
                                         : "g64coopdx: player %d stopped answering; the game goes on without them",
                            p + 1);
                if (!kickNoted[p]) {
                    char note[64];
                    snprintf(note, sizeof note, "%s left the game",
                             g64_session_name(session, p)[0] ? g64_session_name(session, p) : "A player");
                    chat_add(-1, note);
                }
            }
        flush_local();
        g64_ihost_tick(ihost);
        flush_local();
    }
    {
        /* For tests: G64_TEST_CHAT is a line that this player says at frame 300. */
        static int said;
        const char *say = said ? NULL : getenv("G64_TEST_CHAT");
        if (say && g64_iclient_frame(iclient) >= 300) {
            said = 1;
            netplay_chat_send(say);
        }
    }
    measure_corrections();
    memset(&in, 0, sizeof in);
    /* The menu's own button never comes from the controller. */
    buttons = (uint16_t)(buttons & ~ACTION_BUTTON);
    pause_menu(buttons, stickY);
    heldBack &= buttons;
    if (actionFrames > 0) {
        actionFrames--;
        in.buttons = ACTION_BUTTON;
        in.x = (int8_t)action;
    } else if (!pauseOpen) {
        /* Start opens the menu; the game never sees it. */
        in.buttons = (uint16_t)(buttons & ~0x1000 & ~heldBack);
        in.x = stickX;
        in.y = stickY;
    }
    /* The host decides who is present; the core fills it in. */
    in.present = 1;
    g64_iclient_step(iclient, in);
    if (g64_iclient_moving(iclient)) {
        snprintf(status, sizeof status, "g64coopdx - player %d of %d - going to level %d...", me + 1, players,
                 g64_iclient_level(iclient));
    } else {
        snprintf(status, sizeof status, "g64coopdx - player %d of %d - level %d - %u frames ahead - %u desyncs",
                 me + 1, players, g64_iclient_level(iclient),
                 g64_iclient_frame(iclient) - g64_iclient_confirmed(iclient), g64_iclient_desyncs(iclient));
    }
    if (g64_iclient_frame(iclient) % 100 == 0 && g64_iclient_frame(iclient) != lastPrinted) {
        lastPrinted = g64_iclient_frame(iclient);
        char line[256];
        int n = snprintf(line, sizeof line, "g64coopdx: level %d, frame %u, %u confirmed, %llu frames run again, %u desyncs",
                         g64_iclient_level(iclient), g64_iclient_frame(iclient), g64_iclient_confirmed(iclient),
                         (unsigned long long)g64_iclient_rollback_frames(iclient), g64_iclient_desyncs(iclient));
        if (n > 0 && (size_t)n < sizeof line)
            n += snprintf(line + n, sizeof line - (size_t)n, ", corrections moved others by %.0f", (double)corrected);
        corrected = 0;
        if (ihost && n > 0 && (size_t)n < sizeof line)
            snprintf(line + n, sizeof line - (size_t)n, ", %d instances, %u hash checks, %u mismatches",
                     g64_ihost_instances(ihost), g64_ihost_hash_matches(ihost), g64_ihost_hash_mismatches(ihost));
        plog_printf("%s", line);
    }
    /* While the player moves to another level, nothing is drawn. */
    game = g64_iclient_game(iclient);
    if (!game) {
        soundMoved = 1;
        tipLines = 0;
        return 0;
    }
    /* The engine may hold another game (the host's instances): the local game's newest frame goes back in, and is
       drawn with this player swapped into the globals. */
    rb_engine_game_enter(game);
    engine_save(engine, viewState);
    viewPending = 1;
    read_tip();
    /* The sound of the local game's newest frame, before this player is swapped into the globals. A new game (after a
       move) starts the reader afresh. */
    if (sound) {
        if (soundMoved || g64_iclient_instance(iclient) != soundInstance) {
            g64_sound_reset(sound);
            soundInstance = g64_iclient_instance(iclient);
            soundMoved = 0;
        }
        g64_sound_update(sound, me);
        soundout_tick();
    }
    {
        /* The shifts go by player number, so they come before the swap, which moves this player into the globals. */
        int p, k;
        for (p = 0; p < players && p < G64_SESSION_MAX_PLAYERS; p++) {
            if (p == me)
                continue;
            G64NudgePlayer(p, offset[p][0], offset[p][1], offset[p][2]);
            for (k = 0; k < 3; k++) {
                offset[p][k] *= SMOOTH_KEEP;
                if (offset[p][k] > -0.05f && offset[p][k] < 0.05f)
                    offset[p][k] = 0;
            }
        }
    }
    /* The tip that this player reads, in the game's own box. */
    G64TipView(me);
    G64SwapPlayer(me);
    return port_game_draw_view(tasks, max);
}

const char *netplay_status(void) { return status; }
