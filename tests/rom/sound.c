/* The online game's sound (src/client/sound.c) with the real game and rollbacks (needs the ROM). One host and three
 * clients play level 13 over a simulated network with delay, jitter and loss, as in rollback_game.c, with the sound
 * table on (weemitt's src/port/soundtable.h).
 *
 * The test: a reader follows clients 0 and 1 after every window frame, and another reader follows an offline run of
 * the host's confirmed inputs, which has no wrong prediction. With every sound audible (g64_sound_test_all_audible),
 * the sounds a client's reader played and did not stop as wrong must all be sounds of the offline run, each played
 * once; the offline run's sounds that a client's reader missed can only be ones a correction brought more than a few
 * steps late. Readers with the real mix start the song and some effects. The Linux and Windows programs must print
 * the same.
 *
 * Usage: sound ROM [frames]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/sound.h"
#include "../../src/rollback/engine_game.h"
#include "../../weemitt/src/port/soundtable.h"

void port_game_start_level(int level);
void PrepareSong(unsigned char song);

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


/* What a reader played: event keys (step, seq, kind, id), with what 1 added and what 2 removed. */
#define MAX_KEYS 20000
typedef struct {
    unsigned step, seq;
    int kind, id;
    int live;
} key;

typedef struct {
    key keys[MAX_KEYS];
    int count;
    int starts, wrongs, voices, songs, lastSong;
} trace;

static int find_key(const trace *t, unsigned step, unsigned seq, int kind, int id) {
    int i;
    for (i = t->count - 1; i >= 0; i--)
        if (t->keys[i].live && t->keys[i].step == step && t->keys[i].seq == seq && t->keys[i].kind == kind &&
            t->keys[i].id == id)
            return i;
    return -1;
}

static void tr_trace(void *user, int what, unsigned step, unsigned seq, int kind, int id) {
    trace *t = user;
    int i = find_key(t, step, seq, kind, id);
    if (what == 1) {
        t->starts++;
        if (i >= 0) {
            /* The same event played twice: counted as a key of its own, which the check finds. */
            t->keys[i].live++;
            return;
        }
        if (t->count < MAX_KEYS)
            t->keys[t->count++] = (key){step, seq, kind, id, 1};
    } else if (what == 2) {
        t->wrongs++;
        if (i >= 0)
            t->keys[i].live = 0;
    }
}

static int tr_start(void *user, int id, int vol, int pan, int pitch, int reverb) {
    trace *t = user;
    (void)id, (void)vol, (void)pan, (void)pitch, (void)reverb;
    return ++t->voices;
}
static void tr_set(void *user, int h, int vol, int pan, int pitch, int reverb) {
    (void)user, (void)h, (void)vol, (void)pan, (void)pitch, (void)reverb;
}
static void tr_stop(void *user, int h) { (void)user, (void)h; }
static void tr_song(void *user, int song) {
    trace *t = user;
    t->songs++;
    t->lastSong = song;
}
static void tr_master(void *user, int music, int effects) { (void)user, (void)music, (void)effects; }

static trace traces[5]; /* clients 0 and 1, the offline run for players 0 and 1, client 0 with the real mix */
static g64_sound *readers[5];

static void new_reader(int n, int audible) {
    g64_sound_backend b = {&traces[n], tr_start, tr_set, tr_stop, tr_song, tr_master, tr_trace};
    readers[n] = g64_sound_new(&b);
    g64_sound_test_all_audible(readers[n], audible);
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    rb_engine_game *start, *host_game, *games[PLAYERS], *offline;
    uint32_t frames = argc > 2 ? (uint32_t)atoi(argv[2]) : 600, f, end;
    int i, p, failed = 0;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (sound ROM [frames])\n");
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
    /* On before the games are copied from the engine, and before the level starts again: every copy records, from
       the level's song on. */
    soundtable_enable(1);
    PrepareSong(0);
    port_game_start_level(13);
    ops = rb_engine_ops(e);
    start = rb_engine_game_new();
    host_game = rb_engine_game_new();
    host = rb_host_new(ops, host_game, PLAYERS, DEADLINE, host_send, on_confirm, NULL, NULL);
    for (i = 0; i < PLAYERS; i++) {
        ends[i] = i;
        games[i] = rb_engine_game_new();
        client[i] = rb_client_new(ops, games[i], PLAYERS, i, WINDOW, client_send, on_client_final, &ends[i]);
    }
    for (i = 0; i < 5; i++)
        new_reader(i, i < 4);
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
        /* The window frame's sound: each reader with its client's game in the engine. */
        rb_engine_game_enter(games[0]);
        g64_sound_update(readers[0], 0);
        g64_sound_update(readers[4], 0);
        rb_engine_game_enter(games[1]);
        g64_sound_update(readers[1], 1);
    }
    /* The offline run: the start state and the host's confirmed inputs, one step at a time. */
    offline = start;
    for (f = 0; f < frames && f < nconfirmed; f++) {
        ops->step(offline, confirmed[f], PLAYERS);
        rb_engine_game_enter(offline);
        g64_sound_update(readers[2], 0);
        g64_sound_update(readers[3], 1);
    }
    for (p = 0; p < 2; p++) {
        const trace *c = &traces[p], *o = &traces[2 + p];
        int extra = 0, twice = 0, missed = 0, played = 0, k;
        for (k = 0; k < c->count; k++) {
            if (!c->keys[k].live)
                continue;
            played++;
            if (c->keys[k].live > 1)
                twice++;
            if (find_key(o, c->keys[k].step, c->keys[k].seq, c->keys[k].kind, c->keys[k].id) < 0)
                extra++;
        }
        for (k = 0; k < o->count; k++)
            if (find_key(c, o->keys[k].step, o->keys[k].seq, o->keys[k].kind, o->keys[k].id) < 0)
                missed++;
        printf("%s client %d: %d sounds played and kept, %d stopped as wrong; none played twice (%d), none that the "
               "offline run does not have (%d)\n", !twice && !extra ? "ok  " : "FAIL", p, played, c->wrongs, twice,
               extra);
        failed |= twice || extra;
        printf("%s client %d: the offline run played %d; %d of them came too late for the client (at most a tenth)\n",
               o->count > 20 && missed * 10 <= o->count ? "ok  " : "FAIL", p, o->count, missed);
        failed |= !(o->count > 20 && missed * 10 <= o->count);
    }
    printf("%s some wrong predictions took sounds back: %d\n", traces[0].wrongs + traces[1].wrongs > 0 ? "ok  " : "FAIL",
           traces[0].wrongs + traces[1].wrongs);
    failed |= traces[0].wrongs + traces[1].wrongs == 0;
    printf("%s with the real mix, client 0 started the song (%d) and %d effects\n",
           traces[4].lastSong > 0 && traces[4].voices > 0 ? "ok  " : "FAIL", traces[4].lastSong, traces[4].voices);
    failed |= !(traces[4].lastSong > 0 && traces[4].voices > 0);
    printf("%s the reader follows rollbacks: each sound once, wrong ones stopped\n", failed ? "FAIL" : "ok  ");
    for (i = 0; i < 5; i++)
        g64_sound_free(readers[i]);
    return failed;
}
