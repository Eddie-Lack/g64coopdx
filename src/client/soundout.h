/* The online game's audible sound: the real sound player, in a second copy of the game's library built with it
 * (weemitt's make lib64-sound or lib-windows64-sound, shipped as g64coopdx-sound.so or g64coopdx-sound.dll next to the
 * program), and an SDL audio device. The library's own game never runs; only its sound player does, on the clock that
 * soundout_tick gives it. Its memory is apart from the online game's, so nothing it does reaches the game's state.
 * The reader (sound.h) tells it what to play through soundout_backend. Without the library or a device there's no
 * sound, and the game plays on. */
#ifndef G64_SOUNDOUT_H
#define G64_SOUNDOUT_H

#include "sound.h"

/* Loads the library and opens the device. rom is the player's ROM. Returns 0, or -1 when there's no sound. */
int soundout_open(const unsigned char *rom, unsigned long romSize);
void soundout_close(void);
/* The backend for the reader, or NULL when there's no sound. */
const g64_sound_backend *soundout_backend(void);
/* Once per window frame: runs the player ahead of the device. */
void soundout_tick(void);
/* The player's own volumes, in percent: the master volume of the device, and the songs and the effects. */
void soundout_volumes(int master, int music, int effects);

#endif
