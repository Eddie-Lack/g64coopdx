/* The online mode of the window program (weemitt's studio build at 64 bits): the session, the instances, the
 * rollback core and the game, put together. weemitt/src/port/platform.c calls these functions.
 *
 * One player hosts; the others join by room code or by address. The game starts when everyone is in, or when the host
 * starts it with the players who are in (netplay_start_now, netplay_set_start_alone). Others then join the running
 * game while it has places. Player 0 is the host, and each joiner controls the glove the host gave it. Each level
 * that has players in it is its own game, an instance (instances.h): a machine runs the one its player is in, and the
 * host runs a copy of every one. */
#ifndef G64_NETPLAY_H
#define G64_NETPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "../net/udp.h"

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
/* 1 once the game has started: netplay_poll has returned 1, also inside netplay_connect. netplay_cancel sets it back
 * to 0. */
int netplay_active(void);
/* The host's option: the game's normal ball controls (1) or the easy ones (0, the default). Call it before
 * netplay_start. Every machine plays with the host's choice: netplay_normal_ball gives it after the start. */
void netplay_set_normal_ball(int on);
int netplay_normal_ball(void);
/* The host's option: a glove may take any free ball (1), or only its own (0, the default). A ball that is lost
 * kills its real owner either way. Call it before netplay_start; netplay_any_ball gives it after the start. */
void netplay_set_any_ball(int on);
/* The level pack for the session: a folder with a custom level that takes the place of one of the game's levels
 * (pack.h). Every player of a session must have the same pack. Call it
 * before netplay_start. An empty dir means no pack. Returns 0, or -1 with a message in err. */
int netplay_set_pack(const char *dir, char *err, size_t errSize);
const char *netplay_pack_name(void);
/* A joiner who lacks the host's pack can download it from the host (session.h). The host sends a pack only when its
 * level file passes the check (levcheck.h) and is not a level of the ROM, and its models.bin, if it has one, passes
 * the bank check and the rules that need the host's ROM (bankcheck.h). The joiner checks the same on its own ROM.
 * netplay_set_pack_cache names the folder that keeps downloaded packs (in DIR/packs). netplay_set_pack_policy says
 * what a joiner does with the host's offer: ask the player (ask 1), take it (yes 1), or end the session with a
 * message (both 0, the default). Call both before netplay_start.
 * While netplay_pack_asking is 1 during netplay_poll, the offer waits. These functions describe it: netplay_offer_name,
 * netplay_offer_size (bytes, all of the pack), netplay_offer_script (1 for a pack with a script) and
 * netplay_offer_models (the bytes of its models.bin, 0 for none). netplay_pack_kept is 1 when this machine has kept
 * that pack from an earlier download. netplay_pack_answer gives the player's answer. A downloaded pack is used only
 * when it reads as a pack, passes the check, and has the id the host named. */
void netplay_set_pack_cache(const char *dir);
void netplay_set_pack_policy(int ask, int yes);
int netplay_pack_asking(void);
int netplay_pack_kept(void);
const char *netplay_offer_name(void);
size_t netplay_offer_size(void);
int netplay_offer_script(void);
size_t netplay_offer_models(void);
void netplay_pack_answer(int yes);
int netplay_pack_level(void);
/* The host's choice that players may go to different worlds. Without it the session is in one world at a time: a
 * portal from the hub to another world is shut while players are in a world, and opens when everyone is in the
 * hub. Call it before netplay_start; netplay_any_world gives it after the start. */
void netplay_set_any_world(int on);
int netplay_any_world(void);
int netplay_host_any_world(void);
int netplay_any_ball(void);
int netplay_host_any_ball(void);
/* This player's glove and ball colors: G64_COLOR_SET (session.h) plus 0xRRGGBB, or 0 for the color of their player
 * number. Call it before netplay_start. After the start, netplay_apply_colors gives every player's colors to the game
 * (netplay_begin calls it after power-on). */
