/* A libFuzzer target for the messages that move players between instances (src/client/imsg.c): a move, a log chunk
 * with its session events, a log acknowledgement, and a progress event. Each input goes to the four parsers. What a
 * parser accepts is read back in full, and a move, a chunk's events or a progress event it accepts must give the same
 * bytes when written again.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/imsg.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g64_move m;
    static g64_log_chunk c;
    g64_progress pr;
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
            m2.progress.worlds_done != m.progress.worlds_done || memcmp(m2.colors, m.colors, sizeof m.colors) != 0 ||
            m2.session_world != m.session_world || m.session_world >= G64_NUM_WORLDS)
            abort();
    }
    if (g64_parse_log(data, size, &c) == 0) {
        rb_input in[RB_MAX_PLAYERS];
        int i;
        if (c.players < 1 || c.players > RB_MAX_PLAYERS || c.count < 1)
            abort();
        for (i = 0; i < c.count; i++) {
            g64_log_frame(&c, i, in);
            if (c.events[i].len > RB_EVENT_MAX)
                abort();
        }
        {
            /* The events after the frames, written again, are the same bytes. */
            static uint8_t again[1 + 255 * (2 + RB_EVENT_MAX)];
            size_t frames = G64_LOG_HEAD + (size_t)c.count * (2 + 4 * (size_t)c.players);
            size_t n = rb_events_write(again, c.events, c.count);
            if (n != size - frames || memcmp(again, data + frames, n) != 0)
                abort();
        }
    }
    if (g64_parse_progress_event(data, size, &pr) == 0) {
        uint8_t again[G64_EVENT_PROGRESS_SIZE];
        if (pr.hub_target > G64_MAX_HUB || pr.hub_level > G64_MAX_HUB || g64_write_progress_event(again, &pr) != size ||
            memcmp(again, data, size) != 0)
            abort();
    }
    {
        int player;
        uint32_t glove, ball;
        if (g64_parse_colors_event(data, size, &player, &glove, &ball) == 0) {
            uint8_t again[G64_EVENT_COLORS_SIZE];
            if (player < 0 || player > 15 || !g64_valid_color(glove) || !g64_valid_color(ball) ||
                g64_write_colors_event(again, player, glove, ball) != size || memcmp(again, data, size) != 0)
                abort();
        }
    }
    {
        int world;
        if (g64_parse_world_event(data, size, &world) == 0 && (world < 0 || world >= G64_NUM_WORLDS))
            abort();
    }
    if (g64_parse_logack(data, size, &id, &join, &have) == 0 && have > join)
        abort();
    return 0;
}
