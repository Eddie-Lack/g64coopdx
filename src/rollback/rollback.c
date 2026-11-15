/* The rollback core (rollback.h). */
#include "rollback.h"

#include <stdlib.h>
#include <string.h>

enum { MSG_INPUT = 1, MSG_CONFIRM = 2, MSG_HASH = 3, MSG_DESYNC = 4 };

/* Message layouts, all numbers little-endian:
 *   input    type, player, ack (u32: confirmed frames the client has), first (u32), count (0..RB_INPUT_REPEAT),
 *            count inputs: the client's own inputs for frames first .. first + count - 1
 *   confirm  type, players, first (u32), count (1..RB_CONFIRM_BATCH), count * players inputs
 *   hash     type, player, frame (u32), hash (u64)
 *   desync   type, frame (u32)
 * An input is buttons (u16), x (s8), y (s8). */
#define INPUT_HEAD 11
#define CONFIRM_HEAD 7
#define HASH_SIZE 14
#define DESYNC_SIZE 5

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

static void put_input(uint8_t *p, rb_input in) {
    p[0] = (uint8_t)in.buttons;
    p[1] = (uint8_t)(in.buttons >> 8);
    p[2] = (uint8_t)in.x;
    p[3] = (uint8_t)in.y;
}

static rb_input get_input(const uint8_t *p) {
    rb_input in;
    in.buttons = (uint16_t)(p[0] | p[1] << 8);
    in.x = (int8_t)p[2];
    in.y = (int8_t)p[3];
    return in;
}

static int same_input(rb_input a, rb_input b) {
    return a.buttons == b.buttons && a.x == b.x && a.y == b.y;
}

static int valid_ops(const rb_game_ops *ops) {
    return ops && ops->state_size > 0 && ops->step && ops->save && ops->load && ops->hash_state;
}

/* ---------------------------------------------------------------------------------------------------------------
 * The client */

struct rb_client {
    const rb_game_ops *ops;
    void *game;
    int players, me;
    rb_send_fn send;
    rb_frame_fn on_final;
    void *user;
    uint32_t frame;     /* frames run: the game is in the state after frame - 1 */
    uint32_t confirmed; /* frames 0 .. confirmed - 1 have confirmed inputs */
    uint32_t reported;  /* frames 0 .. reported - 1 were passed to on_final */
    uint32_t desyncs;
    uint64_t rollback_frames;
    rb_input last[RB_MAX_PLAYERS]; /* the inputs of frame confirmed - 1: the prediction for the others */
    rb_input inputs[RB_WINDOW][RB_MAX_PLAYERS]; /* the inputs each kept frame was run with, or its confirmed inputs */
    rb_input local[RB_WINDOW];                  /* the local player's own input for each kept frame */
    uint8_t *states;                            /* RB_WINDOW savestates: slot f % RB_WINDOW is the state before f */
};

static uint8_t *slot(const rb_client *c, uint32_t f) {
    return c->states + (size_t)(f % RB_WINDOW) * c->ops->state_size;
}

rb_client *rb_client_new(const rb_game_ops *ops, void *game, int players, int me, rb_send_fn send,
                         rb_frame_fn on_final, void *user) {
    rb_client *c;
    if (!valid_ops(ops) || !send || players < 1 || players > RB_MAX_PLAYERS || me < 0 || me >= players)
        return NULL;
    c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->states = malloc(RB_WINDOW * ops->state_size);
    if (!c->states) {
        free(c);
        return NULL;
    }
    c->ops = ops;
    c->game = game;
    c->players = players;
    c->me = me;
    c->send = send;
    c->on_final = on_final;
    c->user = user;
    ops->save(game, slot(c, 0));
    return c;
}

void rb_client_free(rb_client *c) {
    if (c) {
        free(c->states);
        free(c);
    }
}

/* The inputs to run frame f with: its confirmed inputs, or a prediction (the local player's own input, and the
 * others' inputs of the last confirmed frame). */
