/* The online game's sound (sound.h). */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/src/port/soundtable.h"

/* weemitt's g64players.h. */
int G64SoundListener(int player, float *pos, float *yaw, int *maxDist);

/* The sounds started for the game's events, kept while a rollback can still take their frame back. */
#define PLAYED 512
/* Steps a rollback can go back (the rollback core's window, with room). */
#define BACK 256
/* A sound that a correction adds to a step already handled still plays if it is at most this many steps late (half a
   second): a correction comes at most the host's deadline and the round trip later. */
#define LATE 10
/* Game handles of the sounds without a place, and their backend voices. */
#define MAPPED 256

typedef struct {
    int used;
    unsigned seq, step;
    int kind, id;
    int voice;
} played;

typedef struct {
    int game, voice;
    int vol, pan, pitch, reverb;
} mapped;

typedef struct {
    unsigned key;
    int voice;
} source_voice;

struct g64_sound {
    g64_sound_backend b;
    int fresh;           /* the next update starts from the table as it is, with no older one-shot */
    unsigned stepDone;   /* the events of the steps up to this one were handled */
    played log[PLAYED];
    unsigned logNext;
    mapped map[MAPPED];
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
} listener;

/* The volume (0 to 255) and pan of a sound at x y z for the listener, as the game's PlaySample works them out with
   the first player's camera. 0 when it is too far. vol is the game's volume times 1.8, at most 255. range: the
   radius of PlaySampleRadius or a continuous sound's range. */
static int allAudible;

