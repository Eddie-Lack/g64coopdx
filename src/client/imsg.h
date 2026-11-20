/* The messages that move players between instances (instances.h gives their layouts). Pure functions: no game, no
 * network. The fuzz target (tests/fuzz/instances_fuzz.c) feeds them random bytes. */
#ifndef G64_IMSG_H
#define G64_IMSG_H

#include <stddef.h>
#include <stdint.h>

#include "../rollback/rollback.h"

/* The levels and worlds a move may name (weemitt's enum Level and enum World). */
#define G64_NUM_LEVELS 48
#define G64_NUM_WORLDS 7

#define G64_IMSG_RB 0x01
#define G64_IMSG_MOVE 0x02
#define G64_IMSG_LOG 0x03
#define G64_IMSG_LOGACK 0x04

#define G64_MOVE_SIZE 23
#define G64_LOG_HEAD 9
#define G64_LOGACK_SIZE 10
/* The most frames a log may hold: 4 hours at 20 frames a second. A longer instance cannot take a new player. */
#define G64_MAX_LOG (20 * 3600 * 4)
/* The most instances at once: one per player. */
#define G64_MAX_INSTANCES 16

/* The session's progress (weemitt's G64Save). */
typedef struct {
    uint32_t levels;
    uint32_t all_garibs;
    uint8_t hub_target;
    uint8_t hub_level;
    uint8_t worlds_done;
} g64_progress;

/* A move, as the host sends it and a player reads it. */
typedef struct {
    uint8_t instance;
    uint8_t level, world, players;
    uint16_t members; /* the instance's players at its start */
    uint32_t join;    /* the frame the player joins at: they run the log's frames before it */
    g64_progress progress;
} g64_move;

/* The parsers (also the fuzz targets' way in). Each returns 0, or -1 for a malformed message. */
int g64_parse_move(const uint8_t *d, size_t len, g64_move *out);
size_t g64_write_move(uint8_t *d, const g64_move *m);
/* A log chunk: frames first .. first + count - 1 of an instance with players players. frames points into d. events
 * holds each frame's session event (len 0: none). */
typedef struct {
    uint8_t instance, players, count;
    uint32_t first;
    const uint8_t *frames;
    rb_event events[255];
} g64_log_chunk;
int g64_parse_log(const uint8_t *d, size_t len, g64_log_chunk *out);
/* One frame of a chunk, as confirmed inputs. */
void g64_log_frame(const g64_log_chunk *c, int i, rb_input *out);
int g64_parse_logack(const uint8_t *d, size_t len, uint8_t *instance, uint32_t *join, uint32_t *have);

/* The session events an instance's confirmed frames carry (rb_event): type (u8), then its fields.
 *   progress  0x01, levels (u32), all garibs (u32), hub target (u8), hub level (u8), worlds done (u8): the session's
 *             progress, which the game joins with its own (weemitt's G64ApplyProgress) */
#define G64_EVENT_PROGRESS 0x01
#define G64_EVENT_PROGRESS_SIZE 12
/* The hub counts a progress may hold: HUB1 .. HUB8 are levels 0 .. 7. */
#define G64_MAX_HUB 7
size_t g64_write_progress_event(uint8_t *d, const g64_progress *p);
int g64_parse_progress_event(const uint8_t *d, size_t len, g64_progress *out);

#endif
