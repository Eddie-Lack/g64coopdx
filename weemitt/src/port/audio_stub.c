/* Temporary stand-ins for the music and sound effect driver (libmus) until src/audio replaces them. These
   functions play nothing. Handles are never 0. The game treats each sound as started. The game waits for a new
   song to report that it plays and for a stopped one to report that it does not. The current song's handle
   is tracked; every other sound reports that it has finished. */
#include "port.h"
#include "soundtable.h"

/* The stand-in's structures point to no function (ptr32.h). */
const PortFn port_audio_fn_table[1];
const unsigned int port_audio_fn_count = 0;

static s32 nextHandle = 1;
static s32 songHandle;

s32 MusInitialize(void *config) {
    return 0;
}

void MusBankInitialize(void *pbank, void *wbank) {
}

s32 MusBankStartSong(void *bank, void *song) {
    songHandle = nextHandle++;
    return songHandle;
}

s32 MusStartEffect2(s32 number, s32 volume, s32 pan, s32 restartflag, s32 priority) {
    s32 handle = nextHandle++;
    /* g64coopdx: what the game asks for goes in the sound table (soundtable.h), from the online game only. */
    soundtable_start(handle, number, volume, pan, 0);
    return handle;
}

void MusStop(s32 flags, s32 speed) {
    songHandle = 0;
    soundtable_stop_all(flags);
}

s32 MusAsk(s32 flags) {
    return 0;
}

s32 MusHandleAsk(s32 handle) {
    return handle != 0 && handle == songHandle;
}

void MusHandleStop(s32 handle, s32 speed) {
    if (handle == songHandle) {
        songHandle = 0;
    }
    soundtable_stop(handle);
}

void MusHandleSetVolume(s32 handle, s32 volume) {
    soundtable_volume(handle, volume);
}

void MusHandleSetPan(s32 handle, s32 pan) {
}

void MusHandleSetFreqOffset(s32 handle, f32 offset) {
}

void MusHandleSetTempo(s32 handle, s32 tempo) {
    /* The game sets the tempo to the pitch it gives (sound.c). */
    soundtable_pitch(handle, tempo);
}

void MusHandleSetReverb(s32 handle, s32 reverb) {
    soundtable_reverb(handle, reverb);
}

void MusSetMasterVolume(s32 flags, s32 volume) {
    soundtable_master(flags, volume);
}

/* The stand-in has no clock to advance. The frame driver calls this once per game update. */
void musAudioAdvance(f32 retraces) {
    soundtable_step();
}
