/* The session in the direct mode: a host waits on a UDP port, players join by its address, and the host starts the
 * game when everyone is in (docs/discovery/netcode.md, "The handshake"). After the start the session carries the
 * rollback core's messages between the host and each client.
 *
 * Messages, all numbers little-endian. The first byte is the type; the rollback core's own types are 1 to 4.
 *   hello    0x40, "G64", protocol (u8), rom (u32: a hash of the player's ROM), name (up to 16 bytes)
 *   welcome  0x41, player (u8), players (u8)
 *   refuse   0x42, reason (u8)
 *   start    0x43, players (u8), level (u8), deadline (u8)
 *   ping     0x44 (either way; keeps the connection and a router's mapping open)
 *   game     0x45, then one rollback message
 * Every message from the network is untrusted: lengths, counts and player numbers are checked before use. */
#ifndef G64_SESSION_H
#define G64_SESSION_H

#include <stdint.h>

#include "../net/udp.h"

#define G64_PROTOCOL 1
#define G64_DEFAULT_PORT 27064
#define G64_SESSION_MAX_PLAYERS 16

enum {
    G64_REFUSE_FULL = 1,     /* every player slot is taken */
    G64_REFUSE_ROM = 2,      /* another ROM */
    G64_REFUSE_PROTOCOL = 3, /* another version of g64coopdx */
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
/* Players who have joined so far, the host included (the host's count; a joiner knows only itself). */
int g64_session_joined(const g64_session *g);
/* Why the session failed, in words. */
const char *g64_session_error(const g64_session *g);

/* Sends one rollback message: a client sends to the host (to is ignored), the host to player to. */
void g64_session_send_game(g64_session *g, int to, const uint8_t *data, size_t len);

/* A hash of a ROM's bytes (FNV-1a), for the hello. */
uint32_t g64_rom_hash(const uint8_t *rom, size_t size);

#endif
