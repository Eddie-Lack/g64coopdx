/* g64coopdx: the library's sound player driven from outside the game (glover.h, glover_sound). The online program's
   own game runs with the stand-in sound player, which plays nothing; this library's real player plays what that game
   asks for. Its game does not run: only its sound player does, on the clock that glover_sound_advance gives it. */
#include <string.h>

#include "lib.h"

/* The sound player (src/audio/mus) and the game's song loader (src/game/sound.c), C linkage. */
int MusStartEffect2(int number, int volume, int pan, int restartflag, int priority);
int MusHandleStop(int handle, int speed);
int MusHandleSetVolume(int handle, int volume);
int MusHandleSetPan(int handle, int pan);
int MusHandleSetFreqOffset(int handle, float offset);
int MusHandleSetTempo(int handle, int tempo);
int MusHandleSetReverb(int handle, int reverb);
void MusStop(int flags, int speed);
void MusSetMasterVolume(int flags, int volume);
void PrepareSong(unsigned char song);
void musAudioAdvance(float retraces);

typedef struct {
    const GloverSoundCmd *cmds;
    uint32_t count;
    int32_t *handles;
} SoundCall;

static void call_sound(void *arg) {
    SoundCall *s = (SoundCall *)arg;
    uint32_t i;
    for (i = 0; i < s->count; i++) {
        const GloverSoundCmd *c = &s->cmds[i];
        int32_t handle = 0;
        switch (c->op) {
        case GLOVER_SOUND_SONG:
            if (c->a >= 0 && c->a < 256)
                PrepareSong((unsigned char)c->a);
            break;
        case GLOVER_SOUND_EFFECT:
            handle = MusStartEffect2(c->a, c->b, c->c, 0, -1);
            if (handle != 0) {
                MusHandleSetFreqOffset(handle, c->freq);
                MusHandleSetTempo(handle, c->e);
                if (c->f)
                    MusHandleSetReverb(handle, c->f);
            }
            break;
        case GLOVER_SOUND_STOP:
            if (c->a != 0)
                MusHandleStop(c->a, c->b);
            break;
        case GLOVER_SOUND_SET:
            if (c->a != 0) {
                MusHandleSetVolume(c->a, c->b);
                MusHandleSetPan(c->a, c->c);
                MusHandleSetFreqOffset(c->a, c->freq);
                MusHandleSetTempo(c->a, c->e);
                MusHandleSetReverb(c->a, c->f);
            }
            break;
        case GLOVER_SOUND_STOP_ALL:
            MusStop(c->a, c->b);
            break;
        case GLOVER_SOUND_MASTER:
            MusSetMasterVolume(c->a, c->b);
            break;
        default:
            break;
        }
        if (s->handles != NULL)
            s->handles[i] = handle;
    }
}

GLOVER_API int32_t glover_sound(Glover *g, const GloverSoundCmd *cmds, uint32_t count, int32_t *handles) {
    SoundCall s;
    if (g == NULL || g != lib_game || (cmds == NULL && count > 0))
        return GLOVER_ERR_ARG;
    s.cmds = cmds;
    s.count = count;
    s.handles = handles;
    return enter(g, call_sound, &s);
}

static void call_advance(void *arg) {
    musAudioAdvance(*(float *)arg);
}

GLOVER_API int32_t glover_sound_advance(Glover *g, float retraces) {
    if (g == NULL || g != lib_game || !(retraces >= 0.0f) || retraces > 600.0f)
        return GLOVER_ERR_ARG;
    return enter(g, call_advance, &retraces);
}
