/* Glover behind rb_game_ops, through the brute forcer's engine (weemitt/bruteforce/engine/engine.h).
 *
 * A process holds one game, but a test runs a host and several clients in one process, each with its own game. So
 * each rb_engine_game is a savestate, and the one engine plays whichever game steps: it saves the game it held and
 * loads the next. The engine's saves and loads copy only the pages that differ, so a switch is cheap.
 *
 * Player 0's input is the game's own controller; the other players' inputs go to weemitt's g64players.c, which
 * runs their gloves when g64Players is above 1. */
#ifndef G64_ENGINE_GAME_H
#define G64_ENGINE_GAME_H

#include "../../weemitt/bruteforce/engine/engine.h"
#include "rollback.h"

typedef struct rb_engine_game rb_engine_game;

/* The functions for rb_client_new and rb_host_new, for games of this engine. Call it once, after engine_create. */
const rb_game_ops *rb_engine_ops(Engine *e);
/* A new game in the engine's current state. NULL when out of memory. */
rb_engine_game *rb_engine_game_new(void);
void rb_engine_game_free(rb_engine_game *g);
/* A new game built from a state: the engine loads state (for g64coopdx, the state right after power-on), setup
 * changes it (it sets the players and starts a level), and the result is the new game. NULL when out of memory. */
typedef void (*rb_engine_setup_fn)(void *ctx);
rb_engine_game *rb_engine_game_build(const void *state, rb_engine_setup_fn setup, void *ctx);
/* The most display-list tasks of one frame that rb_engine_last_tasks keeps. */
#define RB_ENGINE_MAX_TASKS 16
/* The display-list tasks (weemitt's PortFrameTask) of the latest frame any game stepped: what to draw after the
 * local game's newest frame. Returns their count, or -1 when that frame built no lists. */
int rb_engine_last_tasks(void *tasks, int max);
/* Puts game g in the engine (the other games are saved first). The engine's memory then holds g's newest state, for
 * drawing it. */
void rb_engine_game_enter(rb_engine_game *g);
/* Saves the game the engine holds, and leaves the engine to the caller, who may load any state into it. The next
 * game that steps or enters is loaded again. */
void rb_engine_release(void);
/* The hash of the state of a game, as rb_game_ops.hash_state gives it: weemitt's memory hash, the same on Linux and
 * Windows. Slow (it reads all 24 MB of the game's memory). */
uint64_t rb_engine_game_hash(rb_engine_game *g);

#endif
