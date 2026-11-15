/* A libFuzzer target for the session's message parser (g64_session_receive). The first byte of each input picks
 * the receiver (a host or a joiner) and one of four senders; the rest is the datagram. Both sessions live across
 * inputs and are polled now and then, so messages meet every state.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/net/udp.h"
#include "../../src/session/session.h"

static void game(void *user, int from, const uint8_t *data, size_t len) {
    (void)user, (void)from, (void)data, (void)len;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static g64_session *host, *join;
    static net_socket *sock;
    static net_addr senders[4];
    static uint32_t now = 1;
    int i;
    if (!sock) {
        char err[64];
        net_init();
        sock = net_open(0, err, sizeof err);
        for (i = 0; i < 4; i++) {
            memset(&senders[i], 0, sizeof senders[i]);
            senders[i].family = 4;
            senders[i].ip[0] = 127, senders[i].ip[3] = (uint8_t)(10 + i);
            senders[i].port = (uint16_t)(5000 + i);
        }
        host = g64_session_host(sock, 4, 0x1234abcdu, 0, 3);
        join = g64_session_join(sock, &senders[0], 0x1234abcdu, "fuzz");
    }
    if (size < 1)
        return 0;
    now += 7;
    g64_session_receive(data[0] & 0x80 ? join : host, &senders[data[0] & 3], data + 1, size - 1, now, game, NULL);
    if (now % 64 == 0) {
        g64_session_poll(host, now, game, NULL);
        g64_session_poll(join, now, game, NULL);
    }
    return 0;
}
