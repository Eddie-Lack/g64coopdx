/* Glover behind rb_game_ops, through the brute forcer's engine (weemitt/bruteforce/engine/engine.h).
 *
 * A process holds one game, but a test runs a host and several clients in one process, each with its own game. So
 * each rb_engine_game is a savestate, and the one engine plays whichever game steps: it saves the game it held and
 * loads the next. The engine's saves and loads copy only the pages that differ, so a switch is cheap.
 *
 * The game has one player so far: player 0's input drives it, and the other players' inputs are ignored. Phase 1
 * adds the other players to the game. */
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
/* The hash of the state of a game, as rb_game_ops.hash_state gives it. Slow (it reads the whole heap). */
uint64_t rb_engine_game_hash(rb_engine_game *g);

#endif
