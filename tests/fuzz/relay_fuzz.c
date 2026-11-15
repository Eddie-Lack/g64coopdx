/* A libFuzzer target for the client's side of the relay protocol (net_relay_parse in src/net/udp.c). The first byte
 * of each input picks the socket (a host, a joiner, or a joiner that knows two addresses of the relay) and the sender
 * (the relay's address, its second address, or another); the rest is the datagram.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/net/udp.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static net_socket *host, *join, *join2;
    static net_addr relay, other, relays[2];
    const net_addr *src;
    net_socket *s;
    uint8_t buf[NET_MAX_DATAGRAM];
    net_addr from;
    if (!host) {
        char err[64];
        net_init();
        host = net_open(0, err, sizeof err);
        join = net_open(0, err, sizeof err);
        join2 = net_open(0, err, sizeof err);
        memset(&relay, 0, sizeof relay);
        relay.family = 4, relay.ip[0] = 127, relay.ip[3] = 9, relay.port = 27065;
        other = relay;
        other.port = 1;
        net_relay_host(host, &relay, 1);
        net_relay_join(join, &relay, 1, "ABC234");
        relays[0] = relay;
        relays[1] = relay;
        relays[1].ip[3] = 10;
        net_relay_join(join2, relays, 2, "ABC234");
    }
    if (size < 1 || size > NET_MAX_DATAGRAM + 16)
        return 0;
    s = (data[0] & 3) == 0 ? host : (data[0] & 3) == 1 ? join : join2;
    src = data[0] & 4 ? &other : data[0] & 8 ? &relays[1] : &relay;
    net_relay_parse(s, src, data + 1, (int)(size - 1), &from, buf, data[0] & 16 ? 8 : sizeof buf);
    return 0;
}
