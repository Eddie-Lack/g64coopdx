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

int netplay_connect(const char *join, int hostPort, int wanted, int deadline, const uint8_t *rom, size_t romSize,
                    int *level, int *outPlayers, char *err, size_t errSize) {
    uint32_t start, last = 0, romHash = g64_rom_hash(rom, romSize);
    if (net_init() != 0) {
        snprintf(err, errSize, "the network did not start");
        return -1;
    }
    sock = net_open(join ? 0 : (uint16_t)hostPort, err, errSize);
    if (!sock)
        return -1;
    if (join) {
        net_addr to;
        char text[64];
        if (net_resolve(join, G64_DEFAULT_PORT, &to, err, errSize) != 0)
            return -1;
        session = g64_session_join(sock, &to, romHash, "player");
        net_addr_text(&to, text, sizeof text);
        printf("g64coopdx: joining %s\n", text);
    } else {
        session = g64_session_host(sock, wanted, romHash, START_LEVEL, deadline > 0 ? deadline : DEADLINE);
        printf("g64coopdx: hosting on UDP port %u for %d players\n", (unsigned)net_port(sock), wanted);
    }
    if (!session) {
        snprintf(err, errSize, "cannot start the session (2 to %d players)", G64_SESSION_MAX_PLAYERS);
        return -1;
    }
    fflush(stdout);
    start = now_ms();
    for (;;) {
        uint32_t t = now_ms();
        g64_session_state st = g64_session_poll(session, t, NULL, NULL);
        if (st == G64_SESSION_FAILED) {
            snprintf(err, errSize, "%s", g64_session_error(session));
            return -1;
        }
        if (st == G64_SESSION_STARTED)
            break;
        if (t - start > CONNECT_TIMEOUT_MS) {
            snprintf(err, errSize, "nobody came");
            return -1;
        }
        if (!join && g64_session_joined(session) != (int)last) {
            last = (uint32_t)g64_session_joined(session);
            printf("g64coopdx: %u of %d players\n", last, wanted);
            fflush(stdout);
        }
        sleep_ms(10);
    }
    me = g64_session_me(session);
    players = g64_session_players(session);
    *level = g64_session_level(session);
    *outPlayers = players;
    printf("g64coopdx: the game starts; you are player %d of %d\n", me + 1, players);
    fflush(stdout);
    active = 1;
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
