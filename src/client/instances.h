/* Instances: one game per level that has players in it (docs/discovery/netcode.md, "Instances").
 *
 * The host runs a reference copy of every instance on confirmed inputs, keeps each instance's log of confirmed
 * inputs, and moves players between instances. A player who takes an exit leaves their instance (the game records
 * where the exit leads, g64players.c). The host then puts them in the instance of that level, or starts a new one, and
 * sends them a move. Every machine builds every instance the same way: from the state saved right after power-on, the
 * start record (the level, its world, the members at the start and the session's progress), and the instance's log
 * up to the frame the player joins at. Only inputs and these records cross the network; no game data does.
 *
 * The host part (g64_ihost) and the player part (g64_iclient) talk only through send functions, so a test can run a
 * host and several players in one process. The host's own player is a g64_iclient too.
 *
 * Messages (the payload of a session game message), all numbers little-endian:
 *   rb      0x01, instance (u8), one rollback message (src/rollback/rollback.h)       both ways
 *   move    0x02, instance (u8), level (u8), world (u8), players (u8), start members (u16), join frame (u32),
 *           progress: levels (u32), all garibs (u32), hub target (u8), hub level (u8), worlds done (u8), the
 *           world the session is in at the instance's start (u8); then every player's colors at the instance's
 *           start: 16 times glove (u32), ball (u32)
 *                                                                                      host to player
 *   log     0x03, instance (u8), players (u8), first frame (u32), count (u8), unused (u8), count frames: present
 *           (u16, a bit per player), players inputs (buttons u16, x s8, y s8); then the frames' session events
 *           (rb_events_write)                                                          host to player
 *   logack  0x04, instance (u8), join frame (u32), frames received (u32)              player to host
 *   ready   0x05, unused (u8): a player who is in the session and in no instance asks for one. The host puts
 *           them in the instance of the session's first level and answers with a move.   player to host
 * Every message is checked before use: lengths, counts, players, instance numbers, frames.
 *
 * Progress made in one instance reaches the others as a session event (imsg.h, G64_EVENT_PROGRESS) on a confirmed
 * frame: the host sends the session's whole progress to every instance whose progress is older, after the frame's
 * exits. The instance's log keeps each frame's event, so a player who joins later runs it at the same frame. */
#ifndef G64_INSTANCES_H
#define G64_INSTANCES_H

#include <stddef.h>
#include <stdint.h>

#include "../rollback/engine_game.h"
#include "../rollback/rollback.h"

#include "imsg.h"

/* Sends one message: the host to player to; a player to the host (to is 0). */
typedef void (*g64_isend_fn)(void *user, int to, const uint8_t *data, size_t len);

typedef struct g64_ihost g64_ihost;
typedef struct g64_iclient g64_iclient;

/* The host for players players. boot is the state right after power-on (engine_save); it must stay valid. Every
 * player starts in one instance of level, in world 0, with a new save. */
g64_ihost *g64_ihost_new(Engine *e, const void *boot, int players, int level, uint32_t deadline, g64_isend_fn send,
                         void *user);
/* The host's "any ball" rule for every game that is built from now on: a glove may take any free ball (1), or only
 * its own (0, the default). Every machine of a session must set the same before its first game is built. */
void g64_instances_any_ball(int on);
/* The host's "one world at a time" rule for every game that is built from now on: a portal from the hub to another
 * world is shut while players are in a world (1), or players may go to different worlds (0, the default here).
 * Every machine of a session must set the same before its first game is built. The host tells every instance which
 * world the session is in, as an event on a confirmed frame. */
void g64_instances_one_world(int on);
/* The session's level pack for every game that is built from now on: its level file takes the place of level's,
 * and is size bytes at offset rom of the ROM buffer, after the ROM's own bytes (engine_set_rom). A size of 0 means
 * no pack. Every machine of a session must set the same before its first game is built. */
void g64_instances_pack(int level, unsigned rom, unsigned size);
/* The pack's script for every game that is built from now on: size bytes of Lua source at offset rom of the ROM
 * buffer. A size of 0 means no script. Each game runs it at its start, and its on_frame function once a frame
 * (weemitt/src/port/g64lua.c). */
void g64_instances_pack_script(unsigned rom, unsigned size);
/* The pack's own models for every game that is built from now on: its texture bank and its object bank, at offsets
 * texRom and objRom of the ROM buffer. Both sizes 0 means none. The banks must have passed the bank check
 * (bankcheck.h). The game loads them in the pack's level (weemitt's g64packbanks.c). */
