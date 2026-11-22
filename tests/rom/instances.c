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
 * A second run tests a game that players join after its start (join_later), and a third the rule of one world at
 * a time (one_world).
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
        else if (player[p->to])
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

extern unsigned g64PlayerColor[16][2];

/* A game that players join after its start. The host's player starts alone in Atlantis 2. Player 1 comes at frame
 * 100 and player 2 at frame 300, each with colors of their own: they have no game at first, ask the host for one,
 * and join the running instance after its log. Player 1 leaves at frame 600, and at frame 700 another player takes
 * the place, with other colors. Place 3 stays empty. The test: everyone ends in the one instance with no desync and
 * no hash mismatch, and every game has the colors of the players who are in it now. */
static void join_later(Engine *e, const void *boot) {
    enum { ATLANTIS2 = 11, RUN = 1300 };
    static const uint32_t glove[3] = {0x1ff0000u, 0x100ff00u, 0x10000ffu}, ball[3] = {0x1ffff00u, 0x1ff00ffu, 0x100ffffu};
    uint32_t none[16][2];
    int p, before = failed;
    memset(none, 0, sizeof none);
    count = 0;
    for (p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, PLAYERS, 1u, none, ATLANTIS2, DEADLINE, host_send, NULL);
    player[0] = g64_iclient_new_with(e, boot, PLAYERS, 1u, none, 0, ATLANTIS2, WINDOW, DEADLINE, player_send, &ids[0]);
    check(host && player[0], "a host starts a game alone");
    if (!host || !player[0])
        return;
    for (now = 0; now < RUN; now++) {
        int who = now == 100 ? 1 : now == 300 ? 2 : now == 700 ? 1 : 0;
        if (now == 600) {
            /* Player 1 leaves. */
            g64_ihost_drop(host, 1);
            g64_iclient_free(player[1]);
            player[1] = NULL;
        }
        if (who) {
            /* A player comes: the host has their colors from the session, and their program has no game yet. */
            int c = now == 700 ? 2 : who - 1;
            g64_ihost_set_colors(host, who, glove[c], ball[c]);
            player[who] = g64_iclient_new_with(e, boot, PLAYERS, 1u, none, who, ATLANTIS2, WINDOW, DEADLINE, player_send,
                                               &ids[who]);
        }
        deliver();
        g64_ihost_tick(host);
        for (p = 0; p < PLAYERS; p++)
            if (player[p])
                g64_iclient_step(player[p], controller(p, g64_iclient_frame(player[p])));
        if ((now + 1) % 325 == 0) {
            printf("frame %u: %d instances; levels", now + 1, g64_ihost_instances(host));
            for (p = 0; p < PLAYERS; p++)
                printf(" %d", !player[p] || !g64_iclient_game(player[p]) ? -1 : g64_iclient_level(player[p]));
            printf("; %u hash checks, %u mismatches\n", g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host));
        }
    }
    check(g64_ihost_instances(host) == 1 && g64_ihost_level_of(host, 1) == ATLANTIS2 &&
              g64_ihost_level_of(host, 2) == ATLANTIS2 && g64_ihost_level_of(host, 3) < 0,
          "the host has one instance with players 0, 1 and 2, and nobody in place 3");
    for (p = 0; p < 3; p++) {
        char what[200];
        rb_engine_game *g = g64_iclient_game(player[p]);
        unsigned c[4] = {0, 0, 0, 0};
        if (g) {
            rb_engine_game_enter(g);
            c[0] = g64PlayerColor[1][0], c[1] = g64PlayerColor[1][1];
            c[2] = g64PlayerColor[2][0], c[3] = g64PlayerColor[2][1];
        }
        snprintf(what, sizeof what, "player %d: level %d, %u frames confirmed, %u desyncs; place 1's colors %07x %07x, "
                 "place 2's %07x %07x", p, g ? g64_iclient_level(player[p]) : -1, g64_iclient_confirmed(player[p]),
                 g64_iclient_desyncs(player[p]), c[0], c[1], c[2], c[3]);
        check(g && !g64_iclient_moving(player[p]) && g64_iclient_level(player[p]) == ATLANTIS2 &&
                  g64_iclient_desyncs(player[p]) == 0 && g64_iclient_confirmed(player[p]) > RUN - 100 &&
                  c[0] == glove[2] && c[1] == ball[2] && c[2] == glove[1] && c[3] == ball[1],
              what);
    }
    {
        char what[160];
        snprintf(what, sizeof what, "%u hash checks, %u mismatches", g64_ihost_hash_matches(host),
                 g64_ihost_hash_mismatches(host));
        check(g64_ihost_hash_matches(host) > 100 && g64_ihost_hash_mismatches(host) == 0, what);
    }
    printf("%s players join a game that has started\n", failed != before ? "FAIL" : "ok  ");
    for (p = 0; p < PLAYERS; p++)
        g64_iclient_free(player[p]);
    g64_ihost_free(host);
}