static void inputs_for(rb_client *c, uint32_t f, rb_input *out) {
    int p;
    if (f < c->confirmed) {
        memcpy(out, c->inputs[f % RB_WINDOW], sizeof(rb_input) * (size_t)c->players);
        return;
    }
    for (p = 0; p < c->players; p++)
        out[p] = p == c->me ? c->local[f % RB_WINDOW] : c->last[p];
}

static void run_frame(rb_client *c, uint32_t f) {
    rb_input in[RB_MAX_PLAYERS];
    inputs_for(c, f, in);
    memcpy(c->inputs[f % RB_WINDOW], in, sizeof(rb_input) * (size_t)c->players);
    c->ops->step(c->game, in, c->players);
    c->ops->save(c->game, slot(c, f + 1));
}

static void send_input(rb_client *c) {
    uint8_t m[INPUT_HEAD + RB_INPUT_REPEAT * 4];
    uint32_t count = c->frame < RB_INPUT_REPEAT ? c->frame : RB_INPUT_REPEAT;
    uint32_t first = c->frame - count, i;
    m[0] = MSG_INPUT;
    m[1] = (uint8_t)c->me;
    put32(m + 2, c->confirmed);
    put32(m + 6, first);
    m[10] = (uint8_t)count;
    for (i = 0; i < count; i++)
        put_input(m + INPUT_HEAD + 4 * i, c->local[(first + i) % RB_WINDOW]);
    c->send(c->user, 0, m, INPUT_HEAD + 4 * count);
}

/* Passes each newly final frame to on_final, and sends the host the hash of every RB_HASH_EVERY-th one. */
static void report_final(rb_client *c) {
    uint32_t limit = c->confirmed < c->frame ? c->confirmed : c->frame;
    while (c->reported < limit) {
        uint32_t f = c->reported++;
        uint64_t h = c->ops->hash_state(slot(c, f + 1));
        if (c->on_final)
            c->on_final(c->user, f, h);
        if (f % RB_HASH_EVERY == 0) {
            uint8_t m[HASH_SIZE];
            m[0] = MSG_HASH;
            m[1] = (uint8_t)c->me;
            put32(m + 2, f);
            put64(m + 6, h);
            c->send(c->user, 0, m, sizeof m);
        }
    }
}

int rb_client_step(rb_client *c, rb_input local) {
    /* The window holds the savestates from the first unconfirmed frame to the newest one. */
    if (c->frame >= c->confirmed && c->frame - c->confirmed > RB_WINDOW - 2) {
        send_input(c);
        return 0;
    }
    c->local[c->frame % RB_WINDOW] = local;
    run_frame(c, c->frame);
    c->frame++;
    report_final(c);
    send_input(c);
    return 1;
}

void rb_client_idle(rb_client *c) { send_input(c); }

static int client_confirm(rb_client *c, const uint8_t *d, size_t len) {
    uint32_t first, count, i, wrong = UINT32_MAX, f;
    int p;
    if (len < CONFIRM_HEAD || d[1] != c->players)
        return -1;
    first = get32(d + 2);
    count = d[6];
    if (count < 1 || count > RB_CONFIRM_BATCH || len != CONFIRM_HEAD + (size_t)count * (size_t)c->players * 4 ||
        first > UINT32_MAX - count)
        return -1;
    for (i = 0; i < count; i++) {
        rb_input in[RB_MAX_PLAYERS];
        f = first + i;
        if (f < c->confirmed)
            continue;
        /* A gap: the host sends again from the frame this client acknowledges. */
        if (f > c->confirmed)
            break;
        /* Confirmed inputs far ahead of the frames run would overwrite inputs the window still needs. */
        if (f >= c->frame && f - c->frame >= RB_WINDOW - 1)
            break;
        for (p = 0; p < c->players; p++)
            in[p] = get_input(d + CONFIRM_HEAD + 4 * ((size_t)i * (size_t)c->players + (size_t)p));
        if (f < c->frame && wrong == UINT32_MAX)
            for (p = 0; p < c->players; p++)
                if (!same_input(in[p], c->inputs[f % RB_WINDOW][p])) {
                    wrong = f;
                    break;
                }
        memcpy(c->inputs[f % RB_WINDOW], in, sizeof(rb_input) * (size_t)c->players);
        memcpy(c->last, in, sizeof(rb_input) * (size_t)c->players);
        c->confirmed++;
    }
    if (wrong != UINT32_MAX) {
        /* Back to the state before the first wrong frame, then the frames again with what is known now. */
        c->ops->load(c->game, slot(c, wrong));
        for (f = wrong; f < c->frame; f++)
            run_frame(c, f);
        c->rollback_frames += c->frame - wrong;
    }
    report_final(c);
    return 0;
}

