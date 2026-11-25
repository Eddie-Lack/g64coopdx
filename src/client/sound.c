/* The online game's sound (sound.h). */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/src/port/soundtable.h"

/* weemitt's g64players.h. */
int G64SoundListener(int player, float *pos, float *yaw, int *maxDist);

/* The one-shots started, kept while a rollback can still take their step back. */
#define PLAYED 512
/* Steps a rollback can go back (more than the rollback core's window). */
#define BACK 256
/* A sound that a correction adds to a step already handled still plays if it's at most this many steps late (half a
   second): a correction comes at most the host's deadline and the round trip later. */
#define LATE 10
/* A voice without a place that the game changed this recently is a loop: a reader that finds it starts it, however
   long ago it started (ambience that a level starts as it loads). */
#define TOUCHED 2

/* A one-shot, by what it is: its step, its effect, its player, and its place among the same ones in that step. A
   correction that adds or takes away another sound leaves it the same. */
typedef struct {
    unsigned step;
    int id, owner, nth;
} shot_key;

typedef struct {
    int used;
    shot_key key;
    int voice;
} played;

typedef struct {
    int handle, id; /* the game's voice: handle 0 for a free entry */
    int voice;      /* the backend's, 0 when it isn't heard */
    int vol, pitch, reverb;
} voice_map;

typedef struct {
    unsigned key;
    int owner, id;
    int voice;
} source_voice;

struct g64_sound {
    g64_sound_backend b;
    int fresh;         /* the next update starts from the table as it is, with no older one-shot */
    unsigned stepDone; /* the one-shots of the steps up to this one were handled */
    played log[PLAYED];
    unsigned logNext;
    voice_map voices[SOUNDTABLE_VOICES];
    source_voice src[SOUNDTABLE_SOURCES];
    int song, music, effects;
    unsigned starts, wrong;
    int allAudible;
};

static float aabs(float a) {
    const float twopi = 6.2831855f;
    a = a > twopi ? a - twopi : a;
    return a < 0.0f ? a + twopi : a;
}

/* The game's FindShortestAngle (maths.c): 0 to pi. */
static float shortest(float a, float b) {
    const float pi = 3.1415927f, twopi = 6.2831855f;
    float t = a - b, r;
    if (fabsf(t) > pi) {
        r = a > b ? b - a : t;
        r += twopi;
    } else {
        r = fabsf(t);
    }
    return r;
}

typedef struct {
    float pos[3], yaw;
    int maxDist;
    int me;
    int allAudible;
} listener;

/* The volume (0 to 255) and pan of a sound at x y z for the listener, as the game's PlaySample works them out with
   the first player's camera. 0 when it's too far. vol is the game's volume times 1.8, at most 255. range: the
   radius of PlaySampleRadius or a continuous sound's range. */
static int mix(const listener *l, float x, float y, float z, float range, int vol, int *outVol, int *outPan) {
    float dx = x - l->pos[0], dy = y - l->pos[1], dz = z - l->pos[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz), f, v;
    if (l->allAudible) {
        *outVol = vol > 255 ? 255 : vol;
        *outPan = 128;
        return 1;
    }
    if (l->maxDist <= 0)
        return 0;
    if (range != 0.0f)
        dist = dist - range < 0.0f ? 0.0f : dist - range;
    if (dist > (float)l->maxDist)
        return 0;
    f = ((float)l->maxDist - dist) / (float)l->maxDist;
    v = (float)vol * f;
    if (v > 255.0f)
        v = 255.0f;
    *outVol = (int)v;
    *outPan = (int)(shortest(aabs(l->yaw + 1.570796327f), aabs(atan2f(dx, dz))) * 81.16902096626815f);
    return 1;
}

g64_sound *g64_sound_new(const g64_sound_backend *backend) {
    g64_sound *s;
    if (!backend || !backend->start || !backend->set || !backend->stop || !backend->song || !backend->master)
        return NULL;
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->b = *backend;
    s->fresh = 1;
    s->song = -1;
    s->music = s->effects = -1;
    return s;
}

static void stop_voice(g64_sound *s, int voice) {
    if (voice)
        s->b.stop(s->b.user, voice);
}

/* Stops every effect the reader started. */
static void stop_all(g64_sound *s) {
    int i;
    for (i = 0; i < PLAYED; i++)
        if (s->log[i].used) {
            stop_voice(s, s->log[i].voice);
            s->log[i].used = 0;
        }
    for (i = 0; i < SOUNDTABLE_VOICES; i++)
        if (s->voices[i].handle) {
            stop_voice(s, s->voices[i].voice);
            s->voices[i].handle = 0;
        }
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        if (s->src[i].key) {
            stop_voice(s, s->src[i].voice);
            s->src[i].key = 0;
        }
}

void g64_sound_free(g64_sound *s) {
    if (!s)
        return;
    stop_all(s);
    free(s);
}

void g64_sound_reset(g64_sound *s) {
    stop_all(s);
    s->fresh = 1;
}

