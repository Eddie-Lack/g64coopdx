/* Instances with the real game (src/client/instances.c), with the ROM.
 *
 * A host and three players run in one process over a simulated network that delays and loses messages. All start in
 * the first hub section. The game's test exits (g64TestExits) then move them, the same way on every machine:
 *   - player 1 takes the Atlantis door: a new instance of the way room starts for them alone;
 *   - player 2 takes the same door later: they join the way room's running instance, after its log;
 *   - player 1 takes the way room's first door to Atlantis 1, a new instance;
 *   - player 1 finishes Atlantis 1 (a world exit that completes it): the progress changes (the level done, the hub's
 *     target raised, the world done). They join the hub's running instance; the host sends the new progress to it as
 *     a session event on the frame they join at. The raised target opens the hub's cave door, and player 1 goes
 *     through it to the cave, a new instance. There the crystal flies to the fountain: the hub moves on to
 *     its second stage (HUB2);
 *   - meanwhile the way room, where player 2 still is, gets the progress as an event: its first door opens at once;
 *   - player 2 chooses "Back to the hub" in their pause menu: the choice goes in their input. They join HUB1, after
 *     its log, which holds the progress event;
 *   - player 1 chooses "Back to the hub" in the cave: they go to HUB2, a new instance. The hub players get the new
 *     stage as an event;
 *   - player 3 chooses "Back to the hub" in HUB1 on the same frame. Their game still says HUB1, but the host sends
 *     them to the newest stage: they join player 1 in HUB2.
 * The test: players 1 and 3 end up in HUB2 and the others in HUB1, every player's game has the new progress, the way
 * room's door opened, every message fits in a datagram, every instance's hash checks pass, and no player is told of a
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
int G64PlatformPosition(int tag, float *x, float *y, float *z);
extern G64TestExit g64TestExits[8];
typedef struct {
    uint32_t levels;
    uint32_t allGaribs;
    uint8_t hubTarget;
    uint8_t hubLevel;
    uint8_t worldComplete;
    uint8_t pad;
} G64Save;
void G64ReadSave(G64Save *save);

/* enum Level, enum World and enum ExitType (weemitt/include/enums.h). */
enum { HUB1 = 0, HUB2 = 1, ATLANTIS1 = 10, WAYROOM = 42, ATLANTIS = 1, WORLD_EXIT = 3, ATLANTIS_EXIT = 6, CAVE_EXIT = 12, LEVEL_1_EXIT = 16 };

#define PLAYERS 4
#define FRAMES 2400
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
/* Messages longer than an RB message's most (RB_MAX_WIRE) and its instance header. */
static int oversize;

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void queue(int from, int to, const uint8_t *data, size_t len) {
    packet *p;
    oversize += len > RB_MAX_WIRE + 2;
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
    /* Players 1 and 3 choose on the same frame: the host takes both exits in the same frame, player 3's first,
       before HUB1 can hear of the new stage. */
    if ((p == 1 && now >= 2000 && now < 2003) || (p == 3 && now >= 2000 && now < 2003) ||
        (p == 2 && now >= 1400 && now < 1403)) {
        /* The pause menu's choice, as netplay.c sends it: G64_ACTION_BUTTON with the choice in x. */
        in.buttons = 0x0080;
        in.x = 2;
    } else if (f < 600) {
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
    float door[2] = {0, 0};
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
    g64TestExits[2] = (G64TestExit){WAYROOM, 400, 1, LEVEL_1_EXIT};
    g64TestExits[3] = (G64TestExit){ATLANTIS1, 200, 1, WORLD_EXIT};
    g64TestExits[4] = (G64TestExit){HUB1, 1340, 1, CAVE_EXIT};
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
        if (now == 1000 || now == 1390) {
            /* The way room's first door, in player 2's game: shut before Atlantis 1 is done, gone (moved to y -5000)
               once the progress event has reached the way room. */
            float x = 0, y = 0, z = 0;
            rb_engine_game *g = g64_iclient_game(player[2]);
            if (g && g64_iclient_level(player[2]) == WAYROOM) {
                rb_engine_game_enter(g);
                G64PlatformPosition(1, &x, &y, &z);
            }
            door[now == 1390] = y;
        }
        if ((now + 1) % 300 == 0) {
            printf("frame %u: %d instances; levels", now + 1, g64_ihost_instances(host));
            for (p = 0; p < PLAYERS; p++)
                printf(" %d", g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]));
            printf("; %u hash checks, %u mismatches\n", g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host));
        }
    }
    check(g64_iclient_level(player[0]) == HUB1 && g64_iclient_level(player[1]) == HUB2 &&
              g64_iclient_level(player[2]) == HUB1 && g64_iclient_level(player[3]) == HUB2,
          "players 1 and 3 are in HUB2, the others in HUB1");
    check(g64_ihost_level_of(host, 1) == HUB2 && g64_ihost_level_of(host, 3) == HUB2 && g64_ihost_instances(host) == 2,
          "the host agrees: two instances");
    {
        char what[160];
        snprintf(what, sizeof what, "the way room's first door: y %.1f before the progress, %.1f after", door[0], door[1]);
        check(door[0] > -4000.0f && door[1] == -5000.0f, what);
        snprintf(what, sizeof what, "%d messages longer than a datagram may carry", oversize);
        check(oversize == 0, what);
    }
    for (p = 0; p < PLAYERS; p++) {
        /* The progress in the player's own game: player 0 got it as events while they played in HUB1, player 2 from
           HUB1's log and as an event, and players 1 and 3 in HUB2's start. */
        char what[160];
        G64Save s;
        rb_engine_game *g = g64_iclient_game(player[p]);
        memset(&s, 0, sizeof s);
        if (g) {
            rb_engine_game_enter(g);
            G64ReadSave(&s);
        }
        snprintf(what, sizeof what, "player %d's game has the new progress: levels %08x, hub target %u, hub level %u, "
                 "worlds done %02x", p, s.levels, s.hubTarget, s.hubLevel, s.worldComplete);
        check(g && (s.levels & 1) && s.hubTarget == 1 && s.hubLevel == 1 && (s.worldComplete >> ATLANTIS) & 1, what);
    }
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
