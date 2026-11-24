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
 * a time (one_world), a fourth a level pack (pack_level), and a fifth a pack's script (script_run). The sample pack
 * plays with its raft and a retail bell, and the client refuses it with other retail banks (sample_pack,
 * sample_pack_retail).
 *
 * Usage: instances ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "../../src/client/instances.h"
#include "../../src/client/levcheck.h"
#include "../../src/client/pack.h"
#include "../../src/client/packrom.h"
#include "../../src/client/rombanks.h"

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
    int p, before = failed, shut300 = 0, shut1000 = 0, level250[THREE] = {-1, -1, -1}, world250 = -1;
    int level500[THREE] = {-1, -1, -1}, world880 = -1;
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
    check(host && player[0] && player[1] && player[2], "a host and two more players start in the hub, one world at a time");
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
                (now == 250 ? level250 : level500)[p] =
                    g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]);
            if (now == 250)
                world250 = g64_ihost_world(host);
        }
        if (now == 880) {
            world880 = g64_ihost_world(host);
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
                 level250[0], level250[1], level250[2], world250);
        check(level250[0] == HUB1 && level250[1] == WAYROOM && level250[2] == HUB1 && world250 == ATLANTIS, what);
        snprintf(what, sizeof what, "the Carnival portal is shut for player 2 while the session is in world %d", shut300);
        check(shut300 == ATLANTIS, what);
        snprintf(what, sizeof what, "the Atlantis portal is open for player 2: the levels are %d %d %d", level500[0],
                 level500[1], level500[2]);
        check(level500[0] == HUB1 && level500[1] == WAYROOM && level500[2] == WAYROOM, what);
        snprintf(what, sizeof what, "with players 1 and 2 back in the hub the session is in world %d", world880);
        check(world880 == 0, what);
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

unsigned G64LevelFileRom(int num, unsigned *size);
int G64PlayerPosition(int p, float *x, float *y, float *z);

/* Where player 0's glove stands 60 frames into a game of level that the host's player starts alone. */
static void start_place(Engine *e, void *boot, int level, float at[3]) {
    uint32_t none[16][2];
    rb_engine_game *g;
    memset(none, 0, sizeof none);
    at[0] = at[1] = at[2] = 0;
    count = 0;
    for (int p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, 2, 1u, none, level, DEADLINE, host_send, NULL);
    player[0] = g64_iclient_new_with(e, boot, 2, 1u, none, 0, level, WINDOW, DEADLINE, player_send, &ids[0]);
    if (!host || !player[0])
        return;
    for (now = 0; now < 60; now++) {
        rb_input in;
        memset(&in, 0, sizeof in);
        in.present = 1;
        deliver();
        g64_ihost_tick(host);
        g64_iclient_step(player[0], in);
    }
    g = g64_iclient_game(player[0]);
    if (g) {
        rb_engine_game_enter(g);
        G64PlayerPosition(0, &at[0], &at[1], &at[2]);
    }
    printf("level %d: %u hash checks, %u mismatches, %u desyncs\n", level, g64_ihost_hash_matches(host),
           g64_ihost_hash_mismatches(host), g64_iclient_desyncs(player[0]));
    failed |= g64_ihost_hash_mismatches(host) != 0 || g64_iclient_desyncs(player[0]) != 0;
    g64_iclient_free(player[0]);
    player[0] = NULL;
    g64_ihost_free(host);
}

