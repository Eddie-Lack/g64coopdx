/* The UDP socket (udp.h). */
#include "udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

/* The relay's datagrams (server/relay.go): "G64R", a type, then the body. */
#define RELAY_HEAD 5
enum { R_OPEN = 0x01, R_JOIN = 0x02, R_TO_HOST = 0x03, R_TO_PEER = 0x04, R_ANNOUNCE = 0x05, R_LIST = 0x06,
       R_BAN = 0x07, R_ROOM = 0x81, R_JOINED = 0x82, R_REFUSED = 0x83, R_FROM_PEER = 0x84, R_FROM_HOST = 0x85,
       R_ROOMS = 0x86 };
/* The length of a list request: the relay's longest answer (relay.go, relayListMin). */
#define RELAY_LIST_ROOMS 12
#define RELAY_LIST_MIN (RELAY_HEAD + 1 + RELAY_LIST_ROOMS * (NET_ROOM_CODE + 4 + NET_ROOM_TITLE))
/* How often a host asks the relay to keep its room, and a joiner to keep its place, in milliseconds. */
#define RELAY_KEEP 2000

struct net_socket {
    sock_t fd;
    int v6; /* the socket is IPv6 (it may take IPv4 too, as mapped addresses) */
    uint16_t port;
    int relay;       /* 0: direct; NET_RELAY_HOST or NET_RELAY_JOIN */
    net_addr relayAddr[NET_RELAY_ADDRS]; /* the relay's addresses; only the first once the relay has answered */
    int relayAddrs;
    char code[NET_ROOM_CODE + 1]; /* the room: given by the relay to a host, by the player to a joiner */
    int state;       /* 0 waiting, 1 ready, -1 refused */
    int refusal;
    int peer;        /* a joiner's number in the room */
    uint32_t lastKeep;
    int kept;
};

int net_init(void) {
#ifdef _WIN32
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void net_shutdown(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static int set_nonblocking(sock_t fd) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static void to_net_addr(const struct sockaddr_storage *ss, net_addr *a) {
    memset(a, 0, sizeof *a);
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)ss;
        a->family = 4;
        memcpy(a->ip, &in->sin_addr, 4);
        a->port = ntohs(in->sin_port);
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)ss;
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        /* An IPv4 peer on a dual-stack socket arrives as ::ffff:a.b.c.d; keep it as IPv4. */
        if (memcmp(&in6->sin6_addr, mapped, 12) == 0) {
            a->family = 4;
            memcpy(a->ip, (const uint8_t *)&in6->sin6_addr + 12, 4);
        } else {
            a->family = 6;
            memcpy(a->ip, &in6->sin6_addr, 16);
        }
        a->port = ntohs(in6->sin6_port);
    }
}

/* The sockaddr for sending to address a from socket s. Returns its length, or 0 when s cannot reach that family. */
static int to_sockaddr(const net_socket *s, const net_addr *a, struct sockaddr_storage *ss) {
    memset(ss, 0, sizeof *ss);
    if (s->v6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(a->port);
        if (a->family == 4) {
            uint8_t *b = (uint8_t *)&in6->sin6_addr;
            b[10] = b[11] = 0xff;
            memcpy(b + 12, a->ip, 4);
        } else {
            memcpy(&in6->sin6_addr, a->ip, 16);
        }
        return (int)sizeof *in6;
    }
    if (a->family != 4)
        return 0;
    {
        struct sockaddr_in *in = (struct sockaddr_in *)ss;
        in->sin_family = AF_INET;
        in->sin_port = htons(a->port);
        memcpy(&in->sin_addr, a->ip, 4);
        return (int)sizeof *in;
    }
}

