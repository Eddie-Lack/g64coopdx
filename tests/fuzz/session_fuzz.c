/* A libFuzzer target for the session's message parsers (g64_session_receive and, behind the seal,
 * g64_session_test_inner). The first byte of each input picks the receiver (a host or a joiner), one of four
 * senders, and whether the rest is a raw datagram or an opened message. Both sessions live across
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

static void chat(void *user, int from, const char *text) {
    (void)user, (void)from;
    /* The line must be a string of printable characters. */
    for (; *text; text++)
        if (*text < 32 || *text > 126)
            __builtin_trap();
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
    }
    /* The sessions are made again whenever the joiner has failed or the host has lost a player: the fuzzer cannot
       make a datagram that opens, so nothing it sends keeps a player alive, and a kick ends the joiner. */
    if (!host || !g64_session_name(host, 1)[0] || !g64_session_name(host, 2)[0] || g64_session_me(join) != 1 ||
        g64_session_poll(join, now, game, NULL) == G64_SESSION_FAILED) {
        /* Two players join the host, and the joiner is welcomed: the messages behind the seal need players
           to come from. The public keys are any bytes that give a secret. */
        uint8_t hello[53 + 4] = {0x40, 'G', '6', '4', G64_PROTOCOL, 0xcd, 0xab, 0x34, 0x12};
        uint8_t welcome[35] = {0x41, 1, 4};
        if (host)
            g64_session_free(host), g64_session_free(join);
        host = g64_session_host(sock, 4, 0x1234abcdu, 0, 3);
        join = g64_session_join(sock, &senders[0], 0x1234abcdu, "fuzz");
        g64_session_on_chat(host, chat, NULL);
        g64_session_on_chat(join, chat, NULL);
        memset(hello + 21, 9, 32);
        memcpy(hello + 53, "fuzz", 4);
        memset(welcome + 3, 7, 32);
        g64_session_receive(host, &senders[1], hello, sizeof hello, now, game, NULL);
        hello[21] = 10;
        g64_session_receive(host, &senders[2], hello, sizeof hello, now, game, NULL);
        g64_session_poll(join, now, game, NULL);
        g64_session_receive(join, &senders[0], welcome, sizeof welcome, now, game, NULL);
        if (g64_session_joined(host) != 3 || g64_session_me(join) != 1)
            __builtin_trap();
    }
    if (size < 1)
        return 0;
    now += 7;
    /* Half of the inputs are raw datagrams: hellos, welcomes, refusals, and sealed datagrams that do not open.
       The other half are messages as they are after the seal is opened: no fuzzer can make a datagram that opens,
       and the parser behind the seal must still meet every input. */
    if (data[0] & 0x40)
        g64_session_test_inner(data[0] & 0x80 ? join : host, 1 + (data[0] & 3), data + 1, size - 1, now, game, NULL);
    else
        g64_session_receive(data[0] & 0x80 ? join : host, &senders[data[0] & 3], data + 1, size - 1, now, game, NULL);
    /* Now and then the host removes a player, or someone says something. */
    if (size > 2 && data[1] == 0xEE)
        g64_session_kick(host, 1 + (data[2] & 3));
    if (size > 2 && data[1] == 0xED)
        g64_session_chat(data[0] & 0x80 ? join : host, "a line");
    if (now % 64 == 0) {
        g64_session_poll(host, now, game, NULL);
        g64_session_poll(join, now, game, NULL);
    }
    return 0;
}
