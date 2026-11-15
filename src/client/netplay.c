/* The online mode of the window program (netplay.h). */
#include "netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* weemitt's src/game/g64players.c and src/port/frame.c. */
int G64PlayerPosition(int p, float *x, float *y, float *z);
void G64SwapPlayer(int p);
int port_game_draw_view(void *tasks, int max);

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
static const rb_game_ops *ops;
static rb_engine_game *localGame, *hostGame;
static rb_client *client;
static rb_host *host;
static char status[160];
/* The local player's view: a joiner's glove is not in the game's globals, so its view is drawn with its slot swapped
 * in, from a saved state that is loaded again after the draw. */
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

/* Makes the session once the socket is ready: at once in the direct mode, after the relay's answer through it. */
static int make_session(void) {
    if (crelay[0] && !chost) {
        net_addr hostAddr = net_relay_player(0);
        session = g64_session_join(sock, &hostAddr, romHash, "player");
    } else if (crelay[0]) {
        session = g64_session_host(sock, cwanted, romHash, START_LEVEL, cdeadline > 0 ? cdeadline : DEADLINE);
    } else if (cjoin[0]) {
        net_addr to;
        char err[160];
        if (net_resolve(cjoin, G64_DEFAULT_PORT, &to, err, sizeof err) != 0)
            return fail_connect(err);
        session = g64_session_join(sock, &to, romHash, "player");
    } else {
        session = g64_session_host(sock, cwanted, romHash, START_LEVEL, cdeadline > 0 ? cdeadline : DEADLINE);
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
        net_addr relayAddr;
        if (net_resolve(relay, 27065, &relayAddr, err, errSize) != 0)
            return fail_connect(err);
        if (room ? net_relay_join(sock, &relayAddr, room) : net_relay_host(sock, &relayAddr)) {
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
            return fail_connect(net_relay_refusal(sock) == 2 ? "that room is full" : "there is no room with that code");
        if (net_relay_state(sock) == 0) {
            if (t - cstart > 15000) {
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
        if (st == G64_SESSION_STARTED) {
            me = g64_session_me(session);
            players = g64_session_players(session);
            clevel = g64_session_level(session);
            cstate = C_STARTED;
            active = 1;
            return 1;
        }
        if (t - cstart > CONNECT_TIMEOUT_MS)
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

/* The session's game messages go to the rollback core. */
static void on_game(void *user, int from, const uint8_t *data, size_t len) {
    (void)user;
    if (host)
        rb_host_receive(host, from, data, len);
    else if (client)
        rb_client_receive(client, data, len);
}

/* A joiner's client sends to the host over the network. */
static void client_send_net(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to;
    g64_session_send_game(session, 0, data, len);
}

/* On the host, the local client talks to the host directly, and the host to the others over the network. */
static void client_send_local(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to;
    rb_host_receive(host, 0, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    if (to == 0)
        rb_client_receive(client, data, len);
    else
        g64_session_send_game(session, to, data, len);
}

int netplay_begin(Engine *e, char *err, size_t errSize) {
    engine = e;
    ops = rb_engine_ops(e);
    if (me != 0) {
        viewState = malloc(engine_state_size(e));
        if (!viewState) {
            snprintf(err, errSize, "out of memory");
            return -1;
        }
    }
    localGame = rb_engine_game_new();
    if (!localGame) {
        snprintf(err, errSize, "out of memory");
        return -1;
    }
    if (me == 0) {
        hostGame = rb_engine_game_new();
        host = rb_host_new(ops, hostGame, players, (uint32_t)g64_session_deadline(session), host_send, NULL, NULL,
                           NULL);
        client = rb_client_new(ops, localGame, players, 0, WINDOW, client_send_local, NULL, NULL);
    } else {
        client = rb_client_new(ops, localGame, players, me, WINDOW, client_send_net, NULL, NULL);
    }
    if (!client || (me == 0 && !host)) {
        snprintf(err, errSize, "cannot start the rollback");
        return -1;
    }
    return 0;
}

void netplay_after_draw(void) {
    if (viewPending) {
        engine_load(engine, viewState);
        viewPending = 0;
    }
}

int netplay_frame(uint16_t buttons, int8_t stickX, int8_t stickY, void *tasks, int max) {
    rb_input in;
    int n;
    if (!active)
        return -1;
    netplay_after_draw();
    net_relay_tick(sock, now_ms());
    if (g64_session_poll(session, now_ms(), on_game, NULL) == G64_SESSION_FAILED) {
        failed = 1;
        snprintf(status, sizeof status, "g64coopdx: %s", g64_session_error(session));
        return rb_engine_last_tasks(tasks, max);
    }
    if (host) {
        int p;
        for (p = 1; p < players; p++)
            if (g64_session_gone(session, p) && !dropped[p]) {
                dropped[p] = 1;
                rb_host_drop_player(host, p);
                printf("g64coopdx: player %d stopped answering; the game goes on without them\n", p + 1);
                fflush(stdout);
            }
        rb_host_tick(host);
    }
    in.buttons = buttons;
    in.x = stickX;
    in.y = stickY;
    /* The local game steps last, so the engine holds it and its newest frame is what is drawn. */
    if (!rb_client_step(client, in))
        rb_client_idle(client);
    n = rb_engine_last_tasks(tasks, max);
    snprintf(status, sizeof status, "g64coopdx - player %d of %d - %u frames ahead - %u desyncs", me + 1, players,
             rb_client_frame(client) - rb_client_confirmed(client), rb_client_desyncs(client));
    if (rb_client_frame(client) % 100 == 0 && rb_client_frame(client) != lastPrinted) {
        lastPrinted = rb_client_frame(client);
        printf("g64coopdx: frame %u, %u confirmed, %llu frames run again, %u desyncs", rb_client_frame(client),
               rb_client_confirmed(client), (unsigned long long)rb_client_rollback_frames(client),
               rb_client_desyncs(client));
        if (host)
            printf(", %u hash checks, %u mismatches, %u late inputs", rb_host_hash_matches(host),
                   rb_host_hash_mismatches(host), rb_host_late_inputs(host));
        {
            int p;
            for (p = 0; p < players; p++) {
                float x, y, z;
                if (G64PlayerPosition(p, &x, &y, &z))
                    printf(", glove %d at %.0f %.0f %.0f", p + 1, x, y, z);
            }
        }
        printf("\n");
        fflush(stdout);
    }
    if (me != 0 && !failed) {
        /* The engine holds the local game's newest frame. Draw it with this player swapped in. */
        engine_save(engine, viewState);
        G64SwapPlayer(me);
        n = port_game_draw_view(tasks, max);
        viewPending = 1;
    }
    return n;
}

const char *netplay_status(void) { return status; }
