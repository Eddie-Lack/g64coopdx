/* The online game's sound reader (src/client/sound.c) against the sound table (weemitt/src/port/soundtable.c), without
 * the game or the ROM. The test writes the table as the game would, takes a step back as a rollback does (it saves and
 * restores the table) and checks exactly what the reader asks the backend to start, change and stop:
 *   A. a correction that adds a one-shot before others: the others go on, uncut, and the new one plays late;
 *   B. a correction that takes a one-shot away: only that one stops;
 *   C. sounds without a place: a stop stops them, and a stop that a correction brings to a step already handled too;
 *   D. a reader that starts afresh plays a loop the game still changes, and not an old sound;
 *   E. the same structure used by two players' updates: two continuous sounds, and one player's stop stops only theirs;
 *   F. a continuous sound whose structure plays another sample: the old one stops and the new one starts.
 *
 * Usage: sound_test. Exits with 1 if a check fails. */
#include <stdio.h>
#include <string.h>

#include "../../src/client/sound.h"
#include "../../weemitt/src/port/soundtable.h"

/* The game's side the reader asks: the listener stands at 0 0 0, and hears 1000 units far. */
int G64SoundListener(int player, float *pos, float *yaw, int *maxDist) {
    (void)player;
    pos[0] = pos[1] = pos[2] = 0.0f;
    *yaw = 0.0f;
    *maxDist = 1000;
    return 1;
}

#define LOG 256
static struct {
    char op; /* S start, T stop, E set, M song */
    int id, voice;
} logs[LOG];
static int nlog, nextVoice;
static int idOf[1000]; /* the effect of each backend voice */
static int failures;

static int b_start(void *u, int id, int vol, int pan, int pitch, int reverb) {
    (void)u, (void)vol, (void)pan, (void)pitch, (void)reverb;
    nextVoice++;
    idOf[nextVoice] = id;
    if (nlog < LOG)
        logs[nlog++] = (typeof(logs[0])){'S', id, nextVoice};
    return nextVoice;
}
static void b_set(void *u, int h, int vol, int pan, int pitch, int reverb) {
    (void)u, (void)vol, (void)pan, (void)pitch, (void)reverb;
    if (nlog < LOG)
        logs[nlog++] = (typeof(logs[0])){'E', idOf[h], h};
}
static void b_stop(void *u, int h) {
    (void)u;
    if (nlog < LOG)
        logs[nlog++] = (typeof(logs[0])){'T', idOf[h], h};
}
static void b_song(void *u, int song) {
    (void)u;
    if (nlog < LOG)
        logs[nlog++] = (typeof(logs[0])){'M', song, 0};
}
static void b_master(void *u, int music, int effects) { (void)u, (void)music, (void)effects; }

static const g64_sound_backend backend = {NULL, b_start, b_set, b_stop, b_song, b_master, NULL};

/* The starts or stops of an effect since the log was cleared. */
static int count(char op, int id) {
    int i, n = 0;
    for (i = 0; i < nlog; i++)
        n += logs[i].op == op && logs[i].id == id;
    return n;
}

static int count_op(char op) {
    int i, n = 0;
    for (i = 0; i < nlog; i++)
        n += logs[i].op == op;
    return n;
}

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

static const float here[3] = {10.0f, 0.0f, 10.0f};

static SoundTable *table(void) { return (SoundTable *)soundtable_get(); }

static void shot(int id) { soundtable_oneshot(id, here, 100, 128, 0.0f, 0); }

