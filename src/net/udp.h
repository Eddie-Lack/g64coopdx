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

/* An address and port, in a form that compares with memcmp after net_addr_normalize. */
typedef struct {
    uint8_t family; /* 4 or 6; 0 for none */
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
/* Writes an address as text ("1.2.3.4:5000" or "[::1]:5000"). */
void net_addr_text(const net_addr *a, char *buf, size_t size);
/* 1 when two addresses are the same. */
int net_addr_equal(const net_addr *a, const net_addr *b);

/* Sends one datagram. Returns 0, or -1 (dropped). */
int net_send(net_socket *s, const net_addr *to, const void *data, size_t len);
/* Receives one datagram if one is waiting: returns its length (0 to NET_MAX_DATAGRAM) and its sender, or -1 when none
 * is waiting. A datagram longer than NET_MAX_DATAGRAM is dropped. */
int net_recv(net_socket *s, net_addr *from, void *buf, size_t size);

#endif