/* The oldest step whose one-shots the table's ring still holds in full. */
static unsigned oldest_whole_step(const SoundTable *t) {
    if (t->seq <= SOUNDTABLE_EVENTS)
        return 0;
    /* The oldest one kept may share its step with one already overwritten. */
    return t->events[t->seq % SOUNDTABLE_EVENTS].step + 1;
}

/* The key of the table's one-shot number seq. */
static shot_key key_of(const SoundTable *t, unsigned seq) {
    const SoundEvent *e = &t->events[seq % SOUNDTABLE_EVENTS];
    shot_key k;
    unsigned q;
    k.step = e->step;
    k.id = e->id;
    k.owner = e->owner;
    k.nth = 0;
    for (q = seq; q > 0 && t->seq - q < SOUNDTABLE_EVENTS;) {
        const SoundEvent *p = &t->events[--q % SOUNDTABLE_EVENTS];
        if (p->step != e->step)
            break;
        if (p->id == e->id && p->owner == e->owner)
            k.nth++;
    }
    return k;
}

static int same_key(shot_key a, shot_key b) {
    return a.step == b.step && a.id == b.id && a.owner == b.owner && a.nth == b.nth;
}

/* Whether the table has this one-shot. */
static int table_has(const SoundTable *t, shot_key k) {
    unsigned seq, first = t->seq > SOUNDTABLE_EVENTS ? t->seq - SOUNDTABLE_EVENTS : 0;
    for (seq = t->seq; seq > first; seq--) {
        const SoundEvent *e = &t->events[(seq - 1) % SOUNDTABLE_EVENTS];
        if (e->step < k.step)
            break;
        if (e->step == k.step && e->id == k.id && e->owner == k.owner && same_key(key_of(t, seq - 1), k))
            return 1;
    }
    return 0;
}

static int was_played(const g64_sound *s, shot_key k) {
    int i;
    for (i = 0; i < PLAYED; i++)
        if (s->log[i].used && same_key(s->log[i].key, k))
            return 1;
    return 0;
}

/* After a rollback the table holds the corrected frames. A one-shot that the corrected frames no longer have in its
   step was started by a wrong prediction: it stops. A step that the ring no longer holds in full can't be checked:
   its sounds play on. */
static void check_played(g64_sound *s, const SoundTable *t) {
    unsigned whole = oldest_whole_step(t);
    int i;
    for (i = 0; i < PLAYED; i++) {
        played *p = &s->log[i];
        if (!p->used)
            continue;
        if (t->step - p->key.step > BACK || p->key.step < whole) {
            p->used = 0;
            continue;
        }
        if (!table_has(t, p->key)) {
            stop_voice(s, p->voice);
            p->used = 0;
            s->wrong++;
            if (s->b.trace)
                s->b.trace(s->b.user, 2, p->key.step, (unsigned)p->key.nth, p->key.owner, p->key.id);
        }
    }
}

static void play_shot(g64_sound *s, const listener *l, const SoundEvent *e, shot_key k) {
    int v = e->vol * 18 / 10, vol, pan, voice;
    played *p;
    if (v > 255)
        v = 255;
    if (!mix(l, e->x, e->y, e->z, e->radius, v, &vol, &pan))
        return;
    voice = s->b.start(s->b.user, e->id, vol, pan, e->pitch, e->reverb ? 30 : 0);
    p = &s->log[s->logNext++ % PLAYED];
    p->used = 1;
    p->key = k;
    p->voice = voice;
    s->starts++;
    if (s->b.trace)
        s->b.trace(s->b.user, 1, k.step, (unsigned)k.nth, k.owner, k.id);
}

static void update_shots(g64_sound *s, const listener *l, const SoundTable *t) {
    unsigned seq, first = t->seq > SOUNDTABLE_EVENTS ? t->seq - SOUNDTABLE_EVENTS : 0;
    check_played(s, t);
    for (seq = first; seq < t->seq; seq++) {
        const SoundEvent *e = &t->events[seq % SOUNDTABLE_EVENTS];
        shot_key k;
        if (e->seq != seq || e->step + LATE <= s->stepDone)
            continue;
        k = key_of(t, seq);
        /* The steps already handled were played; a one-shot that a correction added to one of the last few plays
           late. */
        if (e->step > s->stepDone || !was_played(s, k))
            play_shot(s, l, e, k);
    }
}