static int mix(const listener *l, float x, float y, float z, float range, int vol, int *outVol, int *outPan) {
    float dx = x - l->pos[0], dy = y - l->pos[1], dz = z - l->pos[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz), f, v;
    if (allAudible) {
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
    for (i = 0; i < MAPPED; i++)
        if (s->map[i].game) {
            stop_voice(s, s->map[i].voice);
            s->map[i].game = 0;
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

static mapped *find_map(g64_sound *s, int game, int add) {
    int i, free = -1;
    for (i = 0; i < MAPPED; i++) {
        if (s->map[i].game == game)
            return &s->map[i];
        if (free < 0 && s->map[i].game == 0)
            free = i;
    }
    if (!add)
        return NULL;
    if (free < 0) {
        /* Full: the oldest goes on playing until it ends, but can no longer be changed. */
        memmove(&s->map[0], &s->map[1], sizeof(mapped) * (MAPPED - 1));
        free = MAPPED - 1;
    }
    memset(&s->map[free], 0, sizeof s->map[free]);
    s->map[free].game = game;
    return &s->map[free];
}

static void remember(g64_sound *s, const SoundEvent *e, int voice) {
    played *p = &s->log[s->logNext++ % PLAYED];
    p->used = 1;
    p->seq = e->seq;
    p->step = e->step;
    p->kind = e->kind;
    p->id = e->id;
    p->voice = voice;
    s->starts++;
    if (s->b.trace)
        s->b.trace(s->b.user, 1, e->step, e->seq, e->kind, e->id);
}

/* After a rollback the table holds the corrected frames. A sound started for an event that they no longer have was
   started by a wrong prediction: it stops. */
static void check_played(g64_sound *s, const SoundTable *t) {
    int i;
    for (i = 0; i < PLAYED; i++) {
        played *p = &s->log[i];
        const SoundEvent *e;
        if (!p->used)
            continue;
        if (t->step - p->step > BACK) {
            p->used = 0;
            continue;
        }
        e = &t->events[p->seq % SOUNDTABLE_EVENTS];
        if (e->seq != p->seq || e->step != p->step || e->kind != p->kind || e->id != p->id) {
            stop_voice(s, p->voice);
            p->used = 0;
            s->wrong++;
            if (s->b.trace)
                s->b.trace(s->b.user, 2, p->step, p->seq, p->kind, p->id);
        }
    }
}

/* Whether the reader started a sound for this event. */
static int was_played(const g64_sound *s, const SoundEvent *e) {
    int i;
    for (i = 0; i < PLAYED; i++) {
        const played *p = &s->log[i];
        if (p->used && p->seq == e->seq && p->step == e->step && p->kind == e->kind && p->id == e->id)
            return 1;
    }
    return 0;
}

static void handle_event(g64_sound *s, const listener *l, const SoundEvent *e) {
    int vol, pan, voice, i;
    mapped *m;
    switch (e->kind) {
    case SOUND_EV_ONESHOT: {
        int v = e->vol * 18 / 10;
        if (v > 255)
            v = 255;
        if (!mix(l, e->x, e->y, e->z, e->radius, v, &vol, &pan))
            break;
        voice = s->b.start(s->b.user, e->id, vol, pan, e->pitch, e->reverb ? 30 : 0);
        remember(s, e, voice);
        break;
    }
    case SOUND_EV_START:
        /* Another player's own sound without a place is theirs alone. */
        if (e->owner >= 0 && e->owner != l->me)
            break;
        voice = s->b.start(s->b.user, e->id, e->vol, e->pan, e->pitch, e->reverb);
        remember(s, e, voice);
        m = find_map(s, e->handle, 1);
        m->voice = voice;
        m->vol = e->vol;
        m->pan = e->pan;
        m->pitch = e->pitch;
        m->reverb = e->reverb;
        break;
    case SOUND_EV_STOP:
        m = find_map(s, e->handle, 0);
        if (m) {
            stop_voice(s, m->voice);
            m->game = 0;
        }
        break;
    case SOUND_EV_STOP_ALL:
        if (e->vol & 1) {
            for (i = 0; i < PLAYED; i++)
                if (s->log[i].used) {
                    stop_voice(s, s->log[i].voice);
                    s->log[i].voice = 0;
                }
            for (i = 0; i < MAPPED; i++)
                s->map[i].game = 0;
        }
        break;
    case SOUND_EV_VOLUME:
    case SOUND_EV_PITCH:
        m = find_map(s, e->handle, 0);
        if (!m)
            break;
        if (e->kind == SOUND_EV_VOLUME)
            m->vol = e->vol;
        else
            m->pitch = e->pitch;
        if (m->voice)
            s->b.set(s->b.user, m->voice, m->vol, m->pan, m->pitch, m->reverb);
        break;
    default:
        break;
    }
}

static void update_sources(g64_sound *s, const listener *l, const SoundTable *t) {
    int i, j;
    /* Sources the game no longer has, or no longer plays, stop. */
    for (i = 0; i < SOUNDTABLE_SOURCES; i++) {
        source_voice *v = &s->src[i];
        int keep = 0;
        if (!v->key)
            continue;
        for (j = 0; j < SOUNDTABLE_SOURCES; j++)
            if (t->sources[j].key == v->key && t->sources[j].alive)
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
            if (s->src[i].key == src->key)
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
            s->src[free].voice = s->b.start(s->b.user, src->id, vol, pan, src->pitch, src->reverb ? 30 : 0);
        }
    }
}

void g64_sound_update(g64_sound *s, int me) {
    const SoundTable *t = soundtable_get();
    listener l;
    unsigned seq, first;
    if (!s)
        return;
    memset(&l, 0, sizeof l);
    l.me = me;
    allAudible = s->allAudible;
    if (!t || !G64SoundListener(me, l.pos, &l.yaw, &l.maxDist)) {
        g64_sound_reset(s);
        return;
    }
    /* A table older than what was handled is another game's: start afresh. */
    if (!s->fresh && t->step + BACK < s->stepDone)
        g64_sound_reset(s);
    if (s->fresh) {
        s->fresh = 0;
        s->stepDone = t->step;
    } else {
        check_played(s, t);
        first = t->seq > SOUNDTABLE_EVENTS ? t->seq - SOUNDTABLE_EVENTS : 0;
        for (seq = first; seq < t->seq; seq++) {
            const SoundEvent *e = &t->events[seq % SOUNDTABLE_EVENTS];
            if (e->seq != seq)
                continue;
            /* Events of steps already handled were played. A start that a correction added to one of the last
               few steps plays late; an older one does not. */
            if (e->step > s->stepDone ||
                (e->step + LATE > s->stepDone && (e->kind == SOUND_EV_ONESHOT || e->kind == SOUND_EV_START) &&
                 !was_played(s, e)))
                handle_event(s, &l, e);
        }
        s->stepDone = t->step;
    }
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
