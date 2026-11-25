/* The online game's sound (docs/discovery/netcode.md, "Sound and rollback"). The game runs with the stand-in sound
 * player, and records what it asks for in its sound table (weemitt's src/port/soundtable.h), which savestates hold and
 * a rollback restores. Once per window frame, the reader compares the local game's table with what plays and tells a
 * backend (the real sound player, in a second copy of the game's library) what to start, change and stop:
 *   - only the local game's table: not the host's copies of other instances, not a log being run after a move;
 *   - each sound once: a frame run again after a rollback plays nothing again;
 *   - a sound that a wrong prediction started stops when the corrected frames no longer have it, and a stop or a
 *     change that a correction brings is applied: sounds without a place and continuous sounds are followed as the
 *     table's state, not as a list of calls;
 *   - mixed for the local player's camera: sounds with a place get their volume and pan from it, and the ones it is
 *     too far from aren't played;
 *   - another player's own sounds without a place (a camera click) aren't played.
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
    /* For tests, may be NULL: each one-shot with a place that the reader played (what 1), and each one whose sound
       it stopped because a rollback took it back (what 2). A one-shot is its step, its place among the same ones in
       that step (nth), its player and its effect. */
    void (*trace)(void *user, int what, unsigned step, unsigned nth, int owner, int id);
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
/* For tests: the one-shots started so far, and the one-shots stopped so far because a rollback took them back. */
unsigned g64_sound_starts(const g64_sound *s);
unsigned g64_sound_wrong(const g64_sound *s);
/* For tests: the voices without a place and the continuous sounds that play now. */
void g64_sound_active(const g64_sound *s, int *voices, int *sources);

#endif
