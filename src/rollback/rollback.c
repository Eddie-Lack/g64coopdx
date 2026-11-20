/* The rollback core (rollback.h). */
#include "rollback.h"

#include <stdlib.h>
#include <string.h>

enum { MSG_INPUT = 1, MSG_CONFIRM = 2, MSG_HASH = 3, MSG_DESYNC = 4, MSG_HINT = 5 };

/* Message layouts, all numbers little-endian:
 *   input    type, player, ack (u32: confirmed frames the client has), first (u32), count (0..RB_INPUT_REPEAT),
 *            count inputs: the client's own inputs for frames first .. first + count - 1
 *   confirm  type, players, first (u32), count (1..RB_CONFIRM_BATCH), the host's clock (u32: frames it has counted),
 *            count frames: present (u16, a bit per player), players inputs; then the frames' events
 *            (rb_events_write)
 *   hash     type, player, frame (u32), hash (u64)
 *   desync   type, frame (u32)
 *   hint     the layout of an input, type 5: another player's inputs as the host received them, before they are
 *            confirmed. The host forwards each input message at once; a client predicts with them.
 * An input is buttons (u16), x (s8), y (s8). */
#define INPUT_HEAD 11
#define CONFIRM_HEAD 11
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
    in.present = 0;
    return in;
}

static int same_controller(rb_input a, rb_input b) { return a.buttons == b.buttons && a.x == b.x && a.y == b.y; }

static int same_input(rb_input a, rb_input b) { return same_controller(a, b) && a.present == b.present; }

/* The bytes of one confirmed frame in a confirm message. */
static size_t frame_size(int players) { return 2 + 4 * (size_t)players; }

static int same_event(const rb_event *a, const rb_event *b) {
    return a->len == b->len && memcmp(a->data, b->data, a->len) == 0;
}

size_t rb_events_size(const rb_event *events, uint32_t count) {
    size_t n = 1;
    uint32_t i;
    for (i = 0; i < count; i++)
        if (events[i].len)
            n += 2 + events[i].len;
    return n;
}

size_t rb_events_write(uint8_t *d, const rb_event *events, uint32_t count) {
    size_t n = 1;
    uint32_t i;
    d[0] = 0;
    for (i = 0; i < count; i++) {
        if (!events[i].len)
            continue;
        d[0]++;
        d[n] = (uint8_t)i;
        d[n + 1] = events[i].len;
        memcpy(d + n + 2, events[i].data, events[i].len);
        n += 2 + events[i].len;
    }
    return n;
}

int rb_events_parse(const uint8_t *d, size_t len, uint32_t count, rb_event *events) {
    rb_event out[255];
    size_t n = 1;
    uint32_t i, entries, next = 0;
    if (!d || len < 1 || count > 255)
        return -1;
    entries = d[0];
    if (entries > count)
        return -1;
    memset(out, 0, sizeof(rb_event) * count);
    for (i = 0; i < entries; i++) {
        uint32_t at, size;
        if (len - n < 2)
            return -1;
        at = d[n];
        size = d[n + 1];
        /* One event per frame, in order, inside the run. */
        if (at < next || at >= count || size < 1 || size > RB_EVENT_MAX || len - n - 2 < size)
            return -1;
        out[at].len = (uint8_t)size;
        memcpy(out[at].data, d + n + 2, size);
        n += 2 + size;
        next = at + 1;
    }
    if (n != len)
        return -1;
    memcpy(events, out, sizeof(rb_event) * count);
    return 0;
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
    uint32_t window; /* savestates kept: slots for frames f % window */
    uint32_t delay;  /* frames between a local input and the frame it is for (rb_client_set_input_delay) */
    uint32_t queued; /* frames 0 .. queued - 1 have the local player's input */
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
    rb_event events[RB_WINDOW]; /* each confirmed kept frame's event */
    rb_event ran[RB_WINDOW];    /* the event each kept frame last ran with: none for a predicted frame */
    /* Other players' inputs that the host forwarded before confirming them: hint_at is the frame + 1 of the hint
       in each slot, or 0. */
    rb_input hint[RB_WINDOW][RB_MAX_PLAYERS];
    uint32_t hint_at[RB_WINDOW][RB_MAX_PLAYERS];
    uint32_t hints;
    uint32_t host_clock; /* the newest clock the host has sent */
    /* Frames to stay ahead of host_clock. It grows by one each time the host confirms a frame without this player's
       own input, because it came too late; lead_at is the frame run when it last grew. */
    uint32_t lead, lead_at, late;
    uint8_t *states;                            /* window savestates: slot f % window is the state before f */
};