int G64PortalShut(int player);
int G64SessionWorld(void);

/* One world at a time. Three players start in the hub, with a schedule of exits of its own:
 *   - at frame 100 player 1 walks into the Atlantis portal and player 2 into the Carnival portal. Neither game has
 *     heard of the other, so both take their exit. The host lets the first one go and puts the second back in the
 *     hub: the session is in Atlantis now;
 *   - at frame 300 player 2 walks into the Carnival portal again: it is shut in their game, and they stay;
 *   - at frame 400 player 2 walks into the Atlantis portal: it is open, and they join player 1's way room;
 *   - at frame 600 players 1 and 2 choose "Back to the hub": the session is in no world again;
 *   - at frame 900 player 0 walks into the Carnival portal: everyone is in the hub, so it is open;
 *   - at frame 1000 player 1 walks into the Atlantis portal: it is shut, since the session is in Carnival. */
static void one_world(Engine *e, void *boot) {
    enum { CARNIVAL = 2, CARNIVAL_EXIT = 7, RUN = 1200, THREE = 3 };
    uint32_t none[16][2];
    int p, before = failed, shut300 = 0, shut1000 = 0, level150[THREE] = {-1, -1, -1}, world150 = -1;
    int level500[THREE] = {-1, -1, -1};
    memset(none, 0, sizeof none);
    /* The schedule goes into the state after power-on, which every game of this run is built from. */
    engine_load(e, boot);
    for (p = 0; p < 8; p++)
        g64TestExits[p] = (G64TestExit){-1, 0, 0, 0};
    g64TestExits[0] = (G64TestExit){HUB1, 100, 1, ATLANTIS_EXIT};
    g64TestExits[1] = (G64TestExit){HUB1, 100, 2, CARNIVAL_EXIT};
    g64TestExits[2] = (G64TestExit){HUB1, 300, 2, CARNIVAL_EXIT};
    g64TestExits[3] = (G64TestExit){HUB1, 400, 2, ATLANTIS_EXIT};
    g64TestExits[4] = (G64TestExit){HUB1, 900, 0, CARNIVAL_EXIT};
    g64TestExits[5] = (G64TestExit){HUB1, 1000, 1, ATLANTIS_EXIT};
    engine_save(e, boot);
    g64_instances_one_world(1);
    count = 0;
    for (p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, THREE, 7u, none, HUB1, DEADLINE, host_send, NULL);
    for (p = 0; p < THREE; p++)
        player[p] = g64_iclient_new_with(e, boot, THREE, 7u, none, p, HUB1, WINDOW, DEADLINE, player_send, &ids[p]);
    check(host && player[0] && player[1] && player[2], "a host and two players start in the hub, one world at a time");
    if (!host || !player[0] || !player[1] || !player[2])
        return;
    for (now = 0; now < RUN; now++) {
        deliver();
        g64_ihost_tick(host);
        for (p = 0; p < THREE; p++) {
            rb_input in;
            memset(&in, 0, sizeof in);
            in.present = 1;
            /* "Back to the hub" in the pause menu, as netplay.c sends it. It is held for a while: this network
               is slow for the host's deadline, and a player who joined a running level runs a little behind the
               host's clock, so their first inputs may come too late. */
            if (p > 0 && now >= 600 && now < 612) {
                in.buttons = 0x0080;
                in.x = 2;
            }
            g64_iclient_step(player[p], in);
        }
        for (p = 1; p < THREE; p++) {
            /* The shut portal, in the player's own game. */
            rb_engine_game *g = g64_iclient_game(player[p]);
            if (g && now > 280 && now < 340 && p == 2) {
                rb_engine_game_enter(g);
                if (G64PortalShut(2))
                    shut300 = G64PortalShut(2);
            }
            if (g && now > 980 && now < 1040 && p == 1) {
                rb_engine_game_enter(g);
                if (G64PortalShut(1))
                    shut1000 = G64PortalShut(1);
            }
        }
        if (now == 250 || now == 520) {
            for (p = 0; p < THREE; p++)
                (now == 250 ? level150 : level500)[p] =
                    g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]);
            if (now == 250)
                world150 = g64_ihost_world(host);
        }
        if ((now + 1) % 300 == 0) {
            printf("frame %u: %d instances, world %d; levels", now + 1, g64_ihost_instances(host), g64_ihost_world(host));
            for (p = 0; p < THREE; p++)
                printf(" %d", g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]));
            printf("; %u hash checks, %u mismatches\n", g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host));
        }
    }
    {
        char what[200];
        snprintf(what, sizeof what, "two portals at once: the levels are %d %d %d and the session's world is %d",
                 level150[0], level150[1], level150[2], world150);
        check(level150[0] == HUB1 && level150[1] == WAYROOM && level150[2] == HUB1 && world150 == ATLANTIS, what);
        snprintf(what, sizeof what, "the Carnival portal is shut for player 2 while the session is in world %d", shut300);
        check(shut300 == ATLANTIS, what);
        snprintf(what, sizeof what, "the Atlantis portal is open for player 2: the levels are %d %d %d", level500[0],
                 level500[1], level500[2]);
        check(level500[0] == HUB1 && level500[1] == WAYROOM && level500[2] == WAYROOM, what);
        snprintf(what, sizeof what, "with everyone in the hub the Carnival portal is open for player 0: the levels are "
                 "%d %d %d and the session's world is %d", g64_iclient_level(player[0]), g64_iclient_level(player[1]),
                 g64_iclient_level(player[2]), g64_ihost_world(host));
        check(g64_iclient_level(player[0]) == WAYROOM && g64_iclient_level(player[1]) == HUB1 &&
                  g64_iclient_level(player[2]) == HUB1 && g64_ihost_world(host) == CARNIVAL,
              what);
        snprintf(what, sizeof what, "the Atlantis portal is shut for player 1 while the session is in world %d", shut1000);
        check(shut1000 == CARNIVAL, what);
    }
    for (p = 0; p < THREE; p++) {
        char what[160];
        snprintf(what, sizeof what, "player %d: not moving, %u frames confirmed, %u desyncs", p,
                 g64_iclient_confirmed(player[p]), g64_iclient_desyncs(player[p]));
        check(!g64_iclient_moving(player[p]) && g64_iclient_desyncs(player[p]) == 0 &&
                  g64_iclient_confirmed(player[p]) > 100,
              what);
    }
    {
        char what[160];
        snprintf(what, sizeof what, "%u hash checks, %u mismatches", g64_ihost_hash_matches(host),
                 g64_ihost_hash_mismatches(host));
        check(g64_ihost_hash_matches(host) > 100 && g64_ihost_hash_mismatches(host) == 0, what);
    }
    printf("%s one world at a time\n", failed != before ? "FAIL" : "ok  ");
    for (p = 0; p < THREE; p++)
        g64_iclient_free(player[p]);
    g64_ihost_free(host);
    g64_instances_one_world(0);
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
    join_later(e, boot);
    one_world(e, boot);
    free(boot);
    engine_free(e);
    return failed;
}
