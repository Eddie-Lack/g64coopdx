/* A libFuzzer target for the messages that move players between instances (src/client/imsg.c): a move, a log chunk
 * and a log acknowledgement. Each input goes to the three parsers. What a parser accepts is read back in full, and a
 * move it accepts must give the same bytes when written again.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../src/client/imsg.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g64_move m;
    g64_log_chunk c;
    uint8_t id;
    uint32_t join, have;
    if (g64_parse_move(data, size, &m) == 0) {
        uint8_t again[G64_MOVE_SIZE];
        g64_move m2;
        if (m.level >= G64_NUM_LEVELS || m.players < 1 || m.players > RB_MAX_PLAYERS || m.join > G64_MAX_LOG)
            abort();
        g64_write_move(again, &m);
        if (g64_parse_move(again, sizeof again, &m2) != 0 || m2.instance != m.instance || m2.level != m.level ||
            m2.world != m.world || m2.players != m.players || m2.members != m.members || m2.join != m.join ||
            m2.progress.levels != m.progress.levels || m2.progress.all_garibs != m.progress.all_garibs ||
            m2.progress.hub_target != m.progress.hub_target || m2.progress.hub_level != m.progress.hub_level ||
            m2.progress.worlds_done != m.progress.worlds_done)
            abort();
    }
    if (g64_parse_log(data, size, &c) == 0) {
        rb_input in[RB_MAX_PLAYERS];
        int i;
        if (c.players < 1 || c.players > RB_MAX_PLAYERS || c.count < 1)
            abort();
        for (i = 0; i < c.count; i++)
            g64_log_frame(&c, i, in);
    }
    if (g64_parse_logack(data, size, &id, &join, &have) == 0 && have > join)
        abort();
    return 0;
}
