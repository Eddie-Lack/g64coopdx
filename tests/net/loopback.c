/* Loopback tests of the rollback core with the toy game (docs/discovery/roadmap.md, phase 0).
 *
 * One host and 2 to 16 clients run in one process over a simulated network that delays, loses, duplicates and
 * reorders messages. The test: every client's final state of every frame equals one offline run of the host's
 * confirmed inputs and events, frame by frame, and every hash check passes. The host attaches a session event to a
 * frame now and then (rb_host_set_event). More tests check that a player can leave the game and
 * join it again at a later frame, that a client whose game differs is caught, and that malformed messages are refused.
 *
 * Usage: loopback [-v]. Exits with 1 if a test fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/rollback/rollback.h"
#include "../toy/toy.h"

#define FRAMES 600
#define MAX_PACKETS 65536

typedef struct {
    const char *name;
    int latency; /* frames, one way */
    int jitter;  /* extra frames, 0 .. jitter, drawn per message: it reorders messages */
    int loss;    /* per 1000 messages */
    int dup;     /* per 1000 messages */
    uint32_t deadline;
} network;

static const network NETWORKS[] = {
    {"perfect", 0, 0, 0, 0, 2},
    {"lan", 1, 0, 0, 0, 3},
    {"wan", 3, 2, 20, 5, 7},
    {"bad", 5, 6, 100, 20, 10},
    {"late inputs", 4, 4, 50, 0, 3},
};

typedef struct {
    uint32_t at;
    int to; /* a client number, or -1 for the host */
    int from;
    size_t len;
    uint8_t data[RB_MAX_MESSAGE];
} packet;

typedef struct {
    const network *net;
    int players;
    uint32_t now;
    uint32_t rng;
    packet *packets;
    int count;
    rb_host *host;
    rb_client *client[RB_MAX_PLAYERS];
    toy_game client_game[RB_MAX_PLAYERS];
    toy_game host_game;
    rb_input log[FRAMES + 512][RB_MAX_PLAYERS];
    rb_event log_event[FRAMES + 512];
    uint32_t events; /* frames confirmed with an event */
    uint64_t host_hash[FRAMES + 512];
    uint64_t final[RB_MAX_PLAYERS][FRAMES + 512];
    uint32_t reported[RB_MAX_PLAYERS];
    uint32_t confirmed;
    int bad_order;
    int window;
} world;

typedef struct {
    world *w;
    int id;
} endpoint;

static int verbose;
/* The clients' input delay in the session test (rb_client_set_input_delay). */
static int input_delay;

static uint32_t rnd(world *w) {
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return w->rng = x;
}

static void queue(world *w, int from, int to, const uint8_t *data, size_t len) {
    int copies = 1, i;
    if ((int)(rnd(w) % 1000) < w->net->loss)
        return;
    if ((int)(rnd(w) % 1000) < w->net->dup)
        copies = 2;
    for (i = 0; i < copies && w->count < MAX_PACKETS; i++) {
        packet *p = &w->packets[w->count++];
        p->at = w->now + (uint32_t)w->net->latency + (w->net->jitter ? rnd(w) % (uint32_t)(w->net->jitter + 1) : 0);
        p->to = to;
        p->from = from;
        p->len = len;
        memcpy(p->data, data, len);
    }
}

