/* A libFuzzer target for the session's message parsers (g64_session_receive and, behind the seal,
 * g64_session_test_inner). The first byte of each input picks the receiver (a host or a joiner), one of four
 * senders, and whether the rest is a raw datagram or an opened message. The host has a level pack to send; one of
 * its players lacks it, and a second joiner is in the middle of a download. Both sessions live across
 * inputs and are polled now and then. As a result, messages meet every state.
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
    static g64_session *host, *join, *load, *to;
    static uint8_t packBytes[3000];
    static const uint8_t packId[G64_PACK_ID_SIZE] = {5, 6, 7};
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
    /* The sessions are made again whenever the joiner has failed or the host has lost a player: the fuzzer can't
       make a datagram that opens, so nothing it sends keeps a player alive, and a kick ends the joiner. */
    if (!host || !g64_session_name(host, 1)[0] || !g64_session_name(host, 2)[0] || g64_session_me(join) != 1 ||
        g64_session_poll(join, now, game, NULL) == G64_SESSION_FAILED ||
        g64_session_poll(load, now, game, NULL) == G64_SESSION_FAILED) {
        /* Two players join the host, and the joiner is welcomed: the messages behind the seal need players
           to come from. The public keys are any bytes that give a secret. */
        uint8_t hello[69 + 4] = {0x40, 'G', '6', '4', G64_PROTOCOL, 0xcd, 0xab, 0x34, 0x12};
        uint8_t welcome[35] = {0x41, 1, 4};
        /* The host has a level pack that it may send. Player 1 has it; player 2 lacks it and holds a place. */
        uint8_t offer[31 + 4] = {0x49, 5, 6, 7};
        g64_pack_offer info;
        memset(&info, 0, sizeof info);
        memcpy(info.name, "fuzz", 4);
        info.manifestSize = 40, info.levSize = 2000, info.modelsSize = 20;
        info.scriptSize = sizeof packBytes - 2060;
        if (host)
            g64_session_free(host), g64_session_free(join), g64_session_free(load);
        host = g64_session_host(sock, 4, 0x1234abcdu, 0, 3);
        g64_session_set_pack(host, packId);
        g64_session_send_pack(host, &info, packBytes);
        join = g64_session_join(sock, &senders[0], 0x1234abcdu, "fuzz");
        g64_session_on_chat(host, chat, NULL);
        g64_session_on_chat(join, chat, NULL);
        memset(hello + 21, 9, 32);
        memcpy(hello + 53, packId, sizeof packId);
        memcpy(hello + 69, "fuzz", 4);
        memset(welcome + 3, 7, 32);
        g64_session_receive(host, &senders[1], hello, sizeof hello, now, game, NULL);
        hello[21] = 10;
        memset(hello + 53, 0, G64_PACK_ID_SIZE);
        g64_session_receive(host, &senders[2], hello, sizeof hello, now, game, NULL);
        g64_session_poll(join, now, game, NULL);
        g64_session_receive(join, &senders[0], welcome, sizeof welcome, now, game, NULL);
        /* A second joiner is in the middle of a download: it has the host's offer and its player said yes. */
        load = g64_session_join(sock, &senders[0], 0x1234abcdu, "load");
        g64_session_poll(load, now, game, NULL);
        welcome[1] = 2;
        g64_session_receive(load, &senders[0], welcome, sizeof welcome, now, game, NULL);
        /* The host's offer: a manifest of 40 bytes, a level file of 2000, a script of 940 and a models.bin of 20. */
        offer[17] = 40, offer[19] = 0xD0, offer[20] = 7, offer[23] = 0xAC, offer[24] = 3, offer[27] = 20;
        memcpy(offer + 31, "fuzz", 4);
        g64_session_test_inner(load, 0, offer, sizeof offer, now, game, NULL);
        if (g64_session_joined(host) != 2 || g64_session_me(join) != 1 || g64_session_pack_accept(load) != 0)
            __builtin_trap();
    }
    if (size < 1)
        return 0;
    now += 7;
    /* Half of the inputs are raw datagrams: hellos, welcomes, refusals, and sealed datagrams that don't open.
       The other half are messages as they are after the seal is opened: no fuzzer can make a datagram that opens,
       and the parser behind the seal must still meet every input. */
    to = !(data[0] & 0x80) ? host : data[0] & 0x20 ? load : join;
    if (data[0] & 0x40)
        g64_session_test_inner(to, 1 + (data[0] & 3), data + 1, size - 1, now, game, NULL);
    else
        g64_session_receive(to, &senders[data[0] & 3], data + 1, size - 1, now, game, NULL);
    /* Now and then a joiner's player says yes to the offer, or its program says it has the pack. */
    if (size > 2 && data[1] == 0xEC && to != host)
        g64_session_pack_accept(to);
    if (size > 2 && data[1] == 0xEB && to != host)
        g64_session_pack_ready(to);
    /* A joiner never has more bytes than the offer names, and the offer stays within the pack's limits. */
    for (i = 0; i < 2; i++) {
        const g64_session *j = i ? load : join;
        const g64_pack_offer *o = g64_session_pack_offer(j);
        size_t total = o->manifestSize + o->levSize + o->scriptSize + o->modelsSize;
        const char *c;
        if (o->manifestSize > G64_PACK_MANIFEST_LIMIT || o->levSize > G64_PACK_LEVEL_LIMIT ||
            o->scriptSize > G64_PACK_SCRIPT_LIMIT || o->modelsSize > G64_PACK_MODELS_LIMIT ||
            (o->modelsSize != 0 && o->modelsSize < G64_PACK_MODELS_MIN) || g64_session_pack_have(j) > total ||
            (g64_session_pack_bytes(j) != NULL) != (g64_session_pack_state(j) == G64_PACK_LOADED) ||
            (g64_session_pack_state(j) == G64_PACK_LOADED && g64_session_pack_have(j) != total))
            __builtin_trap();
        for (c = o->name; *c; c++)
            if (*c < 32 || *c > 126)
                __builtin_trap();
    }
    /* Now and then the host removes a player, or someone says something. */
    if (size > 2 && data[1] == 0xEE)
        g64_session_kick(host, 1 + (data[2] & 3));
    if (size > 2 && data[1] == 0xED)
        g64_session_chat(data[0] & 0x80 ? join : host, "a line");
    if (now % 64 == 0) {
        g64_session_poll(host, now, game, NULL);
        g64_session_poll(join, now, game, NULL);
        g64_session_poll(load, now, game, NULL);
    }
    return 0;
}
