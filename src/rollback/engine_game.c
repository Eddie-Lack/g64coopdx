/* Glover behind rb_game_ops (engine_game.h). */
#include "engine_game.h"

#include <stdlib.h>
#include <string.h>

/* weemitt's memory hash (weemitt/src/port/statehash.c): it counts pointers into the game's memory as offsets and
 * pointers to the program as one value. The Linux and Windows programs then give the same hash for the same state.
 * The engine's own hash also covers variables that hold program addresses, which differ between the two. */
unsigned int port_hash_ram(void);
int G64LuaActive(void);
unsigned int G64LuaHash(void);

/* The hash that machines compare: the game's memory in the low half. With a script, the high half is the script's
   own number (weemitt's g64lua.c, G64LuaHash): its memory counts, its random numbers' state and the players it has
   seen. The scripts' block itself holds addresses and cannot be compared, but two machines whose scripts ran
   differently soon differ in that number. The top bit is set: a machine whose script was stopped differs from one
   whose script runs. Without a script the hash is the game's memory alone. */
static uint64_t game_hash(void) {
    uint64_t h = port_hash_ram();
    if (G64LuaActive()) {
        h |= (uint64_t)(G64LuaHash() | 0x80000000u) << 32;
    }
    return h;
}

/* The game's side (weemitt's src/port/frame.h and src/game/g64players.h, which use the game's types). */
typedef struct {
    unsigned int addr;
    unsigned int size;
    int sprite2d;
} PortFrameTask;
extern unsigned short port_buttons;
extern signed char port_stick_x, port_stick_y;
extern unsigned short g64Buttons[16];
extern int g64Member[16];
void G64SetPresent(unsigned present);
void G64PlayerAction(int player, int action);
/* weemitt's G64_ACTION_BUTTON (g64players.h): a choice from a player's pause menu. */
#define ACTION_BUTTON 0x0080
extern signed char g64StickX[16], g64StickY[16];
int port_game_frame(PortFrameTask *tasks, int max);

/* The display lists of the latest frame that a step ran. */
static PortFrameTask lastTasks[RB_ENGINE_MAX_TASKS];
static int lastCount;

struct rb_engine_game {
    uint8_t *state; /* the game's state, unless the engine holds a newer one (live) */
    int live;
};

static Engine *engine;
static rb_engine_game *owner; /* the game whose state the engine holds, or NULL */
static rb_game_ops ops;
static rb_engine_event_fn event_fn;

/* Puts game g in the engine: saves the game the engine held, if it changed, and loads g. */
static void enter(rb_engine_game *g) {
    if (owner == g)
        return;
    if (owner && owner->live) {
        engine_save(engine, owner->state);
        owner->live = 0;
    }
    engine_load(engine, g->state);
    owner = g;
}

/* The engine holds a state that belongs to no game. */
static void leave(void) {
    if (owner && owner->live) {
        engine_save(engine, owner->state);
        owner->live = 0;
    }
    owner = NULL;
}

static void step(void *game, const rb_input *in, int players) {
    rb_engine_game *g = game;
    int p, q;
    unsigned present = 0, acted = 0;
    enter(g);
    /* Who is in the game: a player arrives or leaves when their bit changes (g64players.c). */
    for (p = 0; p < players && p < 16; p++)
        present |= (unsigned)(in[p].present & 1) << p;
    G64SetPresent(present);
    /* A choice from a player's own pause menu, carried in their input: it acts for them, with no controller input. */
    for (p = 0; p < players && p < 16; p++)
        if (in[p].present && (in[p].buttons & ACTION_BUTTON)) {
            G64PlayerAction(p, in[p].x);
            acted |= 1u << p;
        }
    /* Inputs come by player number. The player in the globals (g64Member[0]) reads the game's own controller; the
       others read g64players.c's, by number. */
    q = g64Member[0] >= 0 && g64Member[0] < players ? g64Member[0] : 0;
    port_buttons = (acted >> q) & 1 ? 0 : in[q].buttons;
    port_stick_x = (acted >> q) & 1 ? 0 : in[q].x;
    port_stick_y = (acted >> q) & 1 ? 0 : in[q].y;
    for (p = 0; p < players && p < 16; p++) {
        int quiet = (acted >> p) & 1;
        g64Buttons[p] = quiet ? 0 : in[p].buttons;
        g64StickX[p] = quiet ? 0 : in[p].x;
        g64StickY[p] = quiet ? 0 : in[p].y;
    }
    lastCount = port_game_frame(lastTasks, RB_ENGINE_MAX_TASKS);
    engine_count_logic(engine);
    g->live = 1;
}

int rb_engine_last_tasks(void *tasks, int max) {
    int n = lastCount < max ? lastCount : max;
    if (n > 0)
        memcpy(tasks, lastTasks, sizeof(PortFrameTask) * (size_t)n);
    return lastCount < 0 ? -1 : n;
}

static void event(void *game, const uint8_t *data, size_t len) {
    rb_engine_game *g = game;
    enter(g);
    if (event_fn)
        event_fn(data, len);
    g->live = 1;
}

void rb_engine_set_event_fn(rb_engine_event_fn fn) { event_fn = fn; }

static void save(void *game, void *state) {
    rb_engine_game *g = game;
    if (owner == g)
        engine_save(engine, state);
    else
        memcpy(state, g->state, engine_state_used(g->state));
}

static void load(void *game, const void *state) {
    rb_engine_game *g = game;
    leave();
    engine_load(engine, state);
    owner = g;
    g->live = 1;
}

static uint64_t hash_state(const void *state) {
    leave();
    engine_load(engine, state);
    return game_hash();
}

const rb_game_ops *rb_engine_ops(Engine *e) {
    engine = e;
    ops.state_size = engine_state_size(e);
    ops.step = step;
    ops.save = save;
    ops.load = load;
    ops.hash_state = hash_state;
    ops.event = event;
    return &ops;
}

rb_engine_game *rb_engine_game_new(void) {
    rb_engine_game *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    /* A buffer for the largest state; only the bytes a save uses are touched. */
    g->state = malloc(ops.state_size);
    if (!g->state) {
        free(g);
        return NULL;
    }
    if (owner == NULL) {
        engine_save(engine, g->state);
    } else {
        /* The engine holds another game's state: copy that game's. */
        if (owner->live) {
            engine_save(engine, owner->state);
            owner->live = 0;
        }
        memcpy(g->state, owner->state, engine_state_used(owner->state));
    }
    return g;
}

rb_engine_game *rb_engine_game_build(const void *state, rb_engine_setup_fn setup, void *ctx) {
    rb_engine_game *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->state = malloc(ops.state_size);
    if (!g->state) {
        free(g);
        return NULL;
    }
    leave();
    engine_load(engine, state);
    if (setup)
        setup(ctx);
    engine_save(engine, g->state);
    owner = g;
    return g;
}

void rb_engine_game_free(rb_engine_game *g) {
    if (!g)
        return;
    if (owner == g)
        owner = NULL;
    free(g->state);
    free(g);
}

void rb_engine_release(void) { leave(); }

void rb_engine_game_enter(rb_engine_game *g) {
    enter(g);
    /* The caller may draw, which writes the game's memory: the state is saved again when another game enters. */
    g->live = 1;
}

uint64_t rb_engine_game_hash(rb_engine_game *g) {
    enter(g);
    return game_hash();
}
