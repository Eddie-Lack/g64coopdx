/* A libFuzzer target for the client's side of the relay protocol (net_relay_parse in src/net/udp.c). The first byte
 * of each input picks a host or a joiner socket and whether the datagram comes from the relay's address; the rest is
 * the datagram.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/net/udp.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static net_socket *host, *join;
    static net_addr relay, other;
    uint8_t buf[NET_MAX_DATAGRAM];
    net_addr from;
    if (!host) {
        char err[64];
        net_init();
        host = net_open(0, err, sizeof err);
        join = net_open(0, err, sizeof err);
        memset(&relay, 0, sizeof relay);
        relay.family = 4, relay.ip[0] = 127, relay.ip[3] = 9, relay.port = 27065;
        other = relay;
        other.port = 1;
        net_relay_host(host, &relay);
        net_relay_join(join, &relay, "ABC234");
    }
    if (size < 1 || size > NET_MAX_DATAGRAM + 16)
        return 0;
    net_relay_parse(data[0] & 1 ? join : host, data[0] & 2 ? &other : &relay, data + 1, (int)(size - 1), &from, buf,
                    data[0] & 4 ? 8 : sizeof buf);
    return 0;
}
