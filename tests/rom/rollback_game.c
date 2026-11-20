/* The rollback core with the real game (needs the ROM): one host and three clients play level 13 through
 * src/rollback/engine_game.c over a simulated network with delay, jitter and loss.
 *
 * The test: every hash check between the clients and the host passes, and every client's final state equals one
 * offline run of the host's confirmed inputs at every 10th frame. Player 0 drives the game; the clients that do not
 * control it still predict its input and roll back when the prediction was wrong.
 *
 * Usage: rollback_game ROM [frames]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/rollback/engine_game.h"

#define PLAYERS 3
#define MAX_FRAMES 2000
#define MAX_PACKETS 4096
#define LATENCY 3
#define JITTER 2
#define LOSS 50 /* per 1000 messages */
#define DEADLINE 7
#define WINDOW 16

typedef struct {
    uint32_t at;
    int to, from;
    size_t len;
    uint8_t data[RB_MAX_MESSAGE];
} packet;

static packet packets[MAX_PACKETS];
static int npackets;
static uint32_t now, rng = 4242;
static rb_host *host;
static rb_client *client[PLAYERS];
static rb_input confirmed[MAX_FRAMES][RB_MAX_PLAYERS];
static uint32_t nconfirmed;
static uint64_t client_hash[PLAYERS][MAX_FRAMES];
static uint32_t client_final[PLAYERS];
static const rb_game_ops *ops;
static int ends[PLAYERS];

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void queue(int from, int to, const uint8_t *data, size_t len) {
    packet *p;
    if (rnd() % 1000 < LOSS || npackets == MAX_PACKETS)
        return;
    p = &packets[npackets++];
    p->at = now + LATENCY + rnd() % (JITTER + 1);
    p->to = to;
    p->from = from;
    p->len = len;
    memcpy(p->data, data, len);
}

static void client_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)to;
    queue(*(int *)user, -1, data, len);
}

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    queue(-1, to, data, len);
}

static void on_confirm(void *user, uint32_t frame, const rb_input *in, int players, const rb_event *event) {
    (void)user;
    (void)event;
    if (frame < MAX_FRAMES)
        memcpy(confirmed[frame], in, sizeof(rb_input) * (size_t)players);
    nconfirmed = frame + 1;
}

static void on_client_final(void *user, uint32_t frame, const void *state) {
    int id = *(int *)user;
    if (frame < MAX_FRAMES && frame % RB_HASH_EVERY == 0)
        client_hash[id][frame] = ops->hash_state(state);
    client_final[id] = frame + 1;
}

/* Player 0 walks, runs and jumps; a new stick and buttons every 12 frames. */
static rb_input controller(int player, uint32_t frame) {
    uint32_t h = (uint32_t)player * 0x9e3779b1u ^ (frame / 12) * 0x85ebca77u;
    rb_input in;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    in.x = (int8_t)((int)(h % 141) - 70);
    in.y = (int8_t)((int)((h >> 8) % 141) - 70);
    in.buttons = (h >> 20) % 4 == 0 ? 0x8000 : (h >> 22) % 6 == 0 ? 0x2000 : 0;
    in.present = 1;
    return in;
}

static void deliver(void) {
    int i, kept = 0;
    for (i = 0; i < npackets; i++) {
        packet *p = &packets[i];
        if (p->at > now) {
            packets[kept++] = *p;
            continue;
        }
        if (p->to < 0)
            rb_host_receive(host, p->from, p->data, p->len);
        else
            rb_client_receive(client[p->to], p->data, p->len);
    }
    npackets = kept;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    rb_engine_game *start, *host_game, *games[PLAYERS], *offline;
    uint32_t frames = argc > 2 ? (uint32_t)atoi(argv[2]) : 400, f, checked = 0, wrong = 0, end;
    uint64_t rollbacks = 0;
    int i, ok;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (rollback_game ROM [frames])\n");
        return 0;
    }
    fclose(rom);
    if (frames > MAX_FRAMES - 200)
        frames = MAX_FRAMES - 200;
    engine_default_options(&opt);
    opt.startLevel = 13;
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    ops = rb_engine_ops(e);
    start = rb_engine_game_new();
    host_game = rb_engine_game_new();
    host = rb_host_new(ops, host_game, PLAYERS, DEADLINE, host_send, on_confirm, NULL, NULL);
    for (i = 0; i < PLAYERS; i++) {
        ends[i] = i;
        games[i] = rb_engine_game_new();
        client[i] = rb_client_new(ops, games[i], PLAYERS, i, WINDOW, client_send, on_client_final, &ends[i]);
    }
    end = frames + DEADLINE + 4 * (LATENCY + JITTER) + 60;
    for (now = 0; now < end; now++) {
        deliver();
        rb_host_tick(host);
        for (i = 0; i < PLAYERS; i++) {
            int steps = 0;
            while (steps < 3 && rb_client_frame(client[i]) <= now && rb_client_frame(client[i]) < frames) {
                if (!rb_client_step(client[i], controller(i, rb_client_frame(client[i]))))
                    break;
                steps++;
            }
            if (steps == 0)
                rb_client_idle(client[i]);
        }
    }
    /* The offline run: the start state and the host's confirmed inputs, nothing else. */
    offline = start;
    for (f = 0; f < frames && f < nconfirmed; f++) {
        ops->step(offline, confirmed[f], PLAYERS);
        if (f % RB_HASH_EVERY == 0) {
            uint64_t h = rb_engine_game_hash(offline);
            for (i = 0; i < PLAYERS; i++) {
                if (client_final[i] > f) {
                    checked++;
                    if (client_hash[i][f] != h)
                        wrong++;
                }
            }
        }
    }
    /* For comparing builds: the Linux and Windows programs must print the same value. */
    printf("     offline run, %u frames: hash %016llx\n", frames, (unsigned long long)rb_engine_game_hash(offline));
    for (i = 0; i < PLAYERS; i++)
        rollbacks += rb_client_rollback_frames(client[i]);
    ok = 1;
    for (i = 0; i < PLAYERS; i++)
        if (client_final[i] < frames || rb_client_desyncs(client[i]))
            ok = 0;
    printf("%s every client final on all %u frames, no desync reports\n", ok ? "ok  " : "FAIL", frames);
    printf("%s %u hash checks with the host, %u mismatches\n",
           rb_host_hash_matches(host) && !rb_host_hash_mismatches(host) ? "ok  " : "FAIL",
           rb_host_hash_matches(host), rb_host_hash_mismatches(host));
    printf("%s %u client states equal the offline run at every 10th frame, %u differ\n",
           checked && !wrong ? "ok  " : "FAIL", checked, wrong);
    printf("     %llu frames run again after wrong predictions, %u late inputs\n", (unsigned long long)rollbacks,
           rb_host_late_inputs(host));
    ok = ok && rb_host_hash_matches(host) && !rb_host_hash_mismatches(host) && checked && !wrong;
    for (i = 0; i < PLAYERS; i++) {
        rb_client_free(client[i]);
        rb_engine_game_free(games[i]);
    }
    rb_host_free(host);
    rb_engine_game_free(host_game);
    rb_engine_game_free(start);
    engine_free(e);
    return ok ? 0 : 1;
}