static float be_float(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void put_be_float(uint8_t *p, float f) {
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

/* A level pack: a level file that takes the place of one of the game's levels. The test has no level of its own to
 * put in a pack, and may not hold one made from the ROM. So it takes Atlantis 1's file from the ROM that is in
 * memory, moves the glove's start point in the copy 40 units along x, and puts the copy after the ROM's bytes as a pack's file. The glove must then start 40 units from where
 * the ROM's level puts it. */
static void pack_level(Engine *e, void *boot) {
    enum { SHIFT = 40 };
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    unsigned size = 0, at, i, found = 0;
    uint8_t *with;
    float one[3], packed[3];
    char what[200];
    int before = failed;
    engine_load(e, boot);
    at = G64LevelFileRom(ATLANTIS1, &size);
    check(at != 0 && size > 8 && at + size <= romSize, "the ROM has Atlantis 1's level file");
    if (at == 0 || size <= 8 || at + size > romSize)
        return;
    start_place(e, boot, ATLANTIS1, one);
    with = malloc(romSize + size);
    if (!with)
        return;
    memcpy(with, rom, romSize);
    memcpy(with + romSize, rom + at, size);
    /* The start point: three numbers, of which x and z are where the glove stands in the ROM's level. */
    for (i = 4; i + 12 <= size && !found; i++) {
        const uint8_t *c = with + romSize + i;
        if (be_float(c) > one[0] - 1 && be_float(c) < one[0] + 1 && be_float(c + 8) > one[2] - 1 &&
            be_float(c + 8) < one[2] + 1) {
            put_be_float(with + romSize + i, be_float(c) + SHIFT);
            found = 1;
        }
    }
    check(found, "the level file has the glove's start point");
    if (found) {
        engine_set_rom(e, with, romSize + size);
        g64_instances_pack(ATLANTIS1, (unsigned)romSize, size);
        start_place(e, boot, ATLANTIS1, packed);
        snprintf(what, sizeof what, "the glove starts at x %.0f in Atlantis 1, and at x %.0f with the pack", one[0],
                 packed[0]);
        check(packed[0] > one[0] + SHIFT - 2 && packed[0] < one[0] + SHIFT + 2 && packed[2] > one[2] - 2 &&
                  packed[2] < one[2] + 2,
              what);
        /* Without the pack again, the level is the ROM's own. */
        g64_instances_pack(-1, 0, 0);
        engine_set_rom(e, rom, romSize);
        start_place(e, boot, ATLANTIS1, packed);
        check(packed[0] == one[0] && packed[1] == one[1] && packed[2] == one[2],
              "without the pack Atlantis 1 is the ROM's own");
    }
    free(with);
    printf("%s a level pack's level takes a level's place\n", failed != before ? "FAIL" : "ok  ");
}

int G64LuaActive(void);
void G64LuaCounts(unsigned out[4]);
double G64LuaGlobal(const char *name);

/* Numbers that two_players reads from the script's global variables at the end. */
#define SCRIPT_VALUES 16
static const char *const scriptName[SCRIPT_VALUES] = {"joins",  "angles", "dice",   "balls", "counts", "frames",
                                                      "taken",  "deaths", "taker",  "healths", "vars",
                                                      "livesBefore", "livesAfter", "made", "picked", "picker"};
static double scriptValue[SCRIPT_VALUES];
/* Where a garib of the level is, read at the end of two_players. */
int G64GaribPlace(int k, float *x, float *y, float *z);
static float garibAt[3];

/* Two players in Atlantis 1 for 320 frames over the slow, lossy network, which makes their games go back and run
   frames again. Returns where player 0's glove is in their own game at the end, and prints the hash checks. */
static void two_players(Engine *e, void *boot, float at[3], unsigned counts[4], int *active) {
    uint32_t none[16][2];
    rb_engine_game *g;
    int p;
    memset(none, 0, sizeof none);
    at[0] = at[1] = at[2] = 0;
    memset(counts, 0, 4 * sizeof counts[0]);
    *active = 0;
    count = 0;
    for (p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, 2, 3u, none, ATLANTIS1, DEADLINE, host_send, NULL);
    for (p = 0; p < 2; p++)
        player[p] = g64_iclient_new_with(e, boot, 2, 3u, none, p, ATLANTIS1, WINDOW, DEADLINE, player_send, &ids[p]);
    if (!host || !player[0] || !player[1])
        return;
    for (now = 0; now < 320; now++) {
        deliver();
        g64_ihost_tick(host);
        for (p = 0; p < 2; p++) {
            rb_input in;
            memset(&in, 0, sizeof in);
            in.present = 1;
            /* Player 1 walks, so player 0's guesses about them are sometimes wrong. */
            if (p == 1)
                in.x = (int8_t)((now / 13) % 2 ? 60 : -60);
            g64_iclient_step(player[p], in);
        }
    }
    g = g64_iclient_game(player[0]);
    if (g) {
        rb_engine_game_enter(g);
        G64PlayerPosition(0, &at[0], &at[1], &at[2]);
        *active = G64LuaActive();
        G64LuaCounts(counts);
        for (p = 0; p < SCRIPT_VALUES; p++)
            scriptValue[p] = G64LuaGlobal(scriptName[p]);
        G64GaribPlace(0, &garibAt[0], &garibAt[1], &garibAt[2]);
    }
    printf("two players: %u hash checks, %u mismatches, desyncs %u and %u, %llu and %llu frames run again\n",
           g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host), g64_iclient_desyncs(player[0]),
           g64_iclient_desyncs(player[1]), (unsigned long long)g64_iclient_rollback_frames(player[0]),
           (unsigned long long)g64_iclient_rollback_frames(player[1]));
    failed |= g64_ihost_hash_mismatches(host) != 0 || g64_iclient_desyncs(player[0]) != 0 ||
              g64_iclient_desyncs(player[1]) != 0;
    for (p = 0; p < 2; p++) {
        g64_iclient_free(player[p]);
        player[p] = NULL;
    }
    g64_ihost_free(host);
}

/* The sample pack in packs/garib-island: a level of its own, made in Glovebox from an XML file written for it, with a
   script and models. It takes Atlantis 1's place. Its models.bin holds a raft drawn from scratch
   (packs/garib-island/models/make_raft.py) in the pack's own banks, and names Carnival's texture bank (4) and one of
   its object banks (20) for a bell. The level places the raft as scenery, the raft again as a platform that moves
   along z where the glove lands at the start (tag 200), and the bell as scenery. */
int G64LevelUnplayable(void);
const void *G64PackBankLoaded(int objects, unsigned *word);
void G64PackTableCounts(int *numTex, int *numObj);
int G64PackTableObjectIds(int i, unsigned *ids, int max);
int G64ObjectSlotIds(int slot, unsigned *ids, int max);
#define RAFT_ID 0x60CB35B6u
#define BELL_ID 0x45F8316Cu
#define RAFT_TAG 200

static void write_part(const char *dir, const char *name, const void *d, size_t n) {
    char path[300];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static void make_dir(const char *dir) {
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0700);
#endif
}

/* Loads a pack folder as the online program loads the player's own folder: its models pass the bank check and the
   rules of the ROM. */
static int load_checked(const char *dir, const uint8_t *rom, unsigned long romSize, g64_pack *pk, char *err,
                        size_t errSize) {
    g64_pack_banks banks;
    G64RomBanks *index = g64_rombanks_build_game(rom, romSize);
    int r;
    if (!index) {
        snprintf(err, errSize, "out of memory");
        return -1;
    }
    banks.exists = g64_rombanks_game_range;
    banks.user = NULL;
    banks.rom = index;
    r = g64_pack_load(dir, &banks, pk, err, errSize);
    g64_rombanks_free(index);
    return r;
}

/* Whether a slot of the game's five object slots, or the retail table, has an object id. */
static int slot_has(unsigned id) {
    static unsigned ids[4096];
    int s, i, n;
    for (s = 0; s < 5; s++) {
        n = G64ObjectSlotIds(s, ids, 4096);
        for (i = 0; i < n && i < 4096; i++)
            if (ids[i] == id)
                return 1;
    }
    return 0;
}

static int table_has(unsigned id) {
    static unsigned ids[4096];
    int i, n = G64PackTableObjectIds(0, ids, 4096);
    for (i = 0; i < n && i < 4096; i++)
        if (ids[i] == id)
            return 1;
    return 0;
}

/* Puts the pack's parts after the ROM's bytes and tells the game where they are, as netplay_begin does. Returns the
   buffer, which the caller frees after undo_pack. */
static uint8_t *use_pack(Engine *e, void *boot, const g64_pack *pk, const uint8_t *rom, unsigned long romSize) {
    g64_pack_layout l;
    uint8_t *with;
    g64_pack_layout_of(pk, romSize, &l);
    check(l.texAt % 8 == 0 && l.texAt >= l.scriptAt + pk->scriptSize && l.objAt == l.texAt + l.texSize &&
              l.size == l.objAt + l.objSize,
          "the buffer has the banks after the script, from a multiple of 8");
    with = malloc(l.size);
    if (!with)
        return NULL;
    memcpy(with, rom, romSize);
    g64_pack_layout_fill(pk, &l, with);
    engine_load(e, boot);
    engine_set_rom(e, with, l.size);
    g64_instances_pack(pk->level, (unsigned)l.levAt, (unsigned)pk->levSize);
    g64_instances_pack_script((unsigned)l.scriptAt, (unsigned)pk->scriptSize);
    g64_instances_pack_models((unsigned)l.texAt, (unsigned)l.texSize, (unsigned)l.objAt, (unsigned)l.objSize);
    g64_instances_pack_retail(pk->modelsInfo.flags & 1, pk->modelsInfo.tex, pk->modelsInfo.numTex,
                              pk->modelsInfo.obj, pk->modelsInfo.numObj);
    return with;
}

static void undo_pack(Engine *e, const uint8_t *rom, unsigned long romSize) {
    g64_instances_pack(-1, 0, 0);
    g64_instances_pack_script(0, 0);
    g64_instances_pack_models(0, 0, 0, 0);
    g64_instances_pack_retail(0, NULL, 0, NULL, 0);
    engine_set_rom(e, rom, romSize);
}

/* One player stands still in the level for 400 frames. Every 20 frames from frame 60 the test reads where the glove
   and the raft (tag 200) are. The raft moves along z where the glove lands, and the glove rides it: with no input,
   the glove's z changes with the raft's. Prints the raft's range of z and the glove's. */
static void ride_raft(Engine *e, void *boot) {
    uint32_t none[16][2];
    rb_engine_game *g;
    float raftLo = 1e9f, raftHi = -1e9f, gloveLo = 1e9f, gloveHi = -1e9f;
    int samples = 0, raft = 1, follows = 0, pairs = 0;
    float lastRaft = 0, lastGlove = 0;
    char what[200];
    memset(none, 0, sizeof none);
    count = 0;
    for (int p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, 2, 1u, none, ATLANTIS1, DEADLINE, host_send, NULL);
    player[0] = g64_iclient_new_with(e, boot, 2, 1u, none, 0, ATLANTIS1, WINDOW, DEADLINE, player_send, &ids[0]);
    if (!host || !player[0])
        return;
    for (now = 0; now < 400; now++) {
        rb_input in;
        memset(&in, 0, sizeof in);
        in.present = 1;
        deliver();
        g64_ihost_tick(host);
        g64_iclient_step(player[0], in);
        g = g64_iclient_game(player[0]);
        if (now >= 60 && now % 20 == 0 && g) {
            float rx, ry, rz, gx, gy, gz;
            rb_engine_game_enter(g);
            raft &= G64PlatformPosition(RAFT_TAG, &rx, &ry, &rz);
            G64PlayerPosition(0, &gx, &gy, &gz);
            if (rz < raftLo)
                raftLo = rz;
            if (rz > raftHi)
                raftHi = rz;
            if (gz < gloveLo)
                gloveLo = gz;
            if (gz > gloveHi)
                gloveHi = gz;
            /* The raft moved more than a unit since the last sample: the glove moved the same way, by about as much. */
            if (samples > 0 && (rz - lastRaft > 1 || rz - lastRaft < -1)) {
                float dr = rz - lastRaft, dg = gz - lastGlove;
                pairs++;
                follows += dg * dr > 0 && dg - dr < 1 && dr - dg < 1;
            }
            lastRaft = rz;
            lastGlove = gz;
            samples++;
        }
    }
    printf("the raft: %u hash checks, %u mismatches, %u desyncs\n", g64_ihost_hash_matches(host),
           g64_ihost_hash_mismatches(host), g64_iclient_desyncs(player[0]));
    failed |= g64_ihost_hash_mismatches(host) != 0 || g64_iclient_desyncs(player[0]) != 0;
    check(raft, "the level has the raft platform");
    snprintf(what, sizeof what, "the raft platform moves along z from %.0f to %.0f", raftLo, raftHi);
    check(raftHi - raftLo > 30, what);
    snprintf(what, sizeof what, "the glove rides the raft: z from %.0f to %.0f, with the raft in %d of %d samples",
             gloveLo, gloveHi, follows, pairs);
    check(pairs > 4 && follows == pairs, what);
    g64_iclient_free(player[0]);
    player[0] = NULL;
    g64_ihost_free(host);
}

/* The test: the pack loads and passes the check for packs from another player and the rules of the ROM, its
   models.bin holds the raft's two banks and the bell's two bank numbers, the game loads them, the raft is found in
   the pack's slot and the bell in the retail table, the raft platform moves and carries a glove, two players play
   the level over the slow, lossy network, its first garib is where the XML puts it, the script runs, and no machine
   disagrees. */
static void sample_pack(Engine *e, void *boot) {
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    g64_pack pk;
    char err[200];
    float at[3];
    unsigned counts[4], word = 0;
    int active = 0, before = failed, nt = 0, no = 0;
    uint8_t *with;
    if (load_checked("packs/garib-island", rom, romSize, &pk, err, sizeof err) != 0) {
        check(0, "the sample pack loads from packs/garib-island, and its models pass the rules of the ROM");
        printf("FAIL the sample pack: %s\n", err);
        return;
    }
    check(g64_pack_check_strict(&pk, NULL) == NULL && pk.level == ATLANTIS1 && pk.script != NULL,
          "the sample pack takes Atlantis 1's place, has a script and passes the check for packs from a host");
    check(g64_pack_rom_level(&pk, rom, romSize) < 0, "the sample pack's level is not one of the ROM's own");
    check(pk.models && pk.modelsInfo.texSize > 0 && pk.modelsInfo.objSize > 0 && pk.modelsInfo.numTex == 1 &&
              pk.modelsInfo.tex[0] == 4 && pk.modelsInfo.numObj == 1 && pk.modelsInfo.obj[0] == 20 &&
              pk.modelsInfo.flags == 0,
          "the pack holds the raft's two banks, and names texture bank 4 and object bank 20 for the bell");
    {
        size_t atRule = 0;
        int rule = -1;
        const char *why = g64_pack_check_ids_rom(&pk, rom, romSize, &rule, &atRule);
        check(why == NULL && rule == 0, "the client accepts the level's objects and the retail banks' sprites");
    }
    with = use_pack(e, boot, &pk, rom, romSize);
    if (!with) {
        g64_pack_free(&pk);
        return;
    }
    ride_raft(e, boot);
    two_players(e, boot, at, counts, &active);
    /* two_players leaves player 0's game in the globals. */
    check(!G64LevelUnplayable(), "the sample pack's level plays");
    check(active, "the sample pack's script runs");
    check(G64PackBankLoaded(0, &word) != NULL && G64PackBankLoaded(1, &word) != NULL,
          "the game loaded the pack's texture bank and object bank");
    G64PackTableCounts(&nt, &no);
    check(nt == 1 && no == 1, "the game loaded the retail texture bank and object bank into its table");
    check(slot_has(RAFT_ID) && !table_has(RAFT_ID), "the raft is found in the pack's object slot");
    check(table_has(BELL_ID) && !slot_has(BELL_ID), "the bell is found through the retail table, in no slot");
    /* The XML's row of garibs: z from 200 to 600 in steps of 40, x 40 or -40 in pairs, y 30. */
    {
        int k, row = -1;
        for (k = 0; k <= 10; k++)
            if (garibAt[2] == 200.0f + 40.0f * k)
                row = k;
        snprintf(err, sizeof err, "a garib of the level is at (%.0f, %.0f, %.0f), a place of the XML's row",
                 garibAt[0], garibAt[1], garibAt[2]);
        check(row >= 0 && garibAt[1] == 30.0f && garibAt[0] == ((row / 2) % 2 == 0 ? 40.0f : -40.0f), err);
    }
    undo_pack(e, rom, romSize);
    free(with);
    g64_pack_free(&pk);
    printf("%s the sample pack Garib Island plays with two players, with its raft and the bell\n",
           failed != before ? "FAIL" : "ok  ");
}

/* Writes a models.bin with the sample pack's own two banks and other retail numbers (model-banks.md, "The file").
   Returns its size, or 0. */
static size_t models_with(const g64_pack *pk, const uint8_t *tex, int nt, const uint8_t *obj, int no, uint8_t *out,
                          size_t cap) {
    size_t t = pk->modelsInfo.texSize, o = pk->modelsInfo.objSize, h = (12 + nt + no + 7) & ~(size_t)7;
    if (!pk->models || h + t + o > cap)
        return 0;
    memset(out, 0, h);
    memcpy(out, pk->models, 8);
    out[9] = (uint8_t)nt;
    out[10] = (uint8_t)no;
    memcpy(out + 12, tex, (size_t)nt);
    memcpy(out + 12 + nt, obj, (size_t)no);
    memcpy(out + h, pk->models + pk->modelsInfo.texAt, t);
    memcpy(out + h + t, pk->models + pk->modelsInfo.objAt, o);
    return h + t + o;
}

/* The sample pack with other retail numbers in its models.bin, read through the pack reader as a folder. Object bank
   19 (Carnival's world bank) has sprites that need Carnival's textures. Without texture bank 4 the client refuses it
   (rule 2), as the game would stop the level. Without object bank 20 the bell is in no bank, and the client refuses
   the level's objects (rule 1). */
static void sample_pack_retail(Engine *e) {
    static const char dir[] = "build/tests/retail-pack";
    static uint8_t buf[1 << 16];
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    g64_pack pk, alt;
    char err[200];
    int before = failed;
    if (load_checked("packs/garib-island", rom, romSize, &pk, err, sizeof err) != 0) {
        check(0, "the sample pack loads");
        return;
    }
    make_dir(dir);
    write_part(dir, "pack.txt", pk.manifest, pk.manifestSize);
    write_part(dir, "level.lev", pk.lev, pk.levSize);
    write_part(dir, "script.lua", pk.script, pk.scriptSize);
    {
        static const uint8_t obj[2] = {19, 20};
        size_t n = models_with(&pk, NULL, 0, obj, 2, buf, sizeof buf), atRule = 0;
        int rule = -1;
        write_part(dir, "models.bin", buf, n);
        if (n == 0 || load_checked(dir, rom, romSize, &alt, err, sizeof err) != 0) {
            check(0, "the pack with object banks 19 and 20 and no texture bank loads");
        } else {
            const char *why = g64_pack_check_ids_rom(&alt, rom, romSize, &rule, &atRule);
            check(why != NULL && rule == 2 && atRule == 19,
                  "the client refuses the pack with object bank 19 and no texture bank: its sprites' textures are "
                  "not loaded");
            g64_pack_free(&alt);
        }
    }
    {
        static const uint8_t tex[1] = {4};
        size_t n = models_with(&pk, tex, 1, NULL, 0, buf, sizeof buf), atRule = 0;
        int rule = -1;
        write_part(dir, "models.bin", buf, n);
        if (n == 0 || load_checked(dir, rom, romSize, &alt, err, sizeof err) != 0) {
            check(0, "the pack without object bank 20 loads");
        } else {
            const char *why = g64_pack_check_ids_rom(&alt, rom, romSize, &rule, &atRule);
            check(why != NULL && rule == 1, "the client refuses the pack without object bank 20: the bell is in no bank");
            g64_pack_free(&alt);
        }
    }
    g64_pack_free(&pk);
    printf("%s the sample pack with other retail banks is refused\n", failed != before ? "FAIL" : "ok  ");
}

/* A level pack's script. It counts its frames and, every 50 frames, moves player 0's glove 5 units along x. Its
   state is in the game's savestate: the games go back and run frames again, and the script's count must go back
   with them. The test: with the script the glove ends 30 units from where it ends without (6 moves), no machine
   disagrees, and the script's memory counts are the same on Linux and Windows (the outputs are compared). A second
   script runs without end: it is stopped, the same on every machine, and the game goes on. */
static void script_run(Engine *e, void *boot) {
    static const char script[] =
        "local moves = 0\n"
        "local seen = {}\n"
        "function on_frame()\n"
        "  local f = game.frame()\n"
        "  seen[#seen + 1] = {f}\n"
        "  if f > 0 and f % 50 == 0 and game.has_player(0) then\n"
        "    local x, y, z = game.pos(0)\n"
        "    game.place(0, x + 5, y, z)\n"
        "    moves = moves + 1\n"
        "  end\n"
        "end\n";
    static const char endless[] = "function on_frame() while true do end end\n";
    /* The calls a script has: it adds up what they give, in global variables that the test reads. The sums come
       from the game's own sine and cosine, the script's random numbers and the players' places and counts. */
    static const char calls[] =
        "joins, angles, dice, balls, counts, frames = 0, 0, 0, 0, 0, 0\n"
        "function on_join(p) joins = joins + 1 + p * 10 end\n"
        "function on_frame()\n"
        "  local f = game.frame()\n"
        "  frames = frames + 1\n"
        "  angles = angles + math.sin(f * 0.37) * math.cos(f * 1.3 - 40)\n"
        "  dice = dice + game.random(6) + game.random()\n"
        "  for p = 0, 15 do\n"
        "    if game.has_player(p) then\n"
        "      local x, y, z = game.ball_pos(p)\n"
        "      local gx = game.pos(p)\n"
        "      balls = balls + x + y + z - gx\n"
        "      counts = counts + game.ball_type(p) + game.garibs(p) * 7 + game.health(p) * 31 + game.lives(p) * 101\n"
        "    end\n"
        "  end\n"
        "end\n";
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    /* Events from the game's step. The script puts player 1's glove on a garib at frame 60, and far below the level
       at frame 150. It must hear of the garib, from player 1, and of the death. */
    static const char eventsText[] =
        "taken, deaths, taker, healths, vars = 0, 0, -1, 0, 0\n"
        "function on_garib(p) taken = taken + 1; taker = p end\n"
        "function on_death(p) deaths = deaths + 1 + p * 10 end\n"
        "function on_frame()\n"
        "  local f = game.frame()\n"
        "  if f == 60 and game.has_player(1) then game.place(1, %.3f, %.3f, %.3f) end\n"
        "  if f == 150 and game.has_player(1) then game.place(1, %.3f, %.3f, %.3f) end\n"
        "  -- Player 0: hurt at frame 100, read at 101; healed at 102, read at 103: 2 * 10 + 3.\n"
        "  if f == 100 then game.hurt(0) end\n"
        "  if f == 101 then healths = game.health(0) * 10 end\n"
        "  if f == 102 then game.set_health(0, 3) end\n"
        "  if f == 103 then healths = healths + game.health(0) end\n"
        "  if f %% 7 == 0 then game.show(f %% 2, 'frame ' .. f, 5) end\n"
        "  if f == 110 then vars = game.var(60) * 100; game.set_var(60, 9) end\n"
        "  if f == 130 then vars = vars + game.var(60) end\n"
        "end\n";
    char events[sizeof eventsText + 200];
    uint8_t *with = malloc(romSize + sizeof script + sizeof endless + sizeof calls + sizeof events);
    float plain[3], moved[3], after[3];
    unsigned counts[4], none[4];
    int active = 0, before = failed;
    char what[240];
    if (!with)
        return;
    g64_instances_pack_script(0, 0);
    two_players(e, boot, plain, none, &active);
    check(!active && none[2] == 0, "without a script no script runs");
    memcpy(with, rom, romSize);
    memcpy(with + romSize, script, sizeof script - 1);
    memcpy(with + romSize + sizeof script, endless, sizeof endless - 1);
    memcpy(with + romSize + sizeof script + sizeof endless, calls, sizeof calls - 1);
    engine_set_rom(e, with, romSize + sizeof script + sizeof endless + sizeof calls + sizeof events);
    g64_instances_pack_script((unsigned)romSize, sizeof script - 1);
    two_players(e, boot, moved, counts, &active);
    snprintf(what, sizeof what, "the glove ends at x %.1f without the script and at x %.1f with it", plain[0], moved[0]);
    check(active && moved[0] > plain[0] + 29.0f && moved[0] < plain[0] + 31.0f, what);
    printf("script: its memory: high mark %u, in use %u, %u allocations, %u frees\n", counts[0], counts[1], counts[2],
           counts[3]);
    check(counts[2] > 300 && counts[1] > 0 && counts[1] < counts[0], "the script's memory is in use");
    g64_instances_pack_script((unsigned)(romSize + sizeof script), sizeof endless - 1);
    two_players(e, boot, after, counts, &active);
    snprintf(what, sizeof what, "a script without end is stopped: the glove ends at x %.1f, as without a script",
             after[0]);
    check(!active && after[0] == plain[0], what);
    g64_instances_pack_script((unsigned)(romSize + sizeof script + sizeof endless), sizeof calls - 1);
    two_players(e, boot, after, counts, &active);
    printf("script: joins %.0f, frames %.0f, angles %.9f, dice %.9f, balls %.6f, counts %.0f\n", scriptValue[0],
           scriptValue[5], scriptValue[1], scriptValue[2], scriptValue[3], scriptValue[4]);
    /* Players 0 and 1 joined: 1 + 11. The sums are nonzero, and the Linux and Windows outputs are compared. */
    check(active && scriptValue[0] == 12 && scriptValue[5] > 200 && scriptValue[1] != 0 && scriptValue[2] > 200 &&
              scriptValue[4] > 0,
          "a script reads the players, the game's sine and cosine and its random numbers");
    snprintf(events, sizeof events, eventsText, garibAt[0], garibAt[1], garibAt[2], garibAt[0], garibAt[1] - 3000.0f,
             garibAt[2]);
    memcpy(with + romSize + sizeof script + sizeof endless + sizeof calls, events, strlen(events));
    g64_instances_pack_script((unsigned)(romSize + sizeof script + sizeof endless + sizeof calls),
                              (unsigned)strlen(events));
    two_players(e, boot, after, counts, &active);
    printf("script: %.0f garibs taken, the last by player %.0f, deaths %.0f\n", scriptValue[6], scriptValue[8],
           scriptValue[7]);
    check(active && scriptValue[6] >= 1 && scriptValue[8] == 1 && scriptValue[7] == 11,
          "a script hears of a garib taken and of a death");
    snprintf(what, sizeof what, "a script hurts a glove and heals it: health 2 and then 3 (%.0f)", scriptValue[9]);
    check(scriptValue[9] == 23, what);
    check(scriptValue[10] == 9, "a script sets one of the level's puzzle variables, and it keeps the value");
    {
        /* A script gives player 1 a life at frame 40. At frame 50 it puts the first pick-up of the game's kinds
           that Atlantis 1's banks hold where player 1's glove is, and hears player 1 take it. */
        static const char gifts[] =
            "livesBefore, livesAfter, made, picked, picker = -1, -1, -1, 0, -1\n"
            "function on_pickup(p) picked = picked + 1; picker = p end\n"
            "function on_frame()\n"
            "  local f = game.frame()\n"
            "  if f == 40 and game.has_player(1) then livesBefore = game.lives(1); game.add_life(1) end\n"
            "  if f == 41 and game.has_player(1) then livesAfter = game.lives(1) end\n"
            "  if f == 50 then\n"
            "    for k = 0, 14 do\n"
            "      local x, y, z = game.pos(1)\n"
            "      if made < 0 and x and game.spawn_pickup(k, x, y, z) then made = k end\n"
            "    end\n"
            "  end\n"
            "end\n";
        uint8_t *withGifts = malloc(romSize + sizeof gifts);
        if (withGifts) {
            memcpy(withGifts, rom, romSize);
            memcpy(withGifts + romSize, gifts, sizeof gifts - 1);
            engine_set_rom(e, withGifts, romSize + sizeof gifts);
            g64_instances_pack_script((unsigned)romSize, sizeof gifts - 1);
            two_players(e, boot, after, counts, &active);
            snprintf(what, sizeof what, "a script gives a life (%.0f, then %.0f) and makes a pick-up of kind %.0f",
                     scriptValue[11], scriptValue[12], scriptValue[13]);
            check(active && scriptValue[11] >= 0 && scriptValue[12] == scriptValue[11] + 1 && scriptValue[13] >= 0,
                  what);
            snprintf(what, sizeof what, "a script hears of a pick-up taken: %.0f, by player %.0f", scriptValue[14],
                     scriptValue[15]);
            check(scriptValue[14] >= 1 && scriptValue[15] == 1, what);
            free(withGifts);
        }
    }
    g64_instances_pack_script(0, 0);
    engine_set_rom(e, rom, romSize);
    free(with);
    printf("%s a level pack's script runs with the game\n", failed != before ? "FAIL" : "ok  ");
}

/* A level pack whose level names an object the game does not have. The level file is Atlantis 1's from the ROM with
   the id of its first platform's object changed. The game's parser used what its lookup gave without a test, and
   such a file made it use a null pointer. Now the level is marked as one that cannot be played, and its players
   go back to the hub: two players start in it, and both must end in the hub, with no machine disagreeing. */
/* An enemy kind whose object Atlantis 1's banks do not hold. */
#define ENEMY_ELSEWHERE 50
static size_t firstPlatform, firstGroup, firstEnemy, firstSpin, cameoSteps[64], conditions[64];
static int cameoStepCount;
static int conditionCount;
static void find_platform(void *user, size_t offset, int token) {
    (void)user;
    if (token == 98 && firstPlatform == 0)
        firstPlatform = offset;
    if (token == 133 && firstGroup == 0)
        firstGroup = offset;
    if (token == 131 && firstEnemy == 0)
        firstEnemy = offset;
    if (token == 127 && firstSpin == 0)
        firstSpin = offset;
    if (token == 171 && cameoStepCount < 64)
        cameoSteps[cameoStepCount++] = offset;
    if (token == 149 && conditionCount < 64)
        conditions[conditionCount++] = offset;
}

/* which 0: the first platform's object is one the game does not have. which 1: the first group of garibs is gone
   (its token becomes COLL_OFFSET, which has fields of the same length and needs nothing), so the garibs after it
   come before any group. The parser kept its pointer to the last level's group, and wrote through it. which 2: the
   first enemy is of a kind whose object Atlantis 1's banks do not hold (a boss of another world). which 3: the first
   puzzle condition has a number the game does not have. which 4: every puzzle condition on a tag becomes "the
   platform's spin is at pause 1" (AT_SPIN). Most of the tagged platforms have no spin, and none has a pause: the
   game tested the spin without asking, and divided by its count of pauses. This level must go on. which 5: the
   first SPIN is on axis 7. The game wrote four of the file's bytes at the place the axis picks. which 6: every cameo
   instruction that names an actor names enemy 25, which the level does not have. The cameos do not start, and the
   level goes on. which 7: the first platform is DESTRUCTIBLE with pieces of an object that the level's banks do not
   hold. which 8: 1,300 platforms without an object come first. The game stopped the program when its list of memory
   blocks was full. */
static void unplayable_case(Engine *e, void *boot, int which) {
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    size_t at = 0, size = 0, bad = 0;
    uint8_t *with;
    uint32_t none[16][2];
    /* The cameo case plays the first hub level, which has cameos; the others play Atlantis 1. */
    int level = which == 6 ? HUB1 : ATLANTIS1;
    int p, before = failed, levels[2] = {-1, -1};
    char what[200];
    if (g64_pack_level_from_rom(rom, romSize, level, &at, &size) != NULL)
        return;
    /* Room for the tokens that two of the cases put into the file. */
    with = malloc(romSize + size + 4096);
    if (!with)
        return;
    memset(none, 0, sizeof none);
    memcpy(with, rom, romSize);
    memcpy(with + romSize, rom + at, size);
    firstPlatform = firstGroup = firstEnemy = firstSpin = 0;
    conditionCount = cameoStepCount = 0;
    g64_lev_check(with + romSize, size, find_platform, NULL, &bad);
    if (which != 6)
        check(firstPlatform != 0 && firstGroup != 0, "Atlantis 1 has a platform and a group of garibs");
    if (which == 7 || which == 8) {
        /* Tokens go into the file: a DESTRUCTIBLE with 5 pieces of an object the level does not have, after the
           first platform's own 12 bytes of fields, or 1,300 DUMMY platforms before the first token. The file's first
           word is its size. */
        uint8_t *lev = with + romSize;
        size_t where = which == 7 ? firstPlatform + 14 : 4 + strlen((const char *)lev + 4) + 1;
        size_t extra = which == 7 ? 20 : 2600, k;
        memmove(lev + where + extra, lev + where, size - where);
        memset(lev + where, 0, extra);
        if (which == 7) {
            lev[where + 1] = 101;
            lev[where + 7] = 5;
            lev[where + 8] = 0x12, lev[where + 9] = 0x34, lev[where + 10] = 0x56, lev[where + 11] = 0x78;
        } else {
            for (k = 0; k < extra; k += 2)
                lev[where + k + 1] = 93;
        }
        size += extra;
        lev[0] = (uint8_t)(size >> 24), lev[1] = (uint8_t)(size >> 16), lev[2] = (uint8_t)(size >> 8), lev[3] = (uint8_t)size;
        check(g64_lev_check(lev, size, NULL, NULL, &bad) == NULL, "the check before loading takes the file: it knows no banks and no memory");
    } else if (which == 0) {
        /* The object's id is the four bytes after the token. */
        with[romSize + firstPlatform + 2] ^= 0x5A;
        with[romSize + firstPlatform + 3] ^= 0xA5;
    } else if (which == 1) {
        with[romSize + firstGroup] = 0;
        with[romSize + firstGroup + 1] = 183;
        check(g64_lev_check(with + romSize, size, NULL, NULL, &bad) != NULL,
              "the check before loading refuses garibs without a group");
    } else if (which == 2) {
        check(firstEnemy != 0, "Atlantis 1 has an enemy");
        /* The enemy's type is the two bytes after the token. */
        with[romSize + firstEnemy + 2] = 0;
        with[romSize + firstEnemy + 3] = ENEMY_ELSEWHERE;
    } else if (which == 3) {
        check(conditionCount > 0, "Atlantis 1 has a puzzle condition");
        /* The condition's number is the two bytes after the token; the game has 9 to 44. Number 8 has fields of
           the same length as most. */
        with[romSize + conditions[0] + 2] = 0;
        with[romSize + conditions[0] + 3] = 8;
    } else if (which == 6) {
        int k, changed = 0;
        for (k = 0; k < cameoStepCount; k++) {
            uint8_t *c = with + romSize + cameoSteps[k];
            /* The instruction's type, then its target, for the types 0 to 4. */
            if (c[2] == 0 && c[3] <= 4) {
                c[4] = 0;
                c[5] = 25;
                changed++;
            }
        }
        check(changed > 0, "the first hub level has cameo instructions");
    } else if (which == 5) {
        check(firstSpin != 0, "Atlantis 1 has a SPIN");
        with[romSize + firstSpin + 2] = 0;
        with[romSize + firstSpin + 3] = 7;
        check(g64_lev_check(with + romSize, size, NULL, NULL, &bad) != NULL,
              "the check before loading refuses a spin on axis 7");
    } else {
        int k, changed = 0;
        for (k = 0; k < conditionCount; k++) {
            uint8_t *c = with + romSize + conditions[k];
            /* The conditions with a tag and a value have numbers below 34. Number 10 is AT_SPIN; the value 1. */
            if (c[2] == 0 && c[3] < 34) {
                c[3] = 10;
                c[6] = 0;
                c[7] = 1;
                changed++;
            }
        }
        check(changed > 0, "Atlantis 1 has puzzle conditions on tags");
    }
    engine_load(e, boot);
    for (p = 0; p < 8; p++)
        g64TestExits[p] = (G64TestExit){-1, 0, 0, 0};
    engine_save(e, boot);
    engine_set_rom(e, with, romSize + size);
    g64_instances_pack(level, (unsigned)romSize, (unsigned)size);
    count = 0;
    for (p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, 2, 3u, none, level, DEADLINE, host_send, NULL);
    for (p = 0; p < 2; p++)
        player[p] = g64_iclient_new_with(e, boot, 2, 3u, none, p, level, WINDOW, DEADLINE, player_send, &ids[p]);
    if (host && player[0] && player[1]) {
        for (now = 0; now < 400; now++) {
            deliver();
            g64_ihost_tick(host);
            for (p = 0; p < 2; p++) {
                rb_input in;
                memset(&in, 0, sizeof in);
                in.present = 1;
                g64_iclient_step(player[p], in);
            }
        }
        for (p = 0; p < 2; p++)
            levels[p] = g64_iclient_moving(player[p]) ? -1 : g64_iclient_level(player[p]);
        snprintf(what, sizeof what, "a level with %s %s: levels %d and %d, %u mismatches, desyncs %u and %u",
                 which == 0   ? "a missing object"
                 : which == 1 ? "garibs before any group"
                 : which == 2 ? "an enemy the level's banks do not hold"
                 : which == 3 ? "a puzzle condition the game does not have"
                 : which == 5 ? "a spin on an axis that is not there"
                 : which == 6 ? "cameos about an enemy that is not there"
                 : which == 7 ? "a broken platform's pieces of an object that is not there"
                 : which == 8 ? "more platforms than the game's memory has blocks for"
                              : "conditions on spins that are not there",
                 which == 4 || which == 6 ? "plays on" : "sends its players to the hub", levels[0], levels[1], g64_ihost_hash_mismatches(host), g64_iclient_desyncs(player[0]),
                 g64_iclient_desyncs(player[1]));
        check(levels[0] == (which == 4 || which == 6 ? level : HUB1) && levels[1] == levels[0] &&
                  g64_ihost_hash_mismatches(host) == 0 &&
                  g64_iclient_desyncs(player[0]) == 0 && g64_iclient_desyncs(player[1]) == 0,
              what);
    } else {
        check(0, "a host and a player start in a pack's level");
    }
    for (p = 0; p < PLAYERS; p++) {
        g64_iclient_free(player[p]);
        player[p] = NULL;
    }
    g64_ihost_free(host);
    g64_instances_pack(-1, 0, 0);
    engine_set_rom(e, rom, romSize);
    free(with);
    printf("%s a level that cannot be played does not stop the session\n", failed != before ? "FAIL" : "ok  ");
}

static void pack_unplayable(Engine *e, void *boot) {
    unplayable_case(e, boot, 0);
    unplayable_case(e, boot, 1);
    unplayable_case(e, boot, 2);
    unplayable_case(e, boot, 3);
    unplayable_case(e, boot, 4);
    unplayable_case(e, boot, 5);
    unplayable_case(e, boot, 6);
    unplayable_case(e, boot, 7);
    unplayable_case(e, boot, 8);
}

/* The session's values for scripts. Three players start in the hub with a script that every level runs. Each level's
   script asks for 5 more of value 3 at its frame 50, for 1 more of value 7 twelve times at its frame 60 (a frame
   takes 8 requests), and for 1 more of value 5 at its frame 100. Player 1 leaves the hub for the way room at the
   hub's frame 100, the frame of a request, and player 2 follows at frame 300 into the level that is already running.
   At the end every player's game must have 10, 16 and 2: both levels counted once each, also through the frames
   that ran again. The way room's script must have read 5 as value 3 in its first run, and each script must have
   heard of the changes. */
static void script_session(Engine *e, void *boot) {
    enum { RUN = 700, THREE = 3 };
    static const char script[] =
        "start3 = game.session(3)\n"
        "v3, v5, v7, heard, refused = 0, 0, 0, 0, 0\n"
        "function on_session(k) heard = heard + 1 end\n"
        "function on_frame()\n"
        "  local f = game.frame()\n"
        "  if f == 50 then game.add_session(3, 5) end\n"
        "  if f == 60 then for i = 1, 12 do if not game.add_session(7, 1) then refused = refused + 1 end end end\n"
        "  if f == 100 then game.add_session(5, 1) end\n"
        "  v3, v5, v7 = game.session(3), game.session(5), game.session(7)\n"
        "end\n";
    static const char *const names[] = {"v3", "v5", "v7", "start3", "heard", "refused"};
    unsigned long romSize = 0;
    const uint8_t *rom = engine_rom(e, &romSize);
    uint8_t *with = malloc(romSize + sizeof script);
    uint32_t none[16][2];
    int p, k, before = failed;
    if (!with)
        return;
    memset(none, 0, sizeof none);
    engine_load(e, boot);
    for (p = 0; p < 8; p++)
        g64TestExits[p] = (G64TestExit){-1, 0, 0, 0};
    g64TestExits[0] = (G64TestExit){HUB1, 100, 1, ATLANTIS_EXIT};
    g64TestExits[1] = (G64TestExit){HUB1, 300, 2, ATLANTIS_EXIT};
    engine_save(e, boot);
    g64_instances_one_world(0);
    memcpy(with, rom, romSize);
    memcpy(with + romSize, script, sizeof script - 1);
    engine_set_rom(e, with, romSize + sizeof script);
    g64_instances_pack_script((unsigned)romSize, sizeof script - 1);
    count = 0;
    for (p = 0; p < PLAYERS; p++)
        player[p] = NULL;
    host = g64_ihost_new_with(e, boot, THREE, 7u, none, HUB1, DEADLINE, host_send, NULL);
    for (p = 0; p < THREE; p++)
        player[p] = g64_iclient_new_with(e, boot, THREE, 7u, none, p, HUB1, WINDOW, DEADLINE, player_send, &ids[p]);
    if (host && player[0] && player[1] && player[2]) {
        for (now = 0; now < RUN; now++) {
            deliver();
            g64_ihost_tick(host);
            for (p = 0; p < THREE; p++) {
                rb_input in;
                memset(&in, 0, sizeof in);
                in.present = 1;
                /* Player 0 walks, so the others' guesses about them are sometimes wrong and frames run again. */
                if (p == 0)
                    in.x = (int8_t)((now / 13) % 2 ? 60 : -60);
                g64_iclient_step(player[p], in);
            }
        }
        for (p = 0; p < THREE; p++) {
            double v[6] = {-1, -1, -1, -1, -1, -1};
            char what[240];
            rb_engine_game *g = g64_iclient_game(player[p]);
            int level = g ? g64_iclient_level(player[p]) : -1;
            if (g) {
                rb_engine_game_enter(g);
                for (k = 0; k < 6; k++)
                    v[k] = G64LuaGlobal(names[k]);
            }
            snprintf(what, sizeof what,
                     "player %d in level %d: values 3, 5 and 7 are %.0f, %.0f and %.0f; value 3 was %.0f at the "
                     "script's start; %.0f changes heard, %.0f requests refused; %u desyncs",
                     p, level, v[0], v[1], v[2], v[3], v[4], v[5], g64_iclient_desyncs(player[p]));
            check(g && level == (p == 0 ? HUB1 : WAYROOM) && v[0] == 10 && v[1] == 2 && v[2] == 16 &&
                      v[3] == (p == 0 ? 0 : 5) && v[4] >= 3 && v[5] == 4 && g64_iclient_desyncs(player[p]) == 0,
                  what);
        }
        {
            char what[160];
            snprintf(what, sizeof what, "%d instances, %u hash checks, %u mismatches, %llu frames run again",
                     g64_ihost_instances(host), g64_ihost_hash_matches(host), g64_ihost_hash_mismatches(host),
                     (unsigned long long)(g64_iclient_rollback_frames(player[0]) + g64_iclient_rollback_frames(player[1]) +
                                          g64_iclient_rollback_frames(player[2])));
            check(g64_ihost_instances(host) == 2 && g64_ihost_hash_matches(host) > 100 &&
                      g64_ihost_hash_mismatches(host) == 0,
                  what);
        }
    } else {
        check(0, "a host and two more players start in the hub with a script");
    }
    for (p = 0; p < PLAYERS; p++) {
        g64_iclient_free(player[p]);
        player[p] = NULL;
    }
    g64_ihost_free(host);
    g64_instances_pack_script(0, 0);
    engine_set_rom(e, rom, romSize);
    free(with);
    printf("%s a script's values last from level to level\n", failed != before ? "FAIL" : "ok  ");
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
    pack_level(e, boot);
    script_run(e, boot);
    sample_pack(e, boot);
    sample_pack_retail(e);
    script_session(e, boot);
    pack_unplayable(e, boot);
    free(boot);
    engine_free(e);
    return failed;
}