net_socket *net_open(uint16_t port, char *err, size_t errSize) {
    net_socket *s = calloc(1, sizeof *s);
    struct sockaddr_storage ss;
    socklen_t len;
    if (!s) {
        snprintf(err, errSize, "out of memory");
        return NULL;
    }
    /* A dual-stack IPv6 socket first; plain IPv4 where the system has no IPv6. */
    s->fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s->fd != BAD_SOCKET) {
        struct sockaddr_in6 in6;
        int off = 0;
        setsockopt(s->fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof off);
        memset(&in6, 0, sizeof in6);
        in6.sin6_family = AF_INET6;
        in6.sin6_addr = in6addr_any;
        in6.sin6_port = htons(port);
        if (bind(s->fd, (struct sockaddr *)&in6, sizeof in6) == 0) {
            s->v6 = 1;
        } else {
            close_socket(s->fd);
            s->fd = BAD_SOCKET;
        }
    }
    if (s->fd == BAD_SOCKET) {
        struct sockaddr_in in;
        s->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&in, 0, sizeof in);
        in.sin_family = AF_INET;
        in.sin_addr.s_addr = htonl(INADDR_ANY);
        in.sin_port = htons(port);
        if (s->fd == BAD_SOCKET || bind(s->fd, (struct sockaddr *)&in, sizeof in) != 0) {
            snprintf(err, errSize, "cannot bind UDP port %u (is it in use?)", (unsigned)port);
            if (s->fd != BAD_SOCKET)
                close_socket(s->fd);
            free(s);
            return NULL;
        }
    }
    if (set_nonblocking(s->fd) != 0) {
        snprintf(err, errSize, "cannot make the socket non-blocking");
        close_socket(s->fd);
        free(s);
        return NULL;
    }
    len = sizeof ss;
    if (getsockname(s->fd, (struct sockaddr *)&ss, &len) == 0) {
        net_addr a;
        to_net_addr(&ss, &a);
        s->port = a.port;
    }
    return s;
}

void net_close(net_socket *s) {
    if (s) {
        close_socket(s->fd);
        free(s);
    }
}

uint16_t net_port(const net_socket *s) { return s->port; }

int net_resolve(const char *text, uint16_t defaultPort, net_addr *out, char *err, size_t errSize) {
    return net_resolve_all(text, defaultPort, out, 1, err, errSize) > 0 ? 0 : -1;
}

int net_resolve_all(const char *text, uint16_t defaultPort, net_addr *out, int max, char *err, size_t errSize) {
    char host[256], portText[8];
    const char *colon;
    struct addrinfo hints, *res = NULL, *r;
    size_t n;
    unsigned long port = defaultPort;
    if (!text || !*text || strlen(text) >= sizeof host) {
        snprintf(err, errSize, "no address");
        return -1;
    }
    if (text[0] == '[') {
        /* [v6]:port */
        const char *close = strchr(text, ']');
        if (!close) {
            snprintf(err, errSize, "a [ without a ]");
            return -1;
        }
        n = (size_t)(close - text - 1);
        memcpy(host, text + 1, n);
        host[n] = 0;
        colon = close[1] == ':' ? close + 1 : NULL;
    } else {
        colon = strrchr(text, ':');
        /* More than one colon without brackets: a bare IPv6 address. */
        if (colon && strchr(text, ':') != colon)
            colon = NULL;
        n = colon ? (size_t)(colon - text) : strlen(text);
        memcpy(host, text, n);
        host[n] = 0;
    }
    if (colon) {
        char *end;
        port = strtoul(colon + 1, &end, 10);
        if (*end || port == 0 || port > 65535) {
            snprintf(err, errSize, "a bad port: %s", colon + 1);
            return -1;
        }
    }
    snprintf(portText, sizeof portText, "%lu", port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, portText, &hints, &res) != 0 || !res) {
        snprintf(err, errSize, "cannot find the address %s", host);
        return -1;
    }
    /* IPv4 first: it reaches most home routers' forwarded ports. */
    {
        int count = 0, pass;
        for (pass = 0; pass < 2; pass++) {
            for (r = res; r && count < max; r = r->ai_next) {
                struct sockaddr_storage ss;
                net_addr a;
                int i, seen = 0;
                if ((r->ai_family == AF_INET) != (pass == 0))
                    continue;
                memset(&ss, 0, sizeof ss);
                memcpy(&ss, r->ai_addr, r->ai_addrlen < sizeof ss ? r->ai_addrlen : sizeof ss);
                memset(&a, 0, sizeof a);
                to_net_addr(&ss, &a);
                if (!a.family)
                    continue;
                for (i = 0; i < count; i++)
                    seen |= net_addr_equal(&out[i], &a);
                if (!seen)
                    out[count++] = a;
            }
        }
        freeaddrinfo(res);
        if (count == 0) {
            snprintf(err, errSize, "cannot find the address %s", host);
            return -1;
        }
        return count;
    }
}