/* Sounds without a place: the reader follows the table's voices. */
static void update_voices(g64_sound *s, const listener *l, const SoundTable *t) {
    int i, j;
    for (i = 0; i < SOUNDTABLE_VOICES; i++) {
        voice_map *m = &s->voices[i];
        int keep = 0;
        if (!m->handle)
            continue;
        for (j = 0; j < SOUNDTABLE_VOICES; j++)
            if (t->voices[j].handle == m->handle && t->voices[j].id == m->id)
                keep = 1;
        if (!keep) {
            /* The game stopped it, or a rollback took its start back. */
            stop_voice(s, m->voice);
            m->handle = 0;
        }
    }
    for (j = 0; j < SOUNDTABLE_VOICES; j++) {
        const SoundVoice *v = &t->voices[j];
        voice_map *m = NULL, *free = NULL;
        if (!v->handle)
            continue;
        for (i = 0; i < SOUNDTABLE_VOICES; i++) {
            if (s->voices[i].handle == v->handle && s->voices[i].id == v->id)
                m = &s->voices[i];
            else if (!free && !s->voices[i].handle)
                free = &s->voices[i];
        }
        if (m) {
            if (m->voice && (m->vol != v->vol || m->pitch != v->pitch || m->reverb != v->reverb))
                s->b.set(s->b.user, m->voice, v->vol, v->pan, v->pitch, v->reverb);
        } else if (free) {
            free->handle = v->handle;
            free->id = v->id;
            free->voice = 0;
            /* Another player's own sound without a place is theirs alone. A sound found long after its start plays
               only if it's a loop the game still changes. */
            if ((v->owner < 0 || v->owner == l->me) &&
                (t->step - v->step <= LATE || t->step - v->touched <= TOUCHED))
                free->voice = s->b.start(s->b.user, v->id, v->vol, v->pan, v->pitch, v->reverb);
            m = free;
        } else {
            continue;
        }
        m->vol = v->vol;
        m->pitch = v->pitch;
        m->reverb = v->reverb;
    }
}

static void update_sources(g64_sound *s, const listener *l, const SoundTable *t) {
    int i, j;
    /* Sources the game no longer has, no longer plays, or plays with another sample: their sound stops. */
    for (i = 0; i < SOUNDTABLE_SOURCES; i++) {
        source_voice *v = &s->src[i];
        int keep = 0;
        if (!v->key)
            continue;
        for (j = 0; j < SOUNDTABLE_SOURCES; j++)
            if (t->sources[j].key == v->key && t->sources[j].owner == v->owner && t->sources[j].alive &&
                t->sources[j].id == v->id)
                keep = 1;
        if (!keep) {
            stop_voice(s, v->voice);
            v->key = 0;
        }
    }
    for (j = 0; j < SOUNDTABLE_SOURCES; j++) {
        const SoundSource *src = &t->sources[j];
        source_voice *v = NULL;
        int vol, pan, free = -1, audible;
        if (!src->key || !src->alive)
            continue;
        for (i = 0; i < SOUNDTABLE_SOURCES; i++) {
            if (s->src[i].key == src->key && s->src[i].owner == src->owner)
                v = &s->src[i];
            else if (free < 0 && !s->src[i].key)
                free = i;
        }
        audible = mix(l, src->x, src->y, src->z, src->range, src->vol, &vol, &pan);
        if (v && !audible) {
            stop_voice(s, v->voice);
            v->key = 0;
        } else if (v) {
            s->b.set(s->b.user, v->voice, vol, pan, src->pitch, src->reverb ? 30 : 0);
        } else if (audible && free >= 0) {
            s->src[free].key = src->key;
            s->src[free].owner = src->owner;
            s->src[free].id = src->id;
            s->src[free].voice = s->b.start(s->b.user, src->id, vol, pan, src->pitch, src->reverb ? 30 : 0);
        }
    }
}

void g64_sound_update(g64_sound *s, int me) {
    const SoundTable *t = soundtable_get();
    listener l;
    if (!s)
        return;
    memset(&l, 0, sizeof l);
    l.me = me;
    l.allAudible = s->allAudible;
    if (!t || !G64SoundListener(me, l.pos, &l.yaw, &l.maxDist)) {
        g64_sound_reset(s);
        return;
    }
    /* A table older than what was handled is another game's: start afresh. */
    if (!s->fresh && t->step + BACK < s->stepDone)
        g64_sound_reset(s);
    if (s->fresh)
        s->fresh = 0;
    else
        update_shots(s, &l, t);
    s->stepDone = t->step;
    update_voices(s, &l, t);
    update_sources(s, &l, t);
    if (t->song != s->song) {
        s->song = t->song;
        s->b.song(s->b.user, t->song > 0 ? t->song : 0);
    }
    if ((t->masterMusic >= 0 || t->masterEffects >= 0) && (t->masterMusic != s->music || t->masterEffects != s->effects)) {
        s->music = t->masterMusic;
        s->effects = t->masterEffects;
        s->b.master(s->b.user, s->music, s->effects);
    }
}

void g64_sound_test_all_audible(g64_sound *s, int on) {
    if (s)
        s->allAudible = on;
}

unsigned g64_sound_starts(const g64_sound *s) { return s ? s->starts : 0; }
unsigned g64_sound_wrong(const g64_sound *s) { return s ? s->wrong : 0; }

void g64_sound_active(const g64_sound *s, int *voices, int *sources) {
    int i;
    *voices = *sources = 0;
    if (!s)
        return;
    for (i = 0; i < SOUNDTABLE_VOICES; i++)
        *voices += s->voices[i].handle && s->voices[i].voice;
    for (i = 0; i < SOUNDTABLE_SOURCES; i++)
        *sources += s->src[i].key != 0;
}