static uint8_t *slot(const rb_client *c, uint32_t f) {
    return c->states + (size_t)(f % c->window) * c->ops->state_size;
}

rb_client *rb_client_new(const rb_game_ops *ops, void *game, int players, int me, int window, rb_send_fn send,
                         rb_frame_fn on_final, void *user) {
    return rb_client_new_at(ops, game, players, me, window, 0, NULL, send, on_final, user);
}

rb_client *rb_client_new_at(const rb_game_ops *ops, void *game, int players, int me, int window, uint32_t start,
                            const rb_input *last, rb_send_fn send, rb_frame_fn on_final, void *user) {
    rb_client *c;
    int p;
    if (window == 0)
        window = RB_WINDOW;
    if (!valid_ops(ops) || !send || players < 1 || players > RB_MAX_PLAYERS || me < 0 || me >= players ||
        window < 8 || window > RB_WINDOW)
        return NULL;
    c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->window = (uint32_t)window;
    c->states = malloc((size_t)window * ops->state_size);
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
    c->frame = c->confirmed = c->reported = c->queued = start;
    for (p = 0; p < players; p++) {
        if (last)
            c->last[p] = last[p];
        else
            c->last[p].present = 1;
    }
    ops->save(game, slot(c, start));
    return c;
}

void rb_client_free(rb_client *c) {
    if (c) {
        free(c->states);
        free(c);
    }
}

/* Another player's input to predict frame f with: the newest hint at or before f, or else their input of the last
 * confirmed frame. */
static rb_input predict(const rb_client *c, uint32_t f, int p) {
    uint32_t g = f;
    for (;;) {
        if (c->hint_at[g % c->window][p] == g + 1)
            return c->hint[g % c->window][p];
        if (g == c->confirmed || g == 0)
            break;
        g--;
    }
    return c->last[p];
}

/* The inputs to run frame f with: its confirmed inputs, or a prediction (the local player's own input, and the
 * others' newest known inputs). */
static void inputs_for(rb_client *c, uint32_t f, rb_input *out) {
    int p;
    if (f < c->confirmed) {
        memcpy(out, c->inputs[f % c->window], sizeof(rb_input) * (size_t)c->players);
        return;
    }
    for (p = 0; p < c->players; p++) {
        out[p] = p == c->me ? c->local[f % c->window] : predict(c, f, p);
        out[p].present = c->last[p].present;
    }
}

static void run_frame(rb_client *c, uint32_t f) {
    rb_input in[RB_MAX_PLAYERS];
    rb_event *ran = &c->ran[f % c->window];
    inputs_for(c, f, in);
    memcpy(c->inputs[f % c->window], in, sizeof(rb_input) * (size_t)c->players);
    /* Only a confirmed frame has an event: a predicted one runs without. */
    if (f < c->confirmed)
        *ran = c->events[f % c->window];
    else
        ran->len = 0;
    if (ran->len && c->ops->event)
        c->ops->event(c->game, ran->data, ran->len);
    c->ops->step(c->game, in, c->players);
    c->ops->save(c->game, slot(c, f + 1));
}

static void send_input(rb_client *c) {
    uint8_t m[INPUT_HEAD + RB_INPUT_REPEAT * 4];
    uint32_t count = c->queued < RB_INPUT_REPEAT ? c->queued : RB_INPUT_REPEAT;
    uint32_t first = c->queued - count, i;
    m[0] = MSG_INPUT;
    m[1] = (uint8_t)c->me;
    put32(m + 2, c->confirmed);
    put32(m + 6, first);
    m[10] = (uint8_t)count;
    for (i = 0; i < count; i++)
        put_input(m + INPUT_HEAD + 4 * i, c->local[(first + i) % c->window]);
    c->send(c->user, 0, m, INPUT_HEAD + 4 * count);
}

/* Passes each newly final frame to on_final, and sends the host the hash of every RB_HASH_EVERY-th one. */
static void report_final(rb_client *c) {
    uint32_t limit = c->confirmed < c->frame ? c->confirmed : c->frame;
    while (c->reported < limit) {
        uint32_t f = c->reported++;
        if (c->on_final)
            c->on_final(c->user, f, slot(c, f + 1));
        if (f % RB_HASH_EVERY == 0) {
            uint64_t h = c->ops->hash_state(slot(c, f + 1));
            uint8_t m[HASH_SIZE];
            m[0] = MSG_HASH;
            m[1] = (uint8_t)c->me;
            put32(m + 2, f);
            put64(m + 6, h);
            c->send(c->user, 0, m, sizeof m);
        }
    }
}

