/* The online mode of the window program (weemitt's studio build at 64 bits): the direct-mode session, the rollback
 * core and the game, put together. weemitt/src/port/platform.c calls these functions.
 *
 * One player hosts (--host PORT, --players N); the others join by address (--join HOST[:PORT]). The game starts when
 * everyone is in. Every machine then runs the same game: player 0 is the host, and each joiner controls the glove
 * the host gave it. */
#ifndef G64_NETPLAY_H
#define G64_NETPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Engine Engine;

/* Connects before the game starts: hosts on port hostPort when join is NULL, or joins the address join. A host
 * confirms each frame deadline frames after its own clock (0: 5 frames, 250 ms; more suits players far apart). It
 * waits (printing progress on standard output) until the game starts, then gives the level and the number of
 * players. Returns 0, or -1 with a message in err. */
int netplay_connect(const char *join, int hostPort, int players, int deadline, const uint8_t *rom, size_t romSize,
                    int *level, int *outPlayers, char *err, size_t errSize);
/* 1 after a successful netplay_connect. */
int netplay_active(void);
/* After the game has started at the level (port_game_init) and the engine is attached: sets up the rollback. */
int netplay_begin(Engine *e, char *err, size_t errSize);
/* One game frame (20 a second in a level): sends the local controller, runs what the rollback needs, and gives the
 * display-list tasks of the newest frame (weemitt's PortFrameTask). Returns their count, or -1 for none. */
int netplay_frame(uint16_t buttons, int8_t stickX, int8_t stickY, void *tasks, int max);
/* After the window has drawn the frame that netplay_frame gave: when that frame showed the local player's own view,
 * puts the game back as it was. */
void netplay_after_draw(void);
/* A line for the window's title: the player number, the frames ahead of the confirmed ones, desyncs. */
const char *netplay_status(void);
/* 1 when the session has failed; netplay_status says why. */
int netplay_failed(void);

#ifdef __cplusplus
}
#endif

#endif
