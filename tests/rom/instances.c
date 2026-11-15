/* Instances with the real game (src/client/instances.c), with the ROM.
 *
 * A host and three players run in one process over a simulated network that delays and loses messages. All start in
 * the first hub section. The game's test exits (g64TestExits) then move them, the same way on every machine:
 *   - player 1 takes the Atlantis door: a new instance of the way room starts for them alone;
 *   - player 2 takes the same door later: they join the way room's running instance, after its log;
 *   - player 1 takes the way room's exit back to the hub: they join the hub's running instance, after its log.
 * The test: every player ends up in the right level, every instance's hash checks pass, and no player is told of a
 * desync. It prints the host's hash checks and each player's level; make instances runs it on Linux and under Wine and
 * compares the outputs.
 *
 * Usage: instances ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/instances.h"

typedef struct {
    int level, frame, player, type;
} G64TestExit;
extern G64TestExit g64TestExits[4];

/* enum Level, enum World and enum ExitType (weemitt/include/enums.h). */
enum { HUB1 = 0, WAYROOM = 42, ATLANTIS_EXIT = 6, HUB_EXIT = 13 };

#define PLAYERS 4
#define FRAMES 1800
#define LATENCY 2
#define LOSS 30 /* per 1000 messages */
#define MAX_PACKETS 8192
#define DEADLINE 4
#define WINDOW 32

typedef struct {
    uint32_t at;
    int to; /* a player, or -1 for the host */
    int from;
    size_t len;
    uint8_t data[1400];
} packet;

static packet packets[MAX_PACKETS];
static int count;
static uint32_t now, rng = 4321;
static g64_ihost *host;
static g64_iclient *player[PLAYERS];
static int ids[PLAYERS];
static int failed;

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void queue(int from, int to, const uint8_t *data, size_t len) {
    packet *p;
    if ((int)(rnd() % 1000) < LOSS || count >= MAX_PACKETS || len > sizeof p->data)
        return;
    p = &packets[count++];
    p->at = now + LATENCY;
    p->to = to;
    p->from = from;
    p->len = len;
    memcpy(p->data, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    queue(-1, to, data, len);
}

static void player_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)to;
    queue(*(int *)user, -1, data, len);
}

static void deliver(void) {
    int i, kept = 0;
    for (i = 0; i < count; i++) {
        packet *p = &packets[i];
        if (p->at > now) {
            packets[kept++] = *p;
            continue;
        }
        if (p->to < 0)
            g64_ihost_receive(host, p->from, p->data, p->len);
        else
            g64_iclient_receive(player[p->to], p->data, p->len);
    }
    count = kept;
}

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* A player's controller: A presses close the opening tip, then a walk that changes every 40 frames. */
static rb_input controller(int p, uint32_t f) {
    static const signed char dirs[4][2] = {{0, 60}, {60, 0}, {0, -60}, {-60, 0}};
    rb_input in;
    memset(&in, 0, sizeof in);
    if (f < 600) {
        in.buttons = f % 30 == 0 ? 0x8000 : 0;
    } else {
        in.x = dirs[(f / 40 + (uint32_t)p) % 4][0];
        in.y = dirs[(f / 40 + (uint32_t)p) % 4][1];
    }
    in.present = 1;
    return in;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    void *boot;
    int p;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (instances ROM)\n");
        return 0;
    }
    fclose(rom);
    /* The schedule of exits, in every copy of the game: it is set before the state after power-on is saved. */
    g64TestExits[0] = (G64TestExit){HUB1, 700, 1, ATLANTIS_EXIT};
    g64TestExits[1] = (G64TestExit){HUB1, 900, 2, ATLANTIS_EXIT};
    g64TestExits[2] = (G64TestExit){WAYROOM, 400, 1, HUB_EXIT};
    engine_default_options(&opt);
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(e));
    if (!boot) {
        printf("FAIL out of memory\n");
        return 1;
    }
    engine_save(e, boot);
    host = g64_ihost_new(e, boot, PLAYERS, HUB1, DEADLINE, host_send, NULL);
    for (p = 0; p < PLAYERS; p++) {
        ids[p] = p;
        player[p] = g64_iclient_new(e, boot, PLAYERS, p, HUB1, WINDOW, DEADLINE, player_send, &ids[p]);
    }
    check(host && player[0] && player[PLAYERS - 1], "the host and the players start");
    if (!host || !player[0] || !player[PLAYERS - 1])
        return 1;
    for (now = 0; now < FRAMES; now++) {
        deliver();
        g64_ihost_tick(host);
        for (p = 0; p < PLAYERS; p++)
            g64_iclient_step(player[p], controller(p, g64_iclient_frame(player[p])));
        if ((now + 1) % 300 == 0) {
            printf("frame %u: %d instances; levels", now + 1, g64_ihost_instances(host));
            for (p = 0; p < PLAYERS; p++)
                printf(" %d", g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]));
            printf("; %u hash checks, %u mismatches\n", g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host));
        }
    }
    check(g64_iclient_level(player[0]) == HUB1 && g64_iclient_level(player[1]) == HUB1 &&
              g64_iclient_level(player[2]) == WAYROOM && g64_iclient_level(player[3]) == HUB1,
          "players 0, 1 and 3 are in the hub, player 2 in the way room");
    check(g64_ihost_level_of(host, 1) == HUB1 && g64_ihost_level_of(host, 2) == WAYROOM && g64_ihost_instances(host) == 2,
          "the host agrees: two instances");
    for (p = 0; p < PLAYERS; p++) {
        char what[160];
        snprintf(what, sizeof what, "player %d: not moving, %u frames confirmed, %u desyncs", p,
                 g64_iclient_confirmed(player[p]), g64_iclient_desyncs(player[p]));
        check(!g64_iclient_moving(player[p]) && g64_iclient_desyncs(player[p]) == 0 &&
                  g64_iclient_confirmed(player[p]) > 100,
              what);
    }
    {
        char what[160];
        snprintf(what, sizeof what, "%u hash checks over every instance, %u mismatches", g64_ihost_hash_matches(host),
                 g64_ihost_hash_mismatches(host));
        check(g64_ihost_hash_matches(host) > 300 && g64_ihost_hash_mismatches(host) == 0, what);
    }
    printf("%s players move between instances\n", failed ? "FAIL" : "ok  ");
    for (p = 0; p < PLAYERS; p++)
        g64_iclient_free(player[p]);
    g64_ihost_free(host);
    free(boot);
    engine_free(e);
    return failed;
}