void netplay_set_colors(uint32_t glove, uint32_t ball);
void netplay_apply_colors(void);
/* The local player's input delay in frames (0 to 8; the default is 1): an input is for the frame that many frames
 * later and reaches the others sooner, so they predict less. Call it before netplay_begin. */
void netplay_set_input_delay(int frames);
/* The player's own sound volumes, in percent: the master volume, the songs and the effects (Settings). Call it each
 * window frame; only changes reach the sound player. */
void netplay_set_volumes(int master, int music, int effects);
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
/* Each player's own pause menu (it doesn't stop the game). Start opens it in netplay_frame; netplay_pause_toggle
 * opens or closes it. The window draws it from these. netplay_pause_pick does what item i does without the menu (the
 * window's Esc menu offers the same choices to the mouse). netplay_quit_requested is 1 after the player chose to
 * leave the online game. */
void netplay_pause_toggle(void);
void netplay_pause_pick(int item);
int netplay_pause_open(void);
/* 1 when this player's glove held the ball in the frame drawn last (the mouse's clicks, weemitt's mouselook.h). */
int netplay_holds_ball(void);
int netplay_pause_selection(void);
int netplay_pause_items(void);
const char *netplay_pause_item(int i);
int netplay_quit_requested(void);
/* The tip that the local player reads, as of the last netplay_frame: the number of lines on its page (0 when the
 * player reads none), each line, and 1 when another page follows. The game holds the reader and takes their A. */
int netplay_tip_lines(void);
const char *netplay_tip_line(int i);
int netplay_tip_more(void);
/* The host's choice, before netplay_start: put the room on the relay's list (1), with a title of up to 32
 * characters, for players of the same build to find; or keep it to the room code (0, the default). The room stays
 * on the list while it has a free place, also after the game starts. */
void netplay_set_public(int on, const char *title);
int netplay_public(void);
const char *netplay_public_title(void);
/* The relay's list of rooms, for the start screen. Call netplay_rooms_refresh every frame while the list shows: it
 * asks the relay at most once a second. netplay_rooms_count is -1 until the relay has answered. netplay_rooms_close
 * ends it. */
void netplay_rooms_refresh(const char *relay);
int netplay_rooms_count(void);
const net_room *netplay_room(int i);
void netplay_rooms_close(void);
/* This player's name, up to 16 printable characters. Call netplay_set_name before netplay_start. */
void netplay_set_name(const char *name);
const char *netplay_name(void);
/* 1 once netplay_set_name was called (the --name option, or the start screen). */
int netplay_name_chosen(void);
/* The players of the session (netplay_players gives how many): this machine's number, each one's name ("" when not known: before the start
 * only the host knows the names), and on the host 1 for a player who is gone. netplay_kick removes player p (the
 * host only). */
int netplay_me(void);
const char *netplay_player_name(int p);
/* 1 when player p is in the game now. Places of the game that nobody has taken, and players who left, are not. */
int netplay_player_present(int p);
/* The host starts the game now, with the players who are in; others join the running game while it has places.
 * netplay_set_start_alone(1) does it as soon as the host's room is ready. */
void netplay_start_now(void);
void netplay_set_start_alone(int on);
int netplay_player_gone(int p);
void netplay_kick(int p);
/* Chat. netplay_chat_send sends a line (up to 80 characters) to every other player. The latest lines, oldest first,
 * each with its sender's name, and the milliseconds since it came. Notices (a player left) are lines too. */
void netplay_chat_send(const char *text);
/* The line of text that a level pack's script shows to this player now, or an empty text (docs/scripts.md). */
const char *netplay_script_line(void);
int netplay_chat_count(void);
const char *netplay_chat_line(int i);
unsigned netplay_chat_age(int i);
/* This program's build id: the commit it was built from, or "dev". Only players with the same build can play
 * together. */
const char *netplay_build(void);
/* A line for the window's title: the player number, the level, the frames ahead of the confirmed ones, desyncs. */
const char *netplay_status(void);
/* 1 when the session has failed; netplay_status says why. */
int netplay_failed(void);

#ifdef __cplusplus
}
#endif

#endif