void net_addr_text(const net_addr *a, char *buf, size_t size) {
    char ip[64] = "?";
    if (a->family == NET_FAMILY_RELAY) {
        snprintf(buf, size, "player %u through the relay", (unsigned)a->ip[0] + 1);
        return;
    }
    if (a->family == 4)
        inet_ntop(AF_INET, a->ip, ip, sizeof ip);
    else if (a->family == 6)
        inet_ntop(AF_INET6, a->ip, ip, sizeof ip);
    if (a->family == 6)
        snprintf(buf, size, "[%s]:%u", ip, (unsigned)a->port);
    else
        snprintf(buf, size, "%s:%u", ip, (unsigned)a->port);
}

int net_addr_equal(const net_addr *a, const net_addr *b) {
    size_t n = a->family == 4 ? 4 : 16;
    return a->family == b->family && a->port == b->port && memcmp(a->ip, b->ip, n) == 0;
}

static int raw_send(net_socket *s, const net_addr *to, const void *data, size_t len) {
    struct sockaddr_storage ss;
    int sl;
    if (len > NET_MAX_DATAGRAM + RELAY_HEAD + 1)
        return -1;
    sl = to_sockaddr(s, to, &ss);
    if (!sl)
        return -1;
    return sendto(s->fd, (const char *)data, (int)len, 0, (struct sockaddr *)&ss, (socklen_t)sl) == (int)len ? 0
                                                                                                              : -1;
}

/* Reads one datagram of up to max bytes into buf (size bytes); -1 when none is waiting. */
static int raw_recv(net_socket *s, net_addr *from, void *buf, size_t size, size_t max) {
    for (;;) {
        char tmp[NET_MAX_DATAGRAM + RELAY_HEAD + 2];
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        int n = (int)recvfrom(s->fd, tmp, sizeof tmp, 0, (struct sockaddr *)&ss, &sl);
        if (n < 0) {
#ifdef _WIN32
            /* A reply that a closed port refused (ICMP) shows up as a reset on Windows: skip it. */
            if (WSAGetLastError() == WSAECONNRESET)
                continue;
#endif
            return -1;
        }
        /* Too long for the game, or for the caller's buffer: dropped. */
        if ((size_t)n > max || (size_t)n > size)
            continue;
        memcpy(buf, tmp, (size_t)n);
        to_net_addr(&ss, from);
        return n;
    }
}

/* ---- The relay (server/relay.go) */

static void relay_send(net_socket *s, int type, const void *body, size_t len) {
    uint8_t m[NET_MAX_DATAGRAM + RELAY_HEAD + 1];
    int i;
    if (len > sizeof m - RELAY_HEAD)
        return;
    memcpy(m, "G64R", 4);
    m[4] = (uint8_t)type;
    if (len)
        memcpy(m + RELAY_HEAD, body, len);
    for (i = 0; i < s->relayAddrs; i++)
        raw_send(s, &s->relayAddr[i], m, RELAY_HEAD + len);
}

static int set_relays(net_socket *s, const net_addr *relays, int count) {
    int i;
    if (!relays || count < 1 || count > NET_RELAY_ADDRS)
        return -1;
    for (i = 0; i < count; i++)
        s->relayAddr[i] = relays[i];
    s->relayAddrs = count;
    return 0;
}

/* The relay answered from address i: from now on, only that address is used. */
static void settle(net_socket *s, int i) {
    s->relayAddr[0] = s->relayAddr[i];
    s->relayAddrs = 1;
}

static void keep(net_socket *s) {
    if (s->relay == NET_RELAY_HOST)
        relay_send(s, R_OPEN, NULL, 0);
    else if (s->relay == NET_RELAY_JOIN)
        relay_send(s, R_JOIN, s->code, NET_ROOM_CODE);
}

int net_relay_host(net_socket *s, const net_addr *relays, int count) {
    if (!s || set_relays(s, relays, count) != 0)
        return -1;
    s->relay = NET_RELAY_HOST;
    s->state = 0;
    s->code[0] = 0;
    s->kept = 0;
    keep(s);
    return 0;
}

