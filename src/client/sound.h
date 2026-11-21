/* The online game's sound (docs/discovery/netcode.md, "Sound and rollback"). The game runs with the stand-in sound
 * player, and records what it asks for in its sound table (weemitt's src/port/soundtable.h), which savestates hold and
 * a rollback restores. Once per window frame, the reader compares the local game's table with what plays and tells a
 * backend (the real sound player, in a second copy of the game's library) what to start, change and stop:
 *   - only the local game's table: not the host's copies of other instances, not a log being run after a move;
 *   - each sound once: a frame run again after a rollback plays nothing again;
 *   - a sound that a wrong prediction started stops when the corrected frames no longer have it;
 *   - mixed for the local player's camera: sounds with a place get their volume and pan from it, and the ones it is
 *     too far from are not played;
 *   - another player's own sounds without a place (a camera click) are not played.
 * The reader keeps its own state outside the game. It never changes the game. */
#ifndef G64_SOUND_H
#define G64_SOUND_H

/* What the reader asks for. start returns the backend's handle (0: not started). pitch: 128 is normal. */
typedef struct {
    void *user;
    int (*start)(void *user, int id, int vol, int pan, int pitch, int reverb);
    void (*set)(void *user, int handle, int vol, int pan, int pitch, int reverb);
    void (*stop)(void *user, int handle);
    void (*song)(void *user, int song); /* the game's song number; 0: none */
    void (*master)(void *user, int music, int effects);
    /* For tests, may be NULL: each event of the game's table that the reader played (what 1), and each one whose
       sound it stopped because a rollback took the event back (what 2). */
    void (*trace)(void *user, int what, unsigned step, unsigned seq, int kind, int id);
} g64_sound_backend;

typedef struct g64_sound g64_sound;

g64_sound *g64_sound_new(const g64_sound_backend *backend);
void g64_sound_free(g64_sound *s);
/* Forget what the game's table said before: the local game was built again (a move to another instance, or the
 * start). Every effect stops; the next update starts the song and the loops of the new game, and no older one-shot. */
void g64_sound_reset(g64_sound *s);
/* One window frame, with the local game in the engine (rb_engine_game_enter) and the local player's number. */
void g64_sound_update(g64_sound *s, int me);
/* For tests: every sound with a place is audible, wherever the camera is. */
void g64_sound_test_all_audible(g64_sound *s, int on);
/* For tests: sounds started and stopped so far, and stops of sounds that a wrong prediction started. */
unsigned g64_sound_starts(const g64_sound *s);
unsigned g64_sound_wrong(const g64_sound *s);

#endif
