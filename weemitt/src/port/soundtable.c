/* The sound table (soundtable.h). */
#include <string.h>

#include "soundtable.h"

static int enabled;
static int owner = -1;
static int wrapping;
static SoundTable table = {0, 0, -1, 0, -1, -1};

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
    if (enabled)
        table.step++;
}

void soundtable_wrap(int on) {
    wrapping += on ? 1 : -1;
    if (wrapping < 0)
        wrapping = 0;
}

static SoundEvent *add_event(int kind) {
    SoundEvent *e = &table.events[table.seq % SOUNDTABLE_EVENTS];
    memset(e, 0, sizeof *e);
    e->step = table.step;
    e->seq = table.seq++;
    e->kind = kind;
    e->owner = owner;
    return e;
}

void soundtable_oneshot(int id, const float *pos, int vol, int pitch, float radius, int reverb) {
    SoundEvent *e;
    if (!enabled || wrapping || id == 0 || pos == 0)
        return;
    e = add_event(SOUND_EV_ONESHOT);
    e->id = id;
    e->vol = vol;
    e->pitch = pitch;
    e->reverb = reverb;
    e->radius = radius;
    e->x = pos[0], e->y = pos[1], e->z = pos[2];
}

static SoundSource *find_source(unsigned int key, int add) {
    int i, free = -1;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++) {
        if (table.sources[i].key == key)
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

void soundtable_start(int handle, int id, int vol, int pan, int reverb) {
    SoundEvent *e;
    if (!enabled || wrapping)
        return;
    e = add_event(SOUND_EV_START);
    e->handle = handle;
    e->id = id;
    e->vol = vol;
    e->pan = pan;
    e->pitch = 128;
    e->reverb = reverb;
}

void soundtable_stop(int handle) {
    int i;
    if (!enabled || handle == 0)
        return;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        if (table.sources[i].key && table.sources[i].handle == handle)
            table.sources[i].handle = 0;
    if (wrapping)
        return;
    add_event(SOUND_EV_STOP)->handle = handle;
}

void soundtable_stop_all(int flags) {
    int i;
    if (!enabled)
        return;
    if (flags & 1)
        for (i = 0; i < SOUNDTABLE_SOURCES; i++)
            table.sources[i].alive = table.sources[i].handle = 0;
    if (flags & 2)
        table.song = -1;
    add_event(SOUND_EV_STOP_ALL)->vol = flags;
}

void soundtable_volume(int handle, int vol) {
    if (!enabled || wrapping || handle == 0)
        return;
    {
        SoundEvent *e = add_event(SOUND_EV_VOLUME);
        e->handle = handle;
        e->vol = vol;
    }
}

void soundtable_pitch(int handle, int pitch) {
    if (!enabled || wrapping || handle == 0)
        return;
    {
        SoundEvent *e = add_event(SOUND_EV_PITCH);
        e->handle = handle;
        e->pitch = pitch;
    }
}

void soundtable_reverb(int handle, int reverb) {
    SoundEvent *e;
    int i;
    if (!enabled || handle == 0)
        return;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        if (table.sources[i].key && table.sources[i].handle == handle)
            table.sources[i].reverb = reverb;
    /* A start in this update gets it: the game sets the reverb right after the start. */
    if (table.seq > 0) {
        e = &table.events[(table.seq - 1) % SOUNDTABLE_EVENTS];
        if (e->step == table.step && e->handle == handle && e->kind == SOUND_EV_START)
            e->reverb = reverb;
    }
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