int net_relay_join(net_socket *s, const net_addr *relays, int count, const char *code) {
    size_t i;
    if (!s || !code || strlen(code) != NET_ROOM_CODE || set_relays(s, relays, count) != 0)
        return -1;
    s->relay = NET_RELAY_JOIN;
    s->state = 0;
    for (i = 0; i < NET_ROOM_CODE; i++) {
        char c = code[i];
        s->code[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    s->code[NET_ROOM_CODE] = 0;
    s->kept = 0;
    keep(s);
    return 0;
}

int net_relay_state(const net_socket *s) { return s->state; }
const char *net_relay_code(const net_socket *s) { return s->code; }
int net_relay_refusal(const net_socket *s) { return s->refusal; }

void net_relay_tick(net_socket *s, uint32_t now) {
    if (!s->relay)
        return;
    if (!s->kept || now - s->lastKeep >= (uint32_t)(s->state == 1 ? RELAY_KEEP : 250)) {
        s->kept = 1;
        s->lastKeep = now;
        keep(s);
    }
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

void net_relay_announce(net_socket *s, uint32_t build, int wanted, int joined, int flags, int open,
                        const char *title) {
    uint8_t m[8 + NET_ROOM_TITLE];
    size_t len = title ? strlen(title) : 0;
    if (!s || s->relay != NET_RELAY_HOST || s->state != 1)
        return;
    if (len > NET_ROOM_TITLE)
        len = NET_ROOM_TITLE;
    put_u32(m, build);
    m[4] = (uint8_t)wanted;
    m[5] = (uint8_t)joined;
    m[6] = (uint8_t)flags;
    m[7] = open ? 1 : 0;
    if (len)
        memcpy(m + 8, title, len);
    relay_send(s, R_ANNOUNCE, m, 8 + len);
}

void net_relay_ban(net_socket *s, int peer) {
    uint8_t m = (uint8_t)peer;
    if (!s || s->relay != NET_RELAY_HOST || s->state != 1 || peer < 1 || peer > 255)
        return;
    relay_send(s, R_BAN, &m, 1);
}

void net_rooms_ask(net_socket *s, const net_addr *relays, int count, uint32_t build) {
    uint8_t m[RELAY_LIST_MIN];
    int i;
    if (!s || !relays || count < 1)
        return;
    /* The request is as long as the longest answer: the relay sends no more than it was sent. */
    memset(m, 0, sizeof m);
    memcpy(m, "G64R", 4);
    m[4] = R_LIST;
    put_u32(m + RELAY_HEAD, build);
    for (i = 0; i < count && i < NET_RELAY_ADDRS; i++)
        raw_send(s, &relays[i], m, sizeof m);
}

int net_rooms_parse(const uint8_t *d, int n, net_room *out, int max) {
    int count, i, at = RELAY_HEAD + 1, got = 0;
    if (!d || n < RELAY_HEAD + 1 || memcmp(d, "G64R", 4) != 0 || d[4] != R_ROOMS)
        return -1;
    count = d[RELAY_HEAD];
    for (i = 0; i < count; i++) {
        int k, len;
        if (at + NET_ROOM_CODE + 4 > n)
            return -1;
        len = d[at + NET_ROOM_CODE + 3];
        if (len > NET_ROOM_TITLE || at + NET_ROOM_CODE + 4 + len > n)
            return -1;
        if (got < max && out) {
            net_room *r = &out[got];
            /* The code and the title are text from the network: letters and digits, and printable ASCII. */
            for (k = 0; k < NET_ROOM_CODE; k++) {
                uint8_t c = d[at + k];
                if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                    return -1;
                r->code[k] = (char)c;
            }
            r->code[NET_ROOM_CODE] = 0;
            r->wanted = d[at + NET_ROOM_CODE];
            r->joined = d[at + NET_ROOM_CODE + 1];
            r->flags = d[at + NET_ROOM_CODE + 2];
            for (k = 0; k < len; k++) {
                uint8_t c = d[at + NET_ROOM_CODE + 4 + k];
                r->title[k] = c >= 32 && c < 127 ? (char)c : '?';
            }
            r->title[len] = 0;
            got++;
        }
        at += NET_ROOM_CODE + 4 + len;
    }
    return got;
}

net_addr net_relay_player(int p) {
    net_addr a;
    memset(&a, 0, sizeof a);
    a.family = NET_FAMILY_RELAY;
    a.ip[0] = (uint8_t)p;
    return a;
}

int net_send(net_socket *s, const net_addr *to, const void *data, size_t len) {
    uint8_t m[NET_MAX_DATAGRAM + 1];
    if (len > NET_MAX_DATAGRAM)
        return -1;
    if (s->relay == NET_RELAY_JOIN) {
        relay_send(s, R_TO_HOST, data, len);
        return 0;
    }
    if (s->relay == NET_RELAY_HOST) {
        if (to->family != NET_FAMILY_RELAY || to->ip[0] < 1)
            return -1;
        m[0] = to->ip[0];
        memcpy(m + 1, data, len);
        relay_send(s, R_TO_PEER, m, len + 1);
        return 0;
    }
    return raw_send(s, to, data, len);
}

int net_relay_parse(net_socket *s, const net_addr *src, const uint8_t *d, int n, net_addr *from, void *buf,
                    size_t size) {
    int i;
    /* Only the relay speaks to a socket in relay mode. */
    if (!s->relay || n < RELAY_HEAD || memcmp(d, "G64R", 4) != 0)
        return -1;
    for (i = 0; i < s->relayAddrs; i++)
        if (net_addr_equal(src, &s->relayAddr[i]))
            break;
    if (i == s->relayAddrs)
        return -1;
    /* Before its first answer, the relay may answer from any of its addresses; game data comes only after it. */
    if (s->relayAddrs > 1 && d[4] != R_ROOM && d[4] != R_JOINED && d[4] != R_REFUSED)
        return -1;
    switch (d[4]) {
    case R_ROOM:
        if (s->relay == NET_RELAY_HOST && n == RELAY_HEAD + NET_ROOM_CODE) {
            settle(s, i);
            memcpy(s->code, d + RELAY_HEAD, NET_ROOM_CODE);
            s->code[NET_ROOM_CODE] = 0;
            s->state = 1;
        }
        return -1;
    case R_JOINED:
        if (s->relay == NET_RELAY_JOIN && n == RELAY_HEAD + 1 && d[RELAY_HEAD] >= 1) {
            settle(s, i);
            s->peer = d[RELAY_HEAD];
            s->state = 1;
        }
        return -1;
    case R_REFUSED:
        /* A ban (reason 3) can also come to a joiner who is in the room. */
        if (s->relay == NET_RELAY_JOIN && n == RELAY_HEAD + 1 && (s->state == 0 || d[RELAY_HEAD] == 3)) {
            settle(s, i);
            s->state = -1;
            s->refusal = d[RELAY_HEAD];
        }
        return -1;
    case R_FROM_PEER:
        if (s->relay == NET_RELAY_HOST && n >= RELAY_HEAD + 1 && d[RELAY_HEAD] >= 1 &&
            n - RELAY_HEAD - 1 <= NET_MAX_DATAGRAM && (size_t)(n - RELAY_HEAD - 1) <= size) {
            *from = net_relay_player(d[RELAY_HEAD]);
            memcpy(buf, d + RELAY_HEAD + 1, (size_t)(n - RELAY_HEAD - 1));
            return n - RELAY_HEAD - 1;
        }
        return -1;
    case R_FROM_HOST:
        if (s->relay == NET_RELAY_JOIN && n - RELAY_HEAD <= NET_MAX_DATAGRAM && (size_t)(n - RELAY_HEAD) <= size) {
            *from = net_relay_player(0);
            memcpy(buf, d + RELAY_HEAD, (size_t)(n - RELAY_HEAD));
            return n - RELAY_HEAD;
        }
        return -1;
    default:
        return -1;
    }
}

int net_recv(net_socket *s, net_addr *from, void *buf, size_t size) {
    uint8_t d[NET_MAX_DATAGRAM + RELAY_HEAD + 2];
    net_addr src;
    int n;
    if (!s->relay)
        return raw_recv(s, from, buf, size, NET_MAX_DATAGRAM);
    while ((n = raw_recv(s, &src, d, sizeof d, NET_MAX_DATAGRAM + RELAY_HEAD + 1)) >= 0) {
        int got = net_relay_parse(s, &src, d, n, from, buf, size);
        if (got >= 0)
            return got;
    }
    return -1;
}