int main(void) {
    static SoundTable before;
    g64_sound *s;
    char what[200];
    int a, b;

    soundtable_enable(1);
    s = g64_sound_new(&backend);
    g64_sound_update(s, 0); /* fresh */

    /* A: step 1 plays 10 and 11, step 2 plays 12. A correction then adds 99 to step 1, before them. */
    memcpy(&before, table(), sizeof before);
    soundtable_step();
    shot(10), shot(11);
    g64_sound_update(s, 0);
    soundtable_step();
    shot(12);
    g64_sound_update(s, 0);
    nlog = 0;
    memcpy(table(), &before, sizeof before);
    soundtable_step();
    shot(99), shot(10), shot(11);
    soundtable_step();
    shot(12);
    g64_sound_update(s, 0);
    snprintf(what, sizeof what, "A: a correction adds a one-shot before others: %d stops, %d starts of 10, 11 and 12, "
             "%d of the new one", count_op('T'), count('S', 10) + count('S', 11) + count('S', 12), count('S', 99));
    check(count_op('T') == 0 && count('S', 10) + count('S', 11) + count('S', 12) == 0 && count('S', 99) == 1, what);

    /* B: a correction takes 11 away from step 1. */
    nlog = 0;
    memcpy(table(), &before, sizeof before);
    soundtable_step();
    shot(99), shot(10);
    soundtable_step();
    shot(12);
    g64_sound_update(s, 0);
    snprintf(what, sizeof what, "B: a correction takes a one-shot away: %d stops of it, %d other stops, %d starts",
             count('T', 11), count_op('T') - count('T', 11), count_op('S'));
    check(count('T', 11) == 1 && count_op('T') == 1 && count_op('S') == 0, what);

    /* C: a sound without a place starts, then the game stops it. Then a correction brings a stop to a step the reader
       already handled. */
    nlog = 0;
    soundtable_step();
    soundtable_start(5, 20, 100, 64, 0);
    g64_sound_update(s, 0);
    soundtable_step();
    soundtable_stop(5);
    g64_sound_update(s, 0);
    snprintf(what, sizeof what, "C: a sound without a place: %d start, %d stop", count('S', 20), count('T', 20));
    check(count('S', 20) == 1 && count('T', 20) == 1, what);
    nlog = 0;
    soundtable_step();
    soundtable_start(6, 21, 100, 64, 0);
    g64_sound_update(s, 0);
    memcpy(&before, table(), sizeof before);
    soundtable_step();
    g64_sound_update(s, 0);
    memcpy(table(), &before, sizeof before);
    soundtable_step();
    soundtable_stop(6);
    g64_sound_update(s, 0);
    snprintf(what, sizeof what, "C: a stop that a correction brings to a handled step: %d stop", count('T', 21));
    check(count('S', 21) == 1 && count('T', 21) == 1, what);
    g64_sound_free(s);

    /* D: a reader that starts afresh: a loop started long ago that the game still changes plays; an old sound does
       not. */
    nlog = 0;
    soundtable_step();
    soundtable_start(7, 30, 100, 64, 0);
    soundtable_start(8, 31, 100, 64, 0);
    for (a = 0; a < 40; a++) {
        soundtable_step();
        soundtable_volume(7, 90 + a % 2);
    }
    s = g64_sound_new(&backend);
    g64_sound_update(s, 0);
    snprintf(what, sizeof what, "D: afresh: the loop the game changes plays (%d), the old sound does not (%d)",
             count('S', 30), count('S', 31));
    check(count('S', 30) == 1 && count('S', 31) == 0, what);

    /* E: one structure, two players' updates. */
    nlog = 0;
    {
        static int sample; /* stands for a structure that every player's update uses in turn */
        soundtable_step();
        soundtable_set_owner(0);
        soundtable_source(&sample, 40, 1, here, 100, 128, 0.0f, 0, 1);
        soundtable_set_owner(1);
        soundtable_source(&sample, 41, 2, here, 100, 128, 0.0f, 0, 1);
        soundtable_set_owner(-1);
        g64_sound_update(s, 0);
        g64_sound_active(s, &a, &b);
        snprintf(what, sizeof what, "E: two players' sounds in one structure: %d continuous sounds (40: %d, 41: %d)", b,
                 count('S', 40), count('S', 41));
        check(b == 2 && count('S', 40) == 1 && count('S', 41) == 1, what);
        soundtable_step();
        soundtable_set_owner(0);
        soundtable_source(&sample, 40, 1, here, 100, 128, 0.0f, 0, 1);
        soundtable_set_owner(1);
        soundtable_source_stop(&sample);
        soundtable_set_owner(-1);
        g64_sound_update(s, 0);
        snprintf(what, sizeof what, "E: one player's stop stops only theirs: 40 stopped %d, 41 stopped %d",
                 count('T', 40), count('T', 41));
        check(count('T', 40) == 0 && count('T', 41) == 1, what);

        /* F: the structure plays another sample. */
        nlog = 0;
        soundtable_step();
        soundtable_set_owner(0);
        soundtable_source(&sample, 42, 3, here, 100, 128, 0.0f, 0, 1);
        soundtable_set_owner(-1);
        g64_sound_update(s, 0);
        snprintf(what, sizeof what, "F: another sample in the same structure: the old one stops (%d), the new one "
                 "starts (%d)", count('T', 40), count('S', 42));
        check(count('T', 40) == 1 && count('S', 42) == 1, what);
    }
    g64_sound_free(s);
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
