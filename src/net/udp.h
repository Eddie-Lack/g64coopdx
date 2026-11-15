/* A UDP socket for the direct mode: one player hosts on a port, the others connect to the host's address
 * (docs/discovery/netcode.md). Plain sockets on Linux and Winsock on Windows; nothing blocks.
 *
 * Addresses are IPv4 or IPv6. Every datagram received is untrusted: the caller checks its contents. */
#ifndef G64_NET_UDP_H
#define G64_NET_UDP_H

#include <stddef.h>
#include <stdint.h>

/* The largest datagram the game sends or accepts. It stays under the usual path MTU. */
#define NET_MAX_DATAGRAM 1200

/* An address and port. */
typedef struct {
    uint8_t family; /* 4 or 6, NET_FAMILY_RELAY for a player through the relay (ip[0] is the player); 0 for none */
    uint8_t ip[16]; /* IPv4 in the first 4 bytes */
    uint16_t port;
} net_addr;

typedef struct net_socket net_socket;

/* Starts the socket library (Winsock). Call once before anything else. Returns 0, or -1. */
int net_init(void);
void net_shutdown(void);

/* A socket bound to a port on every local address (0 picks a free port), for IPv6 and IPv4 where the system allows
 * both on one socket. NULL on failure, with a message in err. */
net_socket *net_open(uint16_t port, char *err, size_t errSize);
void net_close(net_socket *s);
/* The port the socket is bound to. */
uint16_t net_port(const net_socket *s);

/* Resolves "host", "host:port", "[v6]:port" or "v6" to an address; defaultPort is used when none is given. Returns 0,
 * or -1 with a message in err. It may block while it asks DNS. */
int net_resolve(const char *text, uint16_t defaultPort, net_addr *out, char *err, size_t errSize);
/* The same, with up to max of the name's addresses: IPv4 ones first, each once. Returns their count (1 or more), or -1
 * with a message in err. */
int net_resolve_all(const char *text, uint16_t defaultPort, net_addr *out, int max, char *err, size_t errSize);
/* Writes an address as text ("1.2.3.4:5000" or "[::1]:5000"). */
void net_addr_text(const net_addr *a, char *buf, size_t size);
/* 1 when two addresses are the same. */
int net_addr_equal(const net_addr *a, const net_addr *b);

/* Sends one datagram. Returns 0, or -1 (dropped). */
int net_send(net_socket *s, const net_addr *to, const void *data, size_t len);
/* Receives one datagram if one is waiting: returns its length (0 to NET_MAX_DATAGRAM) and its sender, or -1 when none
 * is waiting. A datagram longer than NET_MAX_DATAGRAM is dropped. */
int net_recv(net_socket *s, net_addr *from, void *buf, size_t size);

/* ---- The relay (server/relay.go). A socket in relay mode sends everything through the lobby's relay, and nobody
 * forwards a port. The host opens a room and gets a code; the others join with the code. net_send and net_recv then
 * work as before, with virtual addresses: net_relay_player(0) is the host, net_relay_player(k) joiner k. */

#define NET_FAMILY_RELAY 9
#define NET_ROOM_CODE 6
/* The most addresses of the relay that a socket tries. */
#define NET_RELAY_ADDRS 4
enum { NET_RELAY_HOST = 1, NET_RELAY_JOIN = 2 };

/* Puts a socket in relay mode as a host (opens a room) or a joiner (joins the room code). relays holds count of the
 * relay's addresses (1 to NET_RELAY_ADDRS). A name can have addresses that do not carry UDP: the socket sends to every
 * one until the relay answers, then only to the address that answered. Returns 0, or -1. */
int net_relay_host(net_socket *s, const net_addr *relays, int count);
int net_relay_join(net_socket *s, const net_addr *relays, int count, const char *code);
/* Sends what keeps the room or the place in it; call it often (it sends every 250 ms until ready, then every 2 s). */
void net_relay_tick(net_socket *s, uint32_t now);
/* 0 while waiting, 1 when the room is open or joined, -1 when the relay refused the join. */
int net_relay_state(const net_socket *s);
/* The room's code (NET_ROOM_CODE letters and digits). */
const char *net_relay_code(const net_socket *s);
/* Why the relay refused: 1 no such room, 2 the room is full. */
int net_relay_refusal(const net_socket *s);
/* Handles one datagram src sent to a socket in relay mode (net_recv calls it; the fuzz target calls it directly).
 * Returns the game payload's length, with its sender's virtual address in from, or -1 when it held none. */
int net_relay_parse(net_socket *s, const net_addr *src, const uint8_t *d, int n, net_addr *from, void *buf,
                    size_t size);
/* The virtual address of player p through the relay (0 is the host). */
net_addr net_relay_player(int p);

#endif
