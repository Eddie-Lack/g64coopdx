/* Glover behind rb_game_ops (engine_game.h). */
#include "engine_game.h"

#include <stdlib.h>
#include <string.h>

/* weemitt's memory hash (weemitt/src/port/statehash.c): it counts pointers into the game's memory as offsets and
 * pointers to the program as one value, so the Linux and Windows programs give the same hash for the same state. The
 * engine's own hash also covers variables that hold program addresses, which differ between the two. */
unsigned int port_hash_ram(void);

struct rb_engine_game {
    uint8_t *state; /* the game's state, unless the engine holds a newer one (live) */
    int live;
};

static Engine *engine;
static rb_engine_game *owner; /* the game whose state the engine holds, or NULL */
static rb_game_ops ops;

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
    EngineInput e;
    (void)players;
    enter(g);
    e.buttons = in[0].buttons;
    e.x = in[0].x;
    e.y = in[0].y;
    engine_step(engine, &e);
    g->live = 1;
}

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
    return port_hash_ram();
}

const rb_game_ops *rb_engine_ops(Engine *e) {
    engine = e;
    ops.state_size = engine_state_size(e);
    ops.step = step;
    ops.save = save;
    ops.load = load;
    ops.hash_state = hash_state;
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

void rb_engine_game_free(rb_engine_game *g) {
    if (!g)
        return;
    if (owner == g)
        owner = NULL;
    free(g->state);
    free(g);
}

uint64_t rb_engine_game_hash(rb_engine_game *g) {
    enter(g);
    return port_hash_ram();
}
