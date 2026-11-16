/* The online mode of the window program (weemitt's studio build at 64 bits): the session, the instances, the
 * rollback core and the game, put together. weemitt/src/port/platform.c calls these functions.
 *
 * One player hosts; the others join by room code or by address. The game starts when everyone is in. Player 0 is the
 * host, and each joiner controls the glove the host gave it. Each level that has players in it is its own game, an
 * instance (instances.h): a machine runs the one its player is in, and the host runs a copy of every one. */
#ifndef G64_NETPLAY_H
#define G64_NETPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's relay, which carries the game for players who join by room code. */
#define NET_RELAY_DEFAULT "g64coopdx-lobby.fly.dev:27065"
/* The letters and digits of a room code (src/net/udp.h's NET_ROOM_CODE). */
#define NETPLAY_ROOM_CODE 6

typedef struct Engine Engine;

/* Connects before the game starts and waits (printing progress on standard output) until it starts; then gives the
 * level and the number of players. Returns 0, or -1 with a message in err. The ways to connect:
 *   relay set, room NULL: host through the relay at relay (HOST[:PORT]); it prints the room's code
 *   relay set, room set: join the room with that code through the relay
 *   relay NULL, join NULL: host directly on UDP port hostPort (the host forwards that port)
 *   relay NULL, join set: join the host at join (HOST[:PORT]) directly
 * A host confirms each frame deadline frames after its own clock (0: 5 frames, 250 ms). */
int netplay_connect(const char *join, int hostPort, const char *relay, const char *room, int players, int deadline,
                    const uint8_t *rom, size_t romSize, int *level, int *outPlayers, char *err, size_t errSize);
/* The same, one step at a time, for a window that keeps drawing while it waits: netplay_start, then netplay_poll
 * each frame until it returns 1 (the game starts: netplay_level and netplay_players say how) or -1 (netplay_error
 * says why). netplay_cancel stops at any point. Returns of netplay_start: 0, or -1 with a message in err. */
int netplay_start(const char *join, int hostPort, const char *relay, const char *room, int players, int deadline,
                  const uint8_t *rom, size_t romSize, char *err, size_t errSize);
int netplay_poll(void);
void netplay_cancel(void);
/* While connecting: a line for the player, the room's code once the relay has given one ("" before), and how many
 * players are in (on the host). */
const char *netplay_wait_status(void);
const char *netplay_room_code(void);
int netplay_joined(void);
int netplay_is_host(void);
const char *netplay_error(void);
int netplay_level(void);
int netplay_players(void);
/* 1 after a successful netplay_connect. */
int netplay_active(void);
/* The host's option: the game's normal ball controls (1) or the easy ones (0, the default). Call it before
 * netplay_start. Every machine plays with the host's choice: netplay_normal_ball gives it after the start. */
void netplay_set_normal_ball(int on);
int netplay_normal_ball(void);
/* The local player's input delay in frames (0 to 8; the default is 2): an input is for the frame that many frames
 * later and reaches the others sooner, so they predict less. Call it before netplay_begin. */
void netplay_set_input_delay(int frames);
int netplay_input_delay(void);
/* The host's own choice of ball controls before it hosts (netplay_set_normal_ball). */
int netplay_host_normal_ball(void);
/* After power-on (port_game_boot) with the engine attached: saves that state, from which every level is built, and
 * builds the first level. */
int netplay_begin(Engine *e, char *err, size_t errSize);
/* One game frame (20 a second in a level): sends the local controller, runs the host's instances and the local
 * game, and gives the display-list tasks of the local game's newest frame (weemitt's PortFrameTask), drawn from the
 * local player's view. Returns their count: 0 while the player moves to another level, -1 when the session failed.
 * The tasks point into the game's memory, and the game keeps that view's state until the next call, which puts the
 * game back first. The window can draw the tasks again until then. */
int netplay_frame(uint16_t buttons, int8_t stickX, int8_t stickY, void *tasks, int max);
/* A line for the window's title: the player number, the level, the frames ahead of the confirmed ones, desyncs. */
const char *netplay_status(void);
/* 1 when the session has failed; netplay_status says why. */
int netplay_failed(void);

#ifdef __cplusplus
}
#endif

#endif