int rb_client_receive(rb_client *c, const uint8_t *data, size_t len) {
    if (!data || len < 1)
        return -1;
    switch (data[0]) {
    case MSG_CONFIRM:
        return client_confirm(c, data, len);
    case MSG_DESYNC:
        if (len != DESYNC_SIZE)
            return -1;
        c->desyncs++;
        return 0;
    default:
        return -1;
    }
}

uint32_t rb_client_frame(const rb_client *c) { return c->frame; }
uint32_t rb_client_confirmed(const rb_client *c) { return c->confirmed; }
uint32_t rb_client_desyncs(const rb_client *c) { return c->desyncs; }
uint64_t rb_client_rollback_frames(const rb_client *c) { return c->rollback_frames; }

/* ---------------------------------------------------------------------------------------------------------------
 * The host */

struct rb_host {
    const rb_game_ops *ops;
    void *game;
    int players;
    uint32_t deadline;
    rb_send_fn send;
    rb_confirm_fn on_confirm;
    rb_frame_fn on_final;
    void *user;
    uint32_t clock;     /* frames counted by the host's clock */
    uint32_t confirmed; /* frames 0 .. confirmed - 1 are confirmed */
    uint32_t matches, mismatches, late;
    uint32_t ack[RB_MAX_PLAYERS];  /* confirmed frames each client has said it has */
    rb_input last[RB_MAX_PLAYERS]; /* the inputs of frame confirmed - 1 */
    /* Inputs received for frames confirmed .. confirmed + RB_HOST_WINDOW - 1. */
    rb_input pending[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    uint8_t have[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    /* Confirmed inputs and reference hashes of frames confirmed - RB_HOST_WINDOW .. confirmed - 1. */
    rb_input done[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    uint64_t hash[RB_HOST_WINDOW];
    uint8_t *state;
};

rb_host *rb_host_new(const rb_game_ops *ops, void *game, int players, uint32_t deadline, rb_send_fn send,
                     rb_confirm_fn on_confirm, rb_frame_fn on_final, void *user) {
    rb_host *h;
    if (!valid_ops(ops) || !send || players < 1 || players > RB_MAX_PLAYERS || deadline < 1 ||
        deadline >= RB_HOST_WINDOW)
        return NULL;
    h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->state = malloc(ops->state_size);
    if (!h->state) {
        free(h);
        return NULL;
    }
    h->ops = ops;
    h->game = game;
    h->players = players;
    h->deadline = deadline;
    h->send = send;
    h->on_confirm = on_confirm;
    h->on_final = on_final;
    h->user = user;
    return h;
}

void rb_host_free(rb_host *h) {
    if (h) {
        free(h->state);
        free(h);
    }
}

static void host_confirm(rb_host *h) {
    uint32_t f = h->confirmed, s = f % RB_HOST_WINDOW;
    int p;
    for (p = 0; p < h->players; p++) {
        /* A player whose input has not arrived repeats their last input. */
        if (h->have[s][p])
            h->last[p] = h->pending[s][p];
        h->done[s][p] = h->last[p];
        h->have[s][p] = 0;
    }
    h->ops->step(h->game, h->done[s], h->players);
    h->ops->save(h->game, h->state);
    h->hash[s] = h->ops->hash_state(h->state);
    if (h->on_confirm)
        h->on_confirm(h->user, f, h->done[s], h->players);
    if (h->on_final)
        h->on_final(h->user, f, h->hash[s]);
    h->confirmed++;
}

static void host_send_confirmed(rb_host *h, int to) {
    uint8_t m[RB_MAX_MESSAGE];
    uint32_t first = h->ack[to], count, i;
    int p;
    if (first >= h->confirmed)
        return;
    /* A client further behind than the kept frames cannot catch up from here. */
    if (h->confirmed - first > RB_HOST_WINDOW)
        first = h->confirmed - RB_HOST_WINDOW;
    count = h->confirmed - first;
    if (count > RB_CONFIRM_BATCH)
        count = RB_CONFIRM_BATCH;
    m[0] = MSG_CONFIRM;
    m[1] = (uint8_t)h->players;
    put32(m + 2, first);
    m[6] = (uint8_t)count;
    for (i = 0; i < count; i++)
        for (p = 0; p < h->players; p++)
            put_input(m + CONFIRM_HEAD + 4 * ((size_t)i * (size_t)h->players + (size_t)p),
                      h->done[(first + i) % RB_HOST_WINDOW][p]);
    h->send(h->user, to, m, CONFIRM_HEAD + (size_t)count * (size_t)h->players * 4);
}

void rb_host_tick(rb_host *h) {
    int p;
    h->clock++;
    while (h->confirmed + h->deadline <= h->clock)
        host_confirm(h);
    for (p = 0; p < h->players; p++)
        host_send_confirmed(h, p);
}

static int host_input(rb_host *h, int from, const uint8_t *d, size_t len) {
    uint32_t ack, first, count, i;
    if (len < INPUT_HEAD || d[1] != from)
        return -1;
    ack = get32(d + 2);
    first = get32(d + 6);
    count = d[10];
    if (count > RB_INPUT_REPEAT || len != INPUT_HEAD + (size_t)count * 4 || ack > h->confirmed ||
        first > UINT32_MAX - count)
        return -1;
    if (ack > h->ack[from])
        h->ack[from] = ack;
    for (i = 0; i < count; i++) {
        uint32_t f = first + i;
        rb_input in = get_input(d + INPUT_HEAD + 4 * i);
        if (f < h->confirmed) {
            /* Too late: the frame was confirmed without it. Count it when the confirmed input differs. */
            if (h->confirmed - f <= RB_HOST_WINDOW && !same_input(in, h->done[f % RB_HOST_WINDOW][from]))
                h->late++;
        } else if (f - h->confirmed < RB_HOST_WINDOW) {
            h->pending[f % RB_HOST_WINDOW][from] = in;
            h->have[f % RB_HOST_WINDOW][from] = 1;
        }
    }
    return 0;
}

static int host_hash(rb_host *h, int from, const uint8_t *d, size_t len) {
    uint32_t f;
    if (len != HASH_SIZE || d[1] != from)
        return -1;
    f = get32(d + 2);
    /* A client cannot have a final state of a frame the host has not confirmed. */
    if (f >= h->confirmed)
        return -1;
    if (h->confirmed - f > RB_HOST_WINDOW)
        return 0;
    if (get64(d + 6) == h->hash[f % RB_HOST_WINDOW]) {
        h->matches++;
    } else {
        uint8_t m[DESYNC_SIZE];
        h->mismatches++;
        m[0] = MSG_DESYNC;
        put32(m + 1, f);
        h->send(h->user, from, m, sizeof m);
    }
    return 0;
}

int rb_host_receive(rb_host *h, int from, const uint8_t *data, size_t len) {
    if (!data || len < 2 || from < 0 || from >= h->players)
        return -1;
    switch (data[0]) {
    case MSG_INPUT:
        return host_input(h, from, data, len);
    case MSG_HASH:
        return host_hash(h, from, data, len);
    default:
        return -1;
    }
}

uint32_t rb_host_confirmed(const rb_host *h) { return h->confirmed; }
uint32_t rb_host_hash_matches(const rb_host *h) { return h->matches; }
uint32_t rb_host_hash_mismatches(const rb_host *h) { return h->mismatches; }
uint32_t rb_host_late_inputs(const rb_host *h) { return h->late; }
