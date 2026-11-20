/* The toy game: a small deterministic game with the same interface as the real one (step, save, load, hash). It
 * stands in for Glover in the tests that need no ROM (docs/discovery/repository.md, "Layout").
 *
 * Each player has a body in a square arena. The stick pushes it, A gives a dash, bodies bounce off the walls and off
 * each other, and they collect coins that reappear at random places. A player who is not present (rb_input.present)
 * stays still and touches nothing. All arithmetic is on integers, so every machine gives the same state. A session
 * event adds to one body's score. */
#ifndef G64_TOY_H
#define G64_TOY_H

#include <stdint.h>

#include "../../src/rollback/rollback.h"

#define TOY_COINS 8

/* The whole state. Every field has a fixed width and the padding is explicit, so a savestate is plain bytes. */
typedef struct {
    int32_t x, y, vx, vy;
    uint32_t score;
    uint16_t cooldown;
    uint16_t pad;
} toy_body;

typedef struct {
    uint32_t frame;
    uint32_t rng;
    uint32_t players;
    uint32_t pad;
    toy_body body[RB_MAX_PLAYERS];
    int32_t coin_x[TOY_COINS], coin_y[TOY_COINS];
} toy_game;

/* The start state for a number of players. */
void toy_init(toy_game *g, int players);
/* The functions the rollback core calls. */
extern const rb_game_ops toy_ops;

#endif