void g64_instances_pack_models(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
/* The retail banks the pack's models.bin names by number, after g64_instances_pack_models, which clears them: numTex
 * texture banks and numObj object banks, at most 4 of each. first is the header's flag: the retail texture banks are
 * searched before the level's own. Each machine loads them from its own ROM in the pack's level (weemitt's
 * g64packbanks.c). */
void g64_instances_pack_retail(int first, const unsigned char *tex, int numTex, const unsigned char *obj, int numObj);
/* The same for a session that players may join later: members has a bit for each player who is there at the start
 * (at least one), and colors has every player's colors (NULL: the game's own now). */
g64_ihost *g64_ihost_new_with(Engine *e, const void *boot, int players, unsigned members,
                              const uint32_t colors[16][2], int level, uint32_t deadline, g64_isend_fn send,
                              void *user);
/* Player p's colors from now on. Every running instance gets them as an event on a confirmed frame, and every new
 * instance starts with them. */
void g64_ihost_set_colors(g64_ihost *h, int p, uint32_t glove, uint32_t ball);
/* Puts player p, who is in no instance, into the instance of the session's first level. A player's ready message
 * does the same. Returns 0, or -1 when there is no room. */
int g64_ihost_add(g64_ihost *h, int p);
void g64_ihost_free(g64_ihost *h);
/* One host frame: ticks every instance, moves the players who took an exit, and sends what is due. */
void g64_ihost_tick(g64_ihost *h);
/* A message from player from. Returns 0, or -1 when it is malformed (it is then ignored). */
int g64_ihost_receive(g64_ihost *h, int from, const uint8_t *data, size_t len);
/* Player p left the session: they leave their instance. */
void g64_ihost_drop(g64_ihost *h, int p);
/* For checks: fn is called with each confirmed frame's final state of the reference copy of every instance, with
 * the instance's number. The state is valid only during the call. */
typedef void (*g64_final_fn)(void *user, int instance, uint32_t frame, const void *state);
void g64_ihost_on_final(g64_ihost *h, g64_final_fn fn, void *user);
/* For logs: fn is called for each move, with the player, the exit type (weemitt's enum ExitType), the level they
 * left, the level and world they go to, the frame they join at, and the session's progress after the exit. */
typedef void (*g64_move_fn)(void *user, int player, int exit_type, int from_level, int level, int world,
                            uint32_t join, const g64_progress *progress);
void g64_ihost_on_move(g64_ihost *h, g64_move_fn fn, void *user);
/* For status lines and tests. */
int g64_ihost_instances(const g64_ihost *h);
int g64_ihost_level_of(const g64_ihost *h, int p);
/* The world the session is in (weemitt's enum World), or 0 while every player is in the hub. */
int g64_ihost_world(const g64_ihost *h);
uint32_t g64_ihost_hash_matches(const g64_ihost *h);
uint32_t g64_ihost_hash_mismatches(const g64_ihost *h);

/* Player me of players. Builds its first game from the same start as the host's first instance. window is the
 * client's savestate window; deadline is the host's (a player who has fallen behind the host's clock catches up). */
g64_iclient *g64_iclient_new(Engine *e, const void *boot, int players, int me, int level, int window,
                             uint32_t deadline, g64_isend_fn send, void *user);
/* The same with the start's members and colors (g64_ihost_new_with). A player who is not a member has no game at
 * first: g64_iclient_step asks the host for one, and the host's move brings it. */
g64_iclient *g64_iclient_new_with(Engine *e, const void *boot, int players, unsigned members,
                                  const uint32_t colors[16][2], int me, int level, int window, uint32_t deadline,
                                  g64_isend_fn send, void *user);
void g64_iclient_free(g64_iclient *c);
/* A message from the host. Returns 0, or -1 when it is malformed (it is then ignored). */
int g64_iclient_receive(g64_iclient *c, const uint8_t *data, size_t len);
/* One frame with the local controller. Returns 1 when the local game ran a frame (its display lists are the
 * engine's latest), 0 when it waits (for the host, or while it moves to another instance). */
int g64_iclient_step(g64_iclient *c, rb_input local);
/* Input delay for this player's games, now and after every move (rb_client_set_input_delay). Returns 0, or -1. */
int g64_iclient_set_input_delay(g64_iclient *c, int frames);
/* For checks: fn is called with each final frame of the local game (confirmed and run on confirmed inputs). */
void g64_iclient_on_final(g64_iclient *c, g64_final_fn fn, void *user);
/* The local player's instance number. */
int g64_iclient_instance(const g64_iclient *c);
/* 1 while the player moves to another instance (the game is being built). */
int g64_iclient_moving(const g64_iclient *c);
/* The local game (for drawing the local player's view), or NULL while moving. */
rb_engine_game *g64_iclient_game(g64_iclient *c);
int g64_iclient_level(const g64_iclient *c);
uint32_t g64_iclient_frame(const g64_iclient *c);
uint32_t g64_iclient_confirmed(const g64_iclient *c);
uint32_t g64_iclient_desyncs(const g64_iclient *c);
uint64_t g64_iclient_rollback_frames(const g64_iclient *c);

#endif
