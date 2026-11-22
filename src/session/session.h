/* The session in the direct mode: a host waits on a UDP port, players join by its address, and the host starts the
 * game when everyone is in (docs/discovery/netcode.md, "The handshake"). After the start the session carries the
 * rollback core's messages between the host and each client.
 *
 * Messages, all numbers little-endian. The first byte is the type; the rollback core's own types are 1 to 5.
 *   hello    0x40, "G64", protocol (u8), rom (u32: a hash of the player's ROM), build (u32: a hash of the program's
 *            build id), glove color (u32), ball color (u32), name (up to 16 bytes)
 *   welcome  0x41, player (u8), players (u8)
 *   refuse   0x42, reason (u8)
 *   start    0x43, players (u8), level (u8), deadline (u8), flags (u8: the host's options, G64_FLAG_), and for each
 *            player: glove color (u32), ball color (u32), name (16 bytes, padded with zeros)
 * A color is G64_COLOR_SET plus 0xRRGGBB when the player chose it, or 0 for the color of their player number.
 *   ping     0x44 (either way; keeps the connection and a router's mapping open)
 *   game     0x45, then one rollback message
 *   chat     0x46, from (u8: the sender's player number; the host fills it in from the address), line number (u8),
 *            text (up to G64_CHAT_MAX bytes). A joiner sends its lines to the host, which passes them on. A sender
 *            repeats a line a few times; a receiver drops a line whose number is that player's last one.
 *   kick     0x47 (host to a player it removed)
 * Names and chat lines are text from the network: every byte outside printable ASCII becomes '?'. They never enter
 * the game's state.
 * Every message from the network is untrusted: lengths, counts and player numbers are checked before use. */
#ifndef G64_SESSION_H
#define G64_SESSION_H

#include <stdint.h>

#include "../net/udp.h"

#define G64_PROTOCOL 7
#define G64_DEFAULT_PORT 27064
#define G64_SESSION_MAX_PLAYERS 16
/* The host's options, which every machine must share (they change the game's logic). */
#define G64_FLAG_NORMAL_BALL 0x01 /* the game's normal ball controls; without it, the easy ball controls */
#define G64_FLAG_ANY_BALL 0x02    /* a glove may take any free ball; without it, only its own */
#define G64_FLAGS_KNOWN 0x03
#define G64_COLOR_SET 0x1000000u
#define G64_NAME_MAX 16
#define G64_CHAT_MAX 80

enum {
    G64_REFUSE_FULL = 1,     /* every player slot is taken */
    G64_REFUSE_ROM = 2,      /* another ROM */
    G64_REFUSE_PROTOCOL = 3, /* another version of g64coopdx: another protocol or another build */
    G64_REFUSE_STARTED = 4,  /* the game has started */
};

typedef enum {
    G64_SESSION_WAITING,  /* the host waits for players; a joiner waits for the welcome or the start */
    G64_SESSION_STARTED,  /* the game runs */
    G64_SESSION_FAILED,   /* see g64_session_error */
} g64_session_state;

typedef struct g64_session g64_session;

/* Called for each rollback message that arrives after the start. from is the sender's player number on the host,
 * and 0 on a client. */
typedef void (*g64_game_fn)(void *user, int from, const uint8_t *data, size_t len);

/* A host for players players (2 to G64_SESSION_MAX_PLAYERS) on socket s. The host is player 0. */
g64_session *g64_session_host(net_socket *s, int players, uint32_t rom, int level, int deadline);
/* A player who joins the host at address to. */
g64_session *g64_session_join(net_socket *s, const net_addr *to, uint32_t rom, const char *name);
void g64_session_free(g64_session *g);

/* Reads every waiting datagram and sends what is due: hellos, welcomes, starts, pings. now is a time in milliseconds
 * from any start. Returns the state. */
g64_session_state g64_session_poll(g64_session *g, uint32_t now, g64_game_fn on_game, void *user);
/* Handles one datagram (g64_session_poll calls it for each one it reads; the fuzz target calls it directly). */
void g64_session_receive(g64_session *g, const net_addr *from, const uint8_t *data, size_t len, uint32_t now,
                         g64_game_fn on_game, void *user);
/* After the start: this machine's player number, the number of players, the level and the host's deadline. */
int g64_session_me(const g64_session *g);
int g64_session_players(const g64_session *g);
int g64_session_level(const g64_session *g);
int g64_session_deadline(const g64_session *g);
/* This program's build: a hash of its build id. Every build of the game's code plays another game, so a host
 * refuses a joiner whose build differs (G64_REFUSE_PROTOCOL). Set it on both sides before the first poll. It is 0
 * until set. */
void g64_session_set_build(g64_session *g, uint32_t build);
/* The host's options (G64_FLAG_): the host sets them before the start; a joiner gets them with the start. */
int g64_session_flags(const g64_session *g);
void g64_session_set_flags(g64_session *g, int flags);
/* This machine's player's glove and ball colors, before the start (0, or G64_COLOR_SET plus 0xRRGGBB). After the
 * start, every player's, by player number; the host has them as the joiners sent them. */
void g64_session_set_colors(g64_session *g, uint32_t glove, uint32_t ball);
void g64_session_colors(const g64_session *g, int p, uint32_t *glove, uint32_t *ball);
/* This machine's player's name (up to G64_NAME_MAX characters), before the first poll. A joiner's name given to
 * g64_session_join stays unless this is called. After the start g64_session_name gives every player's name, by
 * player number; the host has the names as the joiners sent them, also before the start. */
void g64_session_set_name(g64_session *g, const char *name);
const char *g64_session_name(const g64_session *g, int p);
/* Chat. g64_session_chat sends one line (up to G64_CHAT_MAX characters) to every other player. fn is called once for
 * each line that arrives, with its sender's player number; the text lives only for the call. */
typedef void (*g64_chat_fn)(void *user, int from, const char *text);
void g64_session_on_chat(g64_session *g, g64_chat_fn fn, void *user);
void g64_session_chat(g64_session *g, const char *text);
/* On the host: the address player p sends from (through the relay: their place in the room), or NULL. */
const net_addr *g64_session_peer_addr(const g64_session *g, int p);
/* On the host: removes player p. Before the start their place is free again. After it they count as gone
 * (g64_session_gone). Their program is told, and told again if it goes on sending. */
/* It gives 1 when it removed the player, 0 when there was no one to remove (an empty place, or one already removed). */
int g64_session_kick(g64_session *g, int p);
/* Players who have joined so far, the host included (the host's count; a joiner knows only itself). */
int g64_session_joined(const g64_session *g);
/* On the host after the start: 1 when player p stopped answering. The game goes on without them. */
int g64_session_gone(const g64_session *g, int p);
/* Why the session failed, in words. */
const char *g64_session_error(const g64_session *g);

/* Sends one rollback message: a client sends to the host (to is ignored), the host to player to. */
void g64_session_send_game(g64_session *g, int to, const uint8_t *data, size_t len);

/* A hash of a ROM's bytes (FNV-1a), for the hello. */
uint32_t g64_rom_hash(const uint8_t *rom, size_t size);

#endif
