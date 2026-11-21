/* The sound table (soundtable.h). */
#include <stddef.h>
#include <string.h>

#include "soundtable.h"

static int enabled;
static int owner = -1;
static int wrapping;
/* Zero until soundtable_enable sets it up; the record functions do nothing until then. */
static SoundTable table;

void soundtable_enable(int on) {
    enabled = on != 0;
    memset(&table, 0, sizeof table);
    table.song = -1;
    table.masterMusic = table.masterEffects = -1;
    owner = -1;
    wrapping = 0;
}

int soundtable_enabled(void) { return enabled; }

const SoundTable *soundtable_get(void) { return enabled ? &table : 0; }

void soundtable_set_owner(int player) { owner = player; }

void soundtable_step(void) {
    owner = -1;
    if (enabled)
        table.step++;
}

void soundtable_wrap(int on) {
    wrapping += on ? 1 : -1;
    if (wrapping < 0)
        wrapping = 0;
}

void soundtable_oneshot(int id, const float *pos, int vol, int pitch, float radius, int reverb) {
    SoundEvent *e;
    if (!enabled || wrapping || id == 0 || pos == 0)
        return;
    e = &table.events[table.seq % SOUNDTABLE_EVENTS];
    memset(e, 0, sizeof *e);
    e->step = table.step;
    e->seq = table.seq++;
    e->owner = owner;
    e->id = id;
    e->vol = vol;
    e->pitch = pitch;
    e->reverb = reverb;
    e->radius = radius;
    e->x = pos[0], e->y = pos[1], e->z = pos[2];
}

/* A source by its structure's address and the player whose update it belongs to: a player's own sounds live in
   structures that every player's update uses in turn (g64players.c swaps them). */
static SoundSource *find_source(unsigned int key, int add) {
    int i, free = -1;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++) {
        if (table.sources[i].key == key && table.sources[i].owner == owner)
            return &table.sources[i];
        if (free < 0 && table.sources[i].key == 0)
            free = i;
    }
    if (!add)
        return 0;
    if (free < 0) {
        /* Full: the entry not touched for longest goes. */
        unsigned int oldest = 0xFFFFFFFFu;
        for (i = 0; i < SOUNDTABLE_SOURCES; i++)
            if (table.sources[i].step < oldest) {
                oldest = table.sources[i].step;
                free = i;
            }
    }
    memset(&table.sources[free], 0, sizeof table.sources[free]);
    table.sources[free].key = key;
    table.sources[free].owner = owner;
    return &table.sources[free];
}

void soundtable_source(const void *key, int id, int handle, const float *pos, int vol, int pitch, float range,
                       int reverb, int alive) {
    SoundSource *s;
    if (!enabled || key == 0)
        return;
    s = find_source((unsigned int)(size_t)key, 1);
    s->id = id;
    s->handle = handle;
    s->vol = vol;
    s->pitch = pitch;
    s->reverb = reverb;
    s->alive = alive;
    s->step = table.step;
    s->range = range;
    if (pos)
        s->x = pos[0], s->y = pos[1], s->z = pos[2];
}

void soundtable_source_stop(const void *key) {
    SoundSource *s;
    if (!enabled || key == 0)
        return;
    s = find_source((unsigned int)(size_t)key, 0);
    if (s) {
        s->alive = 0;
        s->handle = 0;
        s->step = table.step;
    }
}

static SoundVoice *find_voice(int handle) {
    int i;
    for (i = 0; i < SOUNDTABLE_VOICES; i++)
        if (table.voices[i].handle == handle)
            return &table.voices[i];
    return 0;
}

void soundtable_start(int handle, int id, int vol, int pan, int reverb) {
    SoundVoice *v;
    int i;
    if (!enabled || wrapping || handle == 0)
        return;
    v = find_voice(0);
    if (!v) {
        /* Full: the voice changed longest ago goes; a sound that is still heard plays on without its entry. */
        unsigned int oldest = 0xFFFFFFFFu;
        for (i = 0; i < SOUNDTABLE_VOICES; i++)
            if (table.voices[i].touched < oldest) {
                oldest = table.voices[i].touched;
                v = &table.voices[i];
            }
    }
    memset(v, 0, sizeof *v);
    v->handle = handle;
    v->owner = owner;
    v->id = id;
    v->vol = vol;
    v->pan = pan;
    v->pitch = 128;
    v->reverb = reverb;
    v->step = v->touched = table.step;
}

void soundtable_stop(int handle) {
    SoundVoice *v;
    int i;
    if (!enabled || handle == 0)
        return;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        if (table.sources[i].key && table.sources[i].handle == handle)
            table.sources[i].handle = 0;
    v = find_voice(handle);
    if (v)
        memset(v, 0, sizeof *v);
}

void soundtable_stop_all(int flags) {
    int i;
    if (!enabled)
        return;
    if (flags & 1) {
        for (i = 0; i < SOUNDTABLE_SOURCES; i++)
            table.sources[i].alive = table.sources[i].handle = 0;
        memset(table.voices, 0, sizeof table.voices);
    }
    if (flags & 2)
        table.song = -1;
}

void soundtable_volume(int handle, int vol) {
    SoundVoice *v;
    if (!enabled || handle == 0 || (v = find_voice(handle)) == 0)
        return;
    v->vol = vol;
    v->touched = table.step;
}

void soundtable_pitch(int handle, int pitch) {
    SoundVoice *v;
    if (!enabled || handle == 0 || (v = find_voice(handle)) == 0)
        return;
    v->pitch = pitch;
    v->touched = table.step;
}

void soundtable_reverb(int handle, int reverb) {
    SoundVoice *v;
    int i;
    if (!enabled || handle == 0)
        return;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        if (table.sources[i].key && table.sources[i].handle == handle)
            table.sources[i].reverb = reverb;
    if ((v = find_voice(handle)) != 0)
        v->reverb = reverb;
}

void soundtable_song(int song) {
    if (!enabled)
        return;
    table.song = song;
    table.songStep = table.step;
}

void soundtable_master(int flags, int vol) {
    if (!enabled)
        return;
    if (flags & 2)
        table.masterMusic = vol;
    if (flags & 1)
        table.masterEffects = vol;
}
