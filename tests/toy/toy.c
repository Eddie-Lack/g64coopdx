/* The toy game (toy.h). */
#include "toy.h"

#include <string.h>

/* Positions are in 1/256 of a unit. The arena is 4096 units on a side. */
#define ARENA (4096 * 256)
#define RADIUS (24 * 256)
#define MAX_SPEED (40 * 256)
#define BUTTON_A 0x8000

static uint32_t next_random(toy_game *g) {
    /* xorshift32 */
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static void place_coin(toy_game *g, int i) {
    g->coin_x[i] = (int32_t)(next_random(g) % (ARENA - 2 * RADIUS)) + RADIUS;
    g->coin_y[i] = (int32_t)(next_random(g) % (ARENA - 2 * RADIUS)) + RADIUS;
}

void toy_init(toy_game *g, int players) {
    int i;
    memset(g, 0, sizeof *g);
    g->rng = 0x9e3779b9u;
    g->players = (uint32_t)players;
    for (i = 0; i < players; i++) {
        g->body[i].x = ARENA / 2 + (i % 4 - 2) * 200 * 256;
        g->body[i].y = ARENA / 2 + (i / 4 - 2) * 200 * 256;
    }
    for (i = 0; i < TOY_COINS; i++)
        place_coin(g, i);
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void bounce_walls(toy_body *b) {
    if (b->x < RADIUS) {
        b->x = RADIUS;
        b->vx = -b->vx;
    } else if (b->x > ARENA - RADIUS) {
        b->x = ARENA - RADIUS;
        b->vx = -b->vx;
    }
    if (b->y < RADIUS) {
        b->y = RADIUS;
        b->vy = -b->vy;
    } else if (b->y > ARENA - RADIUS) {
        b->y = ARENA - RADIUS;
        b->vy = -b->vy;
    }
}

/* Two bodies that touch swap their speeds and are pushed apart along the larger axis. */
static void collide(toy_body *a, toy_body *b) {
    int64_t dx = (int64_t)b->x - a->x, dy = (int64_t)b->y - a->y;
    int32_t t;
    if (dx * dx + dy * dy >= (int64_t)(2 * RADIUS) * (2 * RADIUS))
        return;
    t = a->vx, a->vx = b->vx, b->vx = t;
    t = a->vy, a->vy = b->vy, b->vy = t;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) {
        int32_t push = dx < 0 ? -RADIUS / 4 : RADIUS / 4;
        a->x -= push, b->x += push;
    } else {
        int32_t push = dy < 0 ? -RADIUS / 4 : RADIUS / 4;
        a->y -= push, b->y += push;
    }
}

static void toy_step(void *game, const rb_input *in, int players) {
    toy_game *g = game;
    int i, j, n = players < (int)g->players ? players : (int)g->players;
    for (i = 0; i < n; i++) {
        toy_body *b = &g->body[i];
        b->vx += in[i].x * 16;
        b->vy += in[i].y * 16;
        if ((in[i].buttons & BUTTON_A) && b->cooldown == 0) {
            b->vx *= 2;
            b->vy *= 2;
            b->cooldown = 10;
        }
        if (b->cooldown)
            b->cooldown--;
        b->vx -= b->vx / 16;
        b->vy -= b->vy / 16;
        b->vx = clamp(b->vx, -MAX_SPEED, MAX_SPEED);
        b->vy = clamp(b->vy, -MAX_SPEED, MAX_SPEED);
        b->x += b->vx;
        b->y += b->vy;
        bounce_walls(b);
    }
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            collide(&g->body[i], &g->body[j]);
    for (i = 0; i < n; i++) {
        toy_body *b = &g->body[i];
        bounce_walls(b);
        for (j = 0; j < TOY_COINS; j++) {
            int64_t dx = (int64_t)g->coin_x[j] - b->x, dy = (int64_t)g->coin_y[j] - b->y;
            if (dx * dx + dy * dy < (int64_t)(2 * RADIUS) * (2 * RADIUS)) {
                b->score++;
                place_coin(g, j);
            }
        }
    }
    next_random(g);
    g->frame++;
}

static void toy_save(void *game, void *state) { memcpy(state, game, sizeof(toy_game)); }
static void toy_load(void *game, const void *state) { memcpy(game, state, sizeof(toy_game)); }

static uint64_t toy_hash(const void *state) {
    /* 64-bit FNV-1a over the savestate's bytes. */
    const uint8_t *p = state;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;
    for (i = 0; i < sizeof(toy_game); i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

const rb_game_ops toy_ops = {sizeof(toy_game), toy_step, toy_save, toy_load, toy_hash};