static void client_send(void *user, int to, const uint8_t *data, size_t len) {
    endpoint *e = user;
    (void)to;
    queue(e->w, e->id, -1, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    queue(user, -1, to, data, len);
}

static void host_confirmed(void *user, uint32_t frame, const rb_input *in, int players, const rb_event *event) {
    world *w = user;
    if (frame != w->confirmed)
        w->bad_order = 1;
    if (frame < FRAMES + 512) {
        memcpy(w->log[frame], in, sizeof(rb_input) * (size_t)players);
        w->log_event[frame] = *event;
    }
    w->events += event->len != 0;
    w->confirmed = frame + 1;
}

/* The host's session events: one every 37 ticks, with a varying body and amount, of 1 to 5 bytes. */
static void host_events(world *w) {
    uint8_t d[5];
    size_t len = 1 + w->now % 5;
    if (w->now % 37 != 11)
        return;
    d[0] = (uint8_t)(w->now / 37);
    d[1] = (uint8_t)w->now, d[2] = 1, d[3] = 0, d[4] = 2;
    rb_host_set_event(w->host, d, len);
}

/* The offline run's frame f: its event, then its inputs. */
static void offline_frame(world *w, toy_game *g, uint32_t f) {
    if (w->log_event[f].len)
        toy_ops.event(g, w->log_event[f].data, w->log_event[f].len);
    toy_ops.step(g, w->log[f], w->players);
}

static void host_final(void *user, uint32_t frame, const void *state) {
    world *w = user;
    if (frame < FRAMES + 512)
        w->host_hash[frame] = toy_ops.hash_state(state);
}

static void client_final(void *user, uint32_t frame, const void *state) {
    endpoint *e = user;
    uint64_t hash = toy_ops.hash_state(state);
    world *w = e->w;
    if (frame != w->reported[e->id])
        w->bad_order = 1;
    if (frame < FRAMES + 512)
        w->final[e->id][frame] = hash;
    w->reported[e->id] = frame + 1;
}

/* A player's controller: a new stick and buttons every 8 frames, from a hash of the player and the frame. */
static rb_input controller(int player, uint32_t frame) {
    uint32_t h = (uint32_t)player * 0x9e3779b1u ^ (frame / 8) * 0x85ebca77u;
    rb_input in;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    in.x = (int8_t)((int)(h % 161) - 80);
    in.y = (int8_t)((int)((h >> 8) % 161) - 80);
    in.buttons = (h >> 20) % 5 == 0 ? 0x8000 : 0;
    in.present = 1;
    return in;
}

static void deliver(world *w) {
    int i, kept = 0;
    /* Messages due now, in the order they were sent; a later message with less delay comes first. */
    for (i = 0; i < w->count; i++) {
        packet *p = &w->packets[i];
        if (p->at > w->now) {
            if (kept != i)
                w->packets[kept] = *p;
            kept++;
            continue;
        }
        if (p->to < 0)
            rb_host_receive(w->host, p->from, p->data, p->len);
        else if (w->client[p->to])
            rb_client_receive(w->client[p->to], p->data, p->len);
    }
    w->count = kept;
}

/* The flawed client of the desync test: its game gives one body an extra push at frame 200. */
static void flawed_step(void *game, const rb_input *in, int players) {
    toy_game *g = game;
    toy_ops.step(game, in, players);
    if (g->frame == 200)
        g->body[0].vx += 1;
}

static const rb_game_ops flawed_ops = {sizeof(toy_game), flawed_step, NULL, NULL, NULL, NULL};

static int failures;

static void check(int ok, const char *what) {
    if (!ok || verbose)
        printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/* Runs a session. flawed is a client number whose game differs, or -1. Returns the world for the caller's checks. */
static world *session(const network *net, int players, int flawed, int window) {
    static endpoint ends[RB_MAX_PLAYERS];
    static rb_game_ops flawed_full;
    world *w = calloc(1, sizeof *w);
    uint32_t end;
    int i;
    w->net = net;
    w->players = players;
    w->window = window;
    w->rng = 12345u + (uint32_t)players * 77u;
    w->packets = calloc(MAX_PACKETS, sizeof(packet));
    toy_init(&w->host_game, players);
    w->host = rb_host_new(&toy_ops, &w->host_game, players, net->deadline, host_send, host_confirmed, host_final, w);
    flawed_full = toy_ops;
    flawed_full.step = flawed_ops.step;
    for (i = 0; i < players; i++) {
        ends[i].w = w;
        ends[i].id = i;
        toy_init(&w->client_game[i], players);
        w->client[i] = rb_client_new(i == flawed ? &flawed_full : &toy_ops, &w->client_game[i], players, i,
                                     window, client_send, client_final, &ends[i]);
        if (input_delay)
            rb_client_set_input_delay(w->client[i], input_delay);
    }
    end = FRAMES + net->deadline + (uint32_t)(net->latency + net->jitter) * 4 + 120;
    for (w->now = 0; w->now < end; w->now++) {
        deliver(w);
        host_events(w);
        rb_host_tick(w->host);
        for (i = 0; i < players; i++) {
            int steps = 0;
            /* A client that had to wait runs up to 3 frames in one tick until it catches up. */
            while (steps < 3 && rb_client_frame(w->client[i]) <= w->now && rb_client_frame(w->client[i]) < FRAMES) {
                if (!rb_client_step(w->client[i], controller(i, rb_client_frame(w->client[i]))))
                    break;
                steps++;
            }
            if (steps == 0)
                rb_client_idle(w->client[i]);
        }
    }
    return w;
}

static void finish(world *w) {
    int i;
    for (i = 0; i < w->players; i++)
        rb_client_free(w->client[i]);
    rb_host_free(w->host);
    free(w->packets);
    free(w);
}

static void test_session(const network *net, int players, int window) {
    char what[200];
    world *w = session(net, players, -1, window);
    toy_game offline;
    uint32_t f, wrong = 0, frames = w->confirmed < FRAMES ? w->confirmed : FRAMES;
    int i, clients_ok = 1, desyncs = 0;
    uint64_t rollbacks = 0;
    toy_init(&offline, players);
    for (f = 0; f < frames; f++) {
        toy_game copy;
        uint64_t h;
        offline_frame(w, &offline, f);
        toy_ops.save(&offline, &copy);
        h = toy_ops.hash_state(&copy);
        if (h != w->host_hash[f])
            wrong++;
        for (i = 0; i < players; i++)
            if (w->reported[i] > f && w->final[i][f] != h)
                wrong++;
    }
    for (i = 0; i < players; i++) {
        if (w->reported[i] < FRAMES)
            clients_ok = 0;
        desyncs += (int)rb_client_desyncs(w->client[i]);
        rollbacks += rb_client_rollback_frames(w->client[i]);
    }
    snprintf(what, sizeof what, "%s, %d players, window %d, input delay %d: %u frames confirmed, every client final on "
             "all %d", net->name, players, window, input_delay, w->confirmed, FRAMES);
    check(clients_ok && w->confirmed >= FRAMES, what);
    snprintf(what, sizeof what, "%s, %d players: every final state equals the offline run (%u differ, %u frames with "
             "an event)", net->name, players, wrong, w->events);
    check(wrong == 0 && !w->bad_order && w->events > 10, what);
    snprintf(what, sizeof what, "%s, %d players: %u hash checks, %u mismatches, %d desync reports; %llu frames run "
             "again, %u late inputs", net->name, players, rb_host_hash_matches(w->host),
             rb_host_hash_mismatches(w->host), desyncs, (unsigned long long)rollbacks, rb_host_late_inputs(w->host));
    check(rb_host_hash_matches(w->host) > 0 && rb_host_hash_mismatches(w->host) == 0 && desyncs == 0, what);
    finish(w);
}

/* A player leaves the game at a frame and joins it again later. The host sets them absent; their client stops. Later
 * the host sets them present again, and a new client for them runs the host's confirmed inputs up to that frame
 * offline and starts there (rb_client_new_at). Every client's final states must still equal the offline run. */
static void test_join(const network *net) {
    static endpoint ends[RB_MAX_PLAYERS];
    const int players = 4, mover = 3;
    const uint32_t leave_at = 150, join_at = 320;
    world *w = calloc(1, sizeof *w);
    uint32_t end, f, joined = 0, wrong = 0, frames, left = 0, kept = 0;
    int i, gone = 0, clients_ok = 1, desyncs = 0;
    toy_game offline;
    char what[200];
    w->net = net;
    w->players = players;
    w->rng = 999u;
    w->packets = calloc(MAX_PACKETS, sizeof(packet));
    toy_init(&w->host_game, players);
    w->host = rb_host_new(&toy_ops, &w->host_game, players, net->deadline, host_send, host_confirmed, host_final, w);
    for (i = 0; i < players; i++) {
        ends[i].w = w;
        ends[i].id = i;
        toy_init(&w->client_game[i], players);
        w->client[i] = rb_client_new(&toy_ops, &w->client_game[i], players, i, 0, client_send, client_final, &ends[i]);
    }
    end = FRAMES + net->deadline + (uint32_t)(net->latency + net->jitter) * 4 + 120;
    for (w->now = 0; w->now < end; w->now++) {
        deliver(w);
        if (w->now == leave_at) {
            left = rb_host_confirmed(w->host);
            kept = w->reported[mover];
            rb_host_set_present(w->host, mover, 0);
            rb_client_free(w->client[mover]);
            w->client[mover] = NULL;
            gone = 1;
        }
        host_events(w);
        if (w->now == join_at) {
            /* The joiner builds the game from its start and the host's log, then starts at the next frame the host
               confirms, as present. That frame also carries an event: the joiner gets it in a confirm message. */
            static const uint8_t ev[3] = {3, 50, 0};
            joined = rb_host_confirmed(w->host);
            rb_host_set_present(w->host, mover, 1);
            rb_host_set_event(w->host, ev, sizeof ev);
            toy_init(&w->client_game[mover], players);
            for (f = 0; f < joined; f++)
                offline_frame(w, &w->client_game[mover], f);
            w->reported[mover] = joined;
            w->client[mover] = rb_client_new_at(&toy_ops, &w->client_game[mover], players, mover, 0, joined,
                                                joined ? w->log[joined - 1] : NULL, client_send, client_final,
                                                &ends[mover]);
            gone = 0;
        }
        rb_host_tick(w->host);
        for (i = 0; i < players; i++) {
            int steps = 0;
            if (!w->client[i])
                continue;
            while (steps < 3 && rb_client_frame(w->client[i]) <= w->now && rb_client_frame(w->client[i]) < FRAMES) {
                if (!rb_client_step(w->client[i], controller(i, rb_client_frame(w->client[i]))))
                    break;
                steps++;
            }
            if (steps == 0)
                rb_client_idle(w->client[i]);
        }
    }
    frames = w->confirmed < FRAMES ? w->confirmed : FRAMES;
    toy_init(&offline, players);
    for (f = 0; f < frames; f++) {
        toy_game copy;
        uint64_t h;
        offline_frame(w, &offline, f);
        toy_ops.save(&offline, &copy);
        h = toy_ops.hash_state(&copy);
        if (h != w->host_hash[f])
            wrong++;
        for (i = 0; i < players; i++) {
            /* The mover's first client reported the frames before it left; the second one from the join on. */
            if (i == mover && f >= kept && f < joined)
                continue;
            if (w->reported[i] > f && w->final[i][f] != h)
                wrong++;
        }
    }
    for (i = 0; i < players; i++) {
        if (w->reported[i] < FRAMES)
            clients_ok = 0;
        desyncs += (int)rb_client_desyncs(w->client[i]);
    }
    snprintf(what, sizeof what, "%s: a player leaves at frame %u and joins again at frame %u; every client final on all "
             "%d frames", net->name, leave_at, joined, FRAMES);
    check(!gone && joined > leave_at && clients_ok, what);
    snprintf(what, sizeof what, "%s: absent from frame %u, present again from frame %u in the confirmed inputs",
             net->name, left, joined);
    check(w->log[left - 1][mover].present && !w->log[left][mover].present && !w->log[joined - 1][mover].present &&
          w->log[joined][mover].present && w->log_event[joined].len == 3, what);
    snprintf(what, sizeof what, "%s: with a join, every final state equals the offline run (%u differ), %u mismatches, "
             "%d desync reports", net->name, wrong, rb_host_hash_mismatches(w->host), desyncs);
    check(wrong == 0 && !w->bad_order && rb_host_hash_mismatches(w->host) == 0 && desyncs == 0, what);
    finish(w);
}

static void test_desync(void) {
    world *w = session(&NETWORKS[2], 4, 2, 0);
    char what[160];
    snprintf(what, sizeof what, "a client whose game differs is caught: %u mismatches, %u desync reports to it, %u to "
             "the others", rb_host_hash_mismatches(w->host), rb_client_desyncs(w->client[2]),
             rb_client_desyncs(w->client[0]) + rb_client_desyncs(w->client[1]) + rb_client_desyncs(w->client[3]));
    check(rb_host_hash_mismatches(w->host) > 0 && rb_client_desyncs(w->client[2]) > 0 &&
          rb_client_desyncs(w->client[0]) + rb_client_desyncs(w->client[1]) + rb_client_desyncs(w->client[3]) == 0,
          what);
    finish(w);
}

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* A message for the malformed test: random bytes, or a well-formed message of a random kind (rollback.c gives the
 * layouts) with a few bytes or its length damaged now and then. */
static size_t make_message(world *w, uint8_t *m, size_t cap, int from, uint32_t near) {
    size_t len, k, count;
    uint32_t kind = rnd(w) % 5;
    if (kind == 0) {
        len = rnd(w) % cap;
        for (k = 0; k < len; k++)
            m[k] = (uint8_t)rnd(w);
        return len;
    }
    for (k = 0; k < cap; k++)
        m[k] = (uint8_t)rnd(w);
    switch (kind) {
    case 1: /* input */
        count = rnd(w) % 9;
        m[0] = 1, m[1] = (uint8_t)from;
        put32le(m + 2, near - rnd(w) % 4);
        put32le(m + 6, near + rnd(w) % 300 - 20);
        m[10] = (uint8_t)count;
        len = 11 + 4 * count;
        break;
    case 2: /* confirm */
        count = 1 + rnd(w) % 32;
        m[0] = 2, m[1] = 4;
        put32le(m + 2, near + rnd(w) % 200 - 20);
        m[6] = (uint8_t)count;
        put32le(m + 7, near + rnd(w) % 400);
        len = 11 + 18 * count;
        /* The events after the frames: none, or one for a random frame. */
        if (rnd(w) % 2) {
            m[len] = 0;
            len += 1;
        } else {
            m[len] = 1, m[len + 1] = (uint8_t)(rnd(w) % count), m[len + 2] = (uint8_t)(1 + rnd(w) % RB_EVENT_MAX);
            len += 3 + m[len + 2];
        }
        break;
    case 3: /* hash */
        m[0] = 3, m[1] = (uint8_t)from;
        put32le(m + 2, near - rnd(w) % 300);
        len = 14;
        break;
    default: /* desync */
        m[0] = 4;
        len = 5;
        break;
    }
    if (rnd(w) % 3 == 0)
        m[rnd(w) % len] ^= (uint8_t)(1u << (rnd(w) % 8));
    if (rnd(w) % 5 == 0)
        len = rnd(w) % 2 ? len + 1 : len - 1;
    return len;
}

static void test_malformed(void) {
    toy_game hg, cg;
    world dummy;
    rb_host *h;
    rb_client *c;
    uint8_t m[RB_MAX_MESSAGE + 8];
    int i, refused = 0, accepted = 0;
    memset(&dummy, 0, sizeof dummy);
    toy_init(&hg, 4);
    toy_init(&cg, 4);
    dummy.packets = calloc(MAX_PACKETS, sizeof(packet));
    dummy.net = &NETWORKS[0];
    h = rb_host_new(&toy_ops, &hg, 4, 3, host_send, NULL, NULL, &dummy);
    {
        static endpoint e;
        e.w = &dummy;
        e.id = 1;
        c = rb_client_new(&toy_ops, &cg, 4, 1, 0, client_send, NULL, &e);
    }
    dummy.rng = 777;
    for (i = 0; i < 200000; i++) {
        int from = (int)(rnd(&dummy) % 6) - 1;
        size_t len = make_message(&dummy, m, RB_MAX_MESSAGE, from, rb_host_confirmed(h));
        if (rb_host_receive(h, from, m, len) < 0)
            refused++;
        else
            accepted++;
        len = make_message(&dummy, m, RB_MAX_MESSAGE, 1, rb_client_confirmed(c));
        if (rb_client_receive(c, m, len) < 0)
            refused++;
        else
            accepted++;
        if (i % 64 == 0) {
            rb_host_tick(h);
            rb_client_step(c, controller(1, rb_client_frame(c)));
            dummy.count = 0;
        }
    }
    {
        char what[120];
        snprintf(what, sizeof what, "random messages: %d refused, %d accepted, no crash", refused, accepted);
        check(refused > 0 && accepted > 1000, what);
    }
    check(rb_host_new(&toy_ops, &hg, 0, 3, host_send, NULL, NULL, NULL) == NULL &&
              rb_host_new(&toy_ops, &hg, 17, 3, host_send, NULL, NULL, NULL) == NULL &&
              rb_client_new(&toy_ops, &cg, 4, 4, 0, client_send, NULL, NULL) == NULL &&
              rb_client_new(&toy_ops, &cg, 4, 1, 4, client_send, NULL, NULL) == NULL,
          "bad player counts and windows are refused");
    rb_client_free(c);
    rb_host_free(h);
    free(dummy.packets);
}

/* The hash of one offline run, for comparing builds: the Linux and Windows programs must print the same value. */
static uint64_t offline_hash(void) {
    toy_game g, copy;
    rb_input in[RB_MAX_PLAYERS];
    uint32_t f;
    int p;
    toy_init(&g, RB_MAX_PLAYERS);
    for (f = 0; f < 3000; f++) {
        for (p = 0; p < RB_MAX_PLAYERS; p++)
            in[p] = controller(p, f);
        toy_ops.step(&g, in, RB_MAX_PLAYERS);
    }
    toy_ops.save(&g, &copy);
    return toy_ops.hash_state(&copy);
}

int main(int argc, char **argv) {
    static const int counts[] = {2, 4, 8, 16};
    size_t n, k;
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    for (n = 0; n < sizeof NETWORKS / sizeof NETWORKS[0]; n++)
        for (k = 0; k < sizeof counts / sizeof counts[0]; k++)
            test_session(&NETWORKS[n], counts[k], 0);
    /* A small window, as the real game's large savestates need: clients wait more often. */
    for (k = 0; k < sizeof counts / sizeof counts[0]; k++)
        test_session(&NETWORKS[3], counts[k], 16);
    /* With input delay: inputs reach the others ahead of their frames, so fewer frames run again. */
    input_delay = 3;
    for (n = 0; n < sizeof NETWORKS / sizeof NETWORKS[0]; n++)
        test_session(&NETWORKS[n], 4, 0);
    input_delay = 0;
    for (n = 0; n < sizeof NETWORKS / sizeof NETWORKS[0]; n++)
        test_join(&NETWORKS[n]);
    test_desync();
    test_malformed();
    printf("offline run, 16 players, 3000 frames: hash %016llx\n", (unsigned long long)offline_hash());
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
