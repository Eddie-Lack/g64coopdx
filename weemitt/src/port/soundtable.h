/* g64coopdx: the sound table (game change 12, docs/discovery/netcode.md "Sound and rollback"). The online game runs
   with the stand-in sound player, so its logic gets the same answers on every machine and after every rollback. The
   table records what the game asks the sound player to play, with the positions and volumes from before the
   distance cut, so each machine can mix the sound for its own player's camera. It lives in the stand-in's state:
   savestates hold it and a rollback restores it, but the memory hash does not cover it. A reader outside the game
   (g64coopdx's src/client/sound.c) compares it with what plays and drives a real sound player.

   The game calls the record functions from its sound wrappers (src/game/sound.c) and the stand-in (audio_stub.c).
   They do nothing until soundtable_enable(1): weemitt's own programs and tests never record. */
#ifndef SOUNDTABLE_H
#define SOUNDTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Continuous sources (the game's ContinuousSample): by the address of their structure. */
#define SOUNDTABLE_SOURCES 256
/* Events: one-shots, other starts, stops, settings, songs. A ring that the reader reads by step. */
#define SOUNDTABLE_EVENTS 1024

enum {
    SOUND_EV_ONESHOT = 1, /* a sound at a place: id, x y z, vol (0 to 255 before the distance), pitch, radius */
    SOUND_EV_START,       /* a sound without a place: handle, id, vol, pan, pitch (128 normal), reverb */
    SOUND_EV_STOP,        /* handle */
    SOUND_EV_STOP_ALL,    /* flags (1 effects, 2 songs) */
    SOUND_EV_VOLUME,      /* handle, vol */
    SOUND_EV_PITCH        /* handle, pitch */
};

typedef struct {
    unsigned int step;  /* the game update it happened in (soundtable_step) */
    unsigned int seq;   /* its number among all events */
    int kind;
    int owner;          /* the player whose own update made it (g64Member number), or -1 */
    int handle;         /* the game's handle, or 0 */
    int id;
    int vol, pan, pitch, reverb;
    float x, y, z, radius;
} SoundEvent;

typedef struct {
    unsigned int key;    /* the address of the game's structure, 0 for a free entry */
    int id;
    int handle;          /* the game's handle, 0 while the game has it off (also when it cut it by distance) */
    int vol, pitch;      /* vol 0 to 255, before the distance */
    int reverb;
    int alive;           /* playing in the game's eyes, whatever the distance */
    unsigned int step;   /* the last update that touched it */
    float x, y, z, range;
} SoundSource;

typedef struct {
    unsigned int step;                     /* game updates counted */
    unsigned int seq;                      /* events recorded */
    int song;                              /* the song number (PrepareSong), -1 for none */
    unsigned int songStep;                 /* the update the song started in */
    int masterMusic, masterEffects;        /* MusSetMasterVolume, -1 until the game sets it */
    SoundSource sources[SOUNDTABLE_SOURCES];
    SoundEvent events[SOUNDTABLE_EVENTS];  /* event number n is events[n % SOUNDTABLE_EVENTS] */
} SoundTable;

void soundtable_enable(int on);
int soundtable_enabled(void);
/* The table, for the reader; NULL when it is off. */
const SoundTable *soundtable_get(void);
/* The player whose update runs now (g64players.c), or -1. */
void soundtable_set_owner(int player);

/* Called once per game update, before it (the stand-in's musAudioAdvance). */
void soundtable_step(void);
/* The wrappers (sound.c). soundtable_wrap(1) and soundtable_wrap(0) around their own calls of the sound player keep
   the stand-in from recording those calls a second time; they nest. */
void soundtable_wrap(int on);
void soundtable_oneshot(int id, const float *pos, int vol, int pitch, float radius, int reverb);
void soundtable_source(const void *key, int id, int handle, const float *pos, int vol, int pitch, float range,
                       int reverb, int alive);
void soundtable_source_stop(const void *key);
/* The stand-in's calls. */
void soundtable_start(int handle, int id, int vol, int pan, int reverb);
void soundtable_stop(int handle);
void soundtable_stop_all(int flags);
void soundtable_volume(int handle, int vol);
void soundtable_pitch(int handle, int pitch);
void soundtable_reverb(int handle, int reverb);
void soundtable_song(int song);
void soundtable_master(int flags, int vol);

#ifdef __cplusplus
}
#endif

#endif