int rb_client_set_input_delay(rb_client *c, int frames) {
    uint32_t f;
    if (frames < 0 || frames > RB_MAX_INPUT_DELAY || (uint32_t)frames > c->window / 4 || c->queued != c->frame)
        return -1;
    /* The first frames after the start have no local input yet: none is pressed. */
    for (f = c->frame; f < c->frame + (uint32_t)frames; f++)
        memset(&c->local[f % c->window], 0, sizeof c->local[0]);
    c->delay = (uint32_t)frames;
    c->queued = c->frame + (uint32_t)frames;
    return 0;
}

int rb_client_step(rb_client *c, rb_input local) {
    /* The window holds the savestates from the first unconfirmed frame to the newest one, and the local inputs up to
       delay frames past it. */
    if (c->frame >= c->confirmed && c->frame - c->confirmed > c->window - 2 - c->delay) {
        send_input(c);
        return 0;
    }
    /* The input pressed now is for a frame delay frames on. It goes to the host now, ahead of its frame. */
    c->local[c->queued % c->window] = local;
    c->queued++;
    run_frame(c, c->frame);
    c->frame++;
    report_final(c);
    send_input(c);
    return 1;
}

void rb_client_idle(rb_client *c) { send_input(c); }

static int client_confirm(rb_client *c, const uint8_t *d, size_t len) {
    uint32_t first, count, i, wrong = UINT32_MAX, f;
    rb_event events[RB_CONFIRM_BATCH];
    size_t frames;
    int p;
    if (len < CONFIRM_HEAD || d[1] != c->players)
        return -1;
    first = get32(d + 2);
    count = d[6];
    frames = CONFIRM_HEAD + (size_t)count * frame_size(c->players);
    if (count < 1 || count > RB_CONFIRM_BATCH || len <= frames ||
        rb_events_parse(d + frames, len - frames, count, events) != 0 || first > UINT32_MAX - count ||
        get32(d + 7) < first + count)
        return -1;
    if (get32(d + 7) > c->host_clock)
        c->host_clock = get32(d + 7);
    for (i = 0; i < count; i++) {
        rb_input in[RB_MAX_PLAYERS];
        f = first + i;
        if (f < c->confirmed)
            continue;
        /* A gap: the host sends again from the frame this client acknowledges. */
        if (f > c->confirmed)
            break;
        /* Confirmed inputs far ahead of the frames run would overwrite inputs the window still needs. */
        if (f >= c->frame && f - c->frame >= c->window - 1)
            break;
        {
            const uint8_t *fr = d + CONFIRM_HEAD + (size_t)i * frame_size(c->players);
            unsigned present = (unsigned)fr[0] | (unsigned)fr[1] << 8;
            for (p = 0; p < c->players; p++) {
                in[p] = get_input(fr + 2 + 4 * (size_t)p);
                in[p].present = (uint8_t)((present >> p) & 1);
            }
        }
        /* The host confirmed a frame without the input this player sent for it: it came too late. A frame run after
           the lead last grew shows whether that was enough. */
        if (f < c->queued && c->queued - f <= c->window && !same_controller(in[c->me], c->local[f % c->window])) {
            c->late++;
            if (f >= c->lead_at && c->lead < RB_MAX_LEAD) {
                c->lead++;
                c->lead_at = c->frame;
            }
        }
        if (f < c->frame && wrong == UINT32_MAX) {
            if (!same_event(&events[i], &c->ran[f % c->window]))
                wrong = f;
            for (p = 0; p < c->players && wrong == UINT32_MAX; p++)
                if (!same_input(in[p], c->inputs[f % c->window][p]))
                    wrong = f;
        }
        c->events[f % c->window] = events[i];
        memcpy(c->inputs[f % c->window], in, sizeof(rb_input) * (size_t)c->players);
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

/* Another player's inputs, forwarded by the host. A frame that ran with another prediction runs again now. */
static int client_hint(rb_client *c, const uint8_t *d, size_t len) {
    uint32_t first, count, i, wrong = UINT32_MAX, f;
    int p;
    if (len < INPUT_HEAD)
        return -1;
    p = d[1];
    first = get32(d + 6);
    count = d[10];
    if (p >= c->players || p == c->me || count > RB_INPUT_REPEAT || len != INPUT_HEAD + (size_t)count * 4 ||
        first > UINT32_MAX - count)
        return -1;
    for (i = 0; i < count; i++) {
        rb_input in = get_input(d + INPUT_HEAD + 4 * i);
        f = first + i;
        /* Only frames between the confirmed ones and the window's end: a hint for another frame is of no use. */
        if (f < c->confirmed || f - c->confirmed >= c->window - 1)
            continue;
        c->hint[f % c->window][p] = in;
        c->hint_at[f % c->window][p] = f + 1;
        c->hints++;
        if (f < c->frame && wrong == UINT32_MAX && !same_controller(in, c->inputs[f % c->window][p]))
            wrong = f;
    }
    if (wrong != UINT32_MAX) {
        c->ops->load(c->game, slot(c, wrong));
        for (f = wrong; f < c->frame; f++)
            run_frame(c, f);
        c->rollback_frames += c->frame - wrong;
    }
    return 0;
}

int rb_client_receive(rb_client *c, const uint8_t *data, size_t len) {
    if (!data || len < 1)
        return -1;
    switch (data[0]) {
    case MSG_CONFIRM:
        return client_confirm(c, data, len);
    case MSG_HINT:
        return client_hint(c, data, len);
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
uint32_t rb_client_hints(const rb_client *c) { return c->hints; }
uint32_t rb_client_host_clock(const rb_client *c) { return c->host_clock; }
uint32_t rb_client_target(const rb_client *c) { return c->host_clock + c->lead; }
uint32_t rb_client_late_inputs(const rb_client *c) { return c->late; }

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
    uint8_t dropped[RB_MAX_PLAYERS]; /* players who left: their input is 0 from then on */
    uint8_t present[RB_MAX_PLAYERS]; /* players in the game from the next confirmed frame on */
    rb_input last[RB_MAX_PLAYERS]; /* the inputs of frame confirmed - 1 */
    /* Inputs received for frames confirmed .. confirmed + RB_HOST_WINDOW - 1. */
    rb_input pending[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    uint8_t have[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    /* Confirmed inputs and reference hashes of frames confirmed - RB_HOST_WINDOW .. confirmed - 1. */
    rb_input done[RB_HOST_WINDOW][RB_MAX_PLAYERS];
    rb_event done_event[RB_HOST_WINDOW];
    rb_event event; /* the event for the next confirmed frame (rb_host_set_event), or len 0 */
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
    memset(h->present, 1, sizeof h->present);
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
        if (h->dropped[p] || !h->present[p])
            memset(&h->last[p], 0, sizeof h->last[p]);
        else if (h->have[s][p])
            h->last[p] = h->pending[s][p];
        h->last[p].present = (uint8_t)(h->present[p] && !h->dropped[p]);
        h->done[s][p] = h->last[p];
        h->have[s][p] = 0;
    }
    /* The event belongs to this frame only. */
    h->done_event[s] = h->event;
    h->event.len = 0;
    if (h->done_event[s].len && h->ops->event)
        h->ops->event(h->game, h->done_event[s].data, h->done_event[s].len);
    h->ops->step(h->game, h->done[s], h->players);
    h->ops->save(h->game, h->state);
    if (f % RB_HASH_EVERY == 0)
        h->hash[s] = h->ops->hash_state(h->state);
    if (h->on_confirm)
        h->on_confirm(h->user, f, h->done[s], h->players, &h->done_event[s]);
    if (h->on_final)
        h->on_final(h->user, f, h->state);
    h->confirmed++;
}

static void host_send_confirmed(rb_host *h, int to) {
    uint8_t m[RB_MAX_MESSAGE];
    rb_event events[RB_CONFIRM_BATCH];
    uint32_t first = h->ack[to], count, avail, i;
    size_t size = CONFIRM_HEAD + 1;
    int p;
    if (first >= h->confirmed)
        return;
    /* A client further behind than the kept frames cannot catch up from here. */
    if (h->confirmed - first > RB_HOST_WINDOW)
        first = h->confirmed - RB_HOST_WINDOW;
    avail = h->confirmed - first;
    if (avail > RB_CONFIRM_BATCH)
        avail = RB_CONFIRM_BATCH;
    /* As many frames as fit in RB_MAX_WIRE bytes with their events. */
    for (count = 0; count < avail; count++) {
        const rb_event *e = &h->done_event[(first + count) % RB_HOST_WINDOW];
        size_t more = frame_size(h->players) + (e->len ? 2u + e->len : 0u);
        if (count > 0 && size + more > RB_MAX_WIRE)
            break;
        size += more;
        events[count] = *e;
    }
    m[0] = MSG_CONFIRM;
    m[1] = (uint8_t)h->players;
    put32(m + 2, first);
    m[6] = (uint8_t)count;
    put32(m + 7, h->clock);
    for (i = 0; i < count; i++) {
        uint8_t *fr = m + CONFIRM_HEAD + (size_t)i * frame_size(h->players);
        const rb_input *in = h->done[(first + i) % RB_HOST_WINDOW];
        unsigned present = 0;
        for (p = 0; p < h->players; p++) {
            put_input(fr + 2 + 4 * (size_t)p, in[p]);
            present |= (unsigned)(in[p].present & 1) << p;
        }
        fr[0] = (uint8_t)present;
        fr[1] = (uint8_t)(present >> 8);
    }
    size = CONFIRM_HEAD + (size_t)count * frame_size(h->players);
    size += rb_events_write(m + size, events, count);
    h->send(h->user, to, m, size);
}

void rb_host_tick(rb_host *h) {
    int p;
    h->clock++;
    while (h->confirmed + h->deadline <= h->clock)
        host_confirm(h);
    for (p = 0; p < h->players; p++)
        if (h->present[p] && !h->dropped[p])
            host_send_confirmed(h, p);
}

static int host_input(rb_host *h, int from, const uint8_t *d, size_t len) {
    uint32_t ack, first, count, i;
    if (len < INPUT_HEAD || d[1] != from || h->dropped[from])
        return -1;
    ack = get32(d + 2);
    first = get32(d + 6);
    count = d[10];
    if (count > RB_INPUT_REPEAT || len != INPUT_HEAD + (size_t)count * 4 || ack > h->confirmed ||
        first > UINT32_MAX - count)
        return -1;
    /* A player who is not in this game sends nothing it needs, for example right after they left it. */
    if (!h->present[from])
        return 0;
    {
        /* The others get these inputs at once, as hints for their predictions. */
        uint8_t m[INPUT_HEAD + RB_INPUT_REPEAT * 4];
        int p;
        memcpy(m, d, len);
        m[0] = MSG_HINT;
        for (p = 0; p < h->players; p++)
            if (p != from && h->present[p] && !h->dropped[p])
                h->send(h->user, p, m, len);
    }
    if (ack > h->ack[from])
        h->ack[from] = ack;
    for (i = 0; i < count; i++) {
        uint32_t f = first + i;
        rb_input in = get_input(d + INPUT_HEAD + 4 * i);
        if (f < h->confirmed) {
            /* Too late: the frame was confirmed without it. Count it when the confirmed input differs. */
            if (h->confirmed - f <= RB_HOST_WINDOW && !same_controller(in, h->done[f % RB_HOST_WINDOW][from]))
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
    /* A client cannot have a final state of a frame the host has not confirmed, and sends only every
     * RB_HASH_EVERY-th frame's hash. */
    if (f >= h->confirmed || f % RB_HASH_EVERY != 0)
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

void rb_host_drop_player(rb_host *h, int p) {
    if (p >= 0 && p < h->players)
        h->dropped[p] = 1;
}

void rb_host_set_present(rb_host *h, int p, int present) {
    if (p < 0 || p >= h->players || (h->present[p] != 0) == (present != 0))
        return;
    h->present[p] = (uint8_t)(present != 0);
    if (present) {
        uint32_t f;
        /* Their client starts at the next confirmed frame, and needs nothing before it. Inputs they sent earlier
           belong to another game. */
        h->ack[p] = h->confirmed;
        for (f = 0; f < RB_HOST_WINDOW; f++)
            h->have[f][p] = 0;
    }
}

int rb_host_present(const rb_host *h, int p) { return p >= 0 && p < h->players && h->present[p]; }

int rb_host_set_event(rb_host *h, const uint8_t *data, size_t len) {
    if (!data || len < 1 || len > RB_EVENT_MAX)
        return -1;
    h->event.len = (uint8_t)len;
    memcpy(h->event.data, data, len);
    return 0;
}

uint32_t rb_host_confirmed(const rb_host *h) { return h->confirmed; }
uint32_t rb_host_hash_matches(const rb_host *h) { return h->matches; }
uint32_t rb_host_hash_mismatches(const rb_host *h) { return h->mismatches; }
uint32_t rb_host_late_inputs(const rb_host *h) { return h->late; }
