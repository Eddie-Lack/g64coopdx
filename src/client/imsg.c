/* The messages that move players between instances (imsg.h). */
#include "imsg.h"
#include "../session/session.h"

static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

size_t g64_write_move(uint8_t *d, const g64_move *m) {
    d[0] = G64_IMSG_MOVE;
    d[1] = m->instance;
    d[2] = m->level;
    d[3] = m->world;
    d[4] = m->players;
    put16(d + 5, m->members);
    put32(d + 7, m->join);
    put32(d + 11, m->progress.levels);
    put32(d + 15, m->progress.all_garibs);
    d[19] = m->progress.hub_target;
    d[20] = m->progress.hub_level;
    d[21] = m->progress.worlds_done;
    d[22] = m->session_world;
    {
        int p;
        for (p = 0; p < 16; p++) {
            put32(d + G64_MOVE_HEAD + 8 * p, m->colors[p][0]);
            put32(d + G64_MOVE_HEAD + 8 * p + 4, m->colors[p][1]);
        }
    }
    {
        int k;
        for (k = 0; k < G64_SCRIPT_KEYS; k++)
            put32(d + G64_MOVE_VALUES + 4 * k, m->values[k]);
    }
    return G64_MOVE_SIZE;
}

int g64_parse_move(const uint8_t *d, size_t len, g64_move *out) {
    g64_move m;
    if (!d || len != G64_MOVE_SIZE || d[0] != G64_IMSG_MOVE)
        return -1;
    m.instance = d[1];
    m.level = d[2];
    m.world = d[3];
    m.players = d[4];
    m.members = (uint16_t)get16(d + 5);
    m.join = get32(d + 7);
    m.progress.levels = get32(d + 11);
    m.progress.all_garibs = get32(d + 15);
    m.progress.hub_target = d[19];
    m.progress.hub_level = d[20];
    m.progress.worlds_done = d[21];
    m.session_world = d[22];
    if (m.session_world >= G64_NUM_WORLDS)
        return -1;
    if (m.level >= G64_NUM_LEVELS || m.world >= G64_NUM_WORLDS || m.players < 1 || m.players > RB_MAX_PLAYERS ||
        m.join > G64_MAX_LOG || (m.players < 16 && (m.members >> m.players) != 0) || m.members == 0 ||
        (m.progress.worlds_done >> G64_NUM_WORLDS) != 0 || m.progress.hub_target > G64_MAX_HUB ||
        m.progress.hub_level > G64_MAX_HUB)
        return -1;
    {
        int p;
        for (p = 0; p < 16; p++) {
            m.colors[p][0] = get32(d + G64_MOVE_HEAD + 8 * p);
            m.colors[p][1] = get32(d + G64_MOVE_HEAD + 8 * p + 4);
            if (!g64_valid_color(m.colors[p][0]) || !g64_valid_color(m.colors[p][1]))
                return -1;
        }
    }
    {
        /* Any 32 bits are a value. */
        int k;
        for (k = 0; k < G64_SCRIPT_KEYS; k++)
            m.values[k] = get32(d + G64_MOVE_VALUES + 4 * k);
    }
    *out = m;
    return 0;
}

int g64_valid_color(uint32_t c) { return (c >> 29) == 0 && ((c >> 25) & 0xFu) < G64_MODELS; }

size_t g64_write_script_event(uint8_t *d, int key, uint32_t value) {
    d[0] = G64_EVENT_SCRIPT;
    d[1] = (uint8_t)key;
    put32(d + 2, value);
    return G64_EVENT_SCRIPT_SIZE;
}

int g64_parse_script_event(const uint8_t *d, size_t len, int *key, uint32_t *value) {
    if (!d || len != G64_EVENT_SCRIPT_SIZE || d[0] != G64_EVENT_SCRIPT || d[1] >= G64_SCRIPT_KEYS)
        return -1;
    *key = d[1];
    *value = get32(d + 2);
    return 0;
}

size_t g64_write_world_event(uint8_t *d, int world) {
    d[0] = G64_EVENT_WORLD;
    d[1] = (uint8_t)world;
    return G64_EVENT_WORLD_SIZE;
}

int g64_parse_world_event(const uint8_t *d, size_t len, int *world) {
    if (!d || len != G64_EVENT_WORLD_SIZE || d[0] != G64_EVENT_WORLD || d[1] >= G64_NUM_WORLDS)
        return -1;
    *world = d[1];
    return 0;
}

size_t g64_write_colors_event(uint8_t *d, int player, uint32_t glove, uint32_t ball) {
    d[0] = G64_EVENT_COLORS;
    d[1] = (uint8_t)player;
    put32(d + 2, glove);
    put32(d + 6, ball);
    return G64_EVENT_COLORS_SIZE;
}

int g64_parse_colors_event(const uint8_t *d, size_t len, int *player, uint32_t *glove, uint32_t *ball) {
    if (!d || len != G64_EVENT_COLORS_SIZE || d[0] != G64_EVENT_COLORS || d[1] >= 16 ||
        !g64_valid_color(get32(d + 2)) || !g64_valid_color(get32(d + 6)))
        return -1;
    *player = d[1];
    *glove = get32(d + 2);
    *ball = get32(d + 6);
    return 0;
}

static size_t log_frame_size(int players) { return 2 + 4 * (size_t)players; }

int g64_parse_log(const uint8_t *d, size_t len, g64_log_chunk *out) {
    g64_log_chunk c;
    if (!d || len < G64_LOG_HEAD || d[0] != G64_IMSG_LOG)
        return -1;
    c.instance = d[1];
    c.players = d[2];
    c.first = get32(d + 3);
    c.count = d[7];
    c.frames = d + G64_LOG_HEAD;
    if (c.players < 1 || c.players > RB_MAX_PLAYERS || c.count < 1 || c.first > G64_MAX_LOG ||
        c.count > G64_MAX_LOG - c.first || len <= G64_LOG_HEAD + (size_t)c.count * log_frame_size(c.players))
        return -1;
    /* The frames' events follow the frames (rb_events_write). */
    {
        size_t frames = G64_LOG_HEAD + (size_t)c.count * log_frame_size(c.players);
        if (rb_events_parse(d + frames, len - frames, c.count, c.events) != 0)
            return -1;
    }
    *out = c;
    return 0;
}

void g64_log_frame(const g64_log_chunk *c, int i, rb_input *out) {
    const uint8_t *f = c->frames + (size_t)i * log_frame_size(c->players);
    uint32_t present = get16(f);
    int p;
    for (p = 0; p < c->players; p++) {
        const uint8_t *in = f + 2 + 4 * (size_t)p;
        out[p].buttons = (uint16_t)get16(in);
        out[p].x = (int8_t)in[2];
        out[p].y = (int8_t)in[3];
        out[p].present = (uint8_t)((present >> p) & 1);
    }
}

size_t g64_write_progress_event(uint8_t *d, const g64_progress *p) {
    d[0] = G64_EVENT_PROGRESS;
    put32(d + 1, p->levels);
    put32(d + 5, p->all_garibs);
    d[9] = p->hub_target;
    d[10] = p->hub_level;
    d[11] = p->worlds_done;
    return G64_EVENT_PROGRESS_SIZE;
}

int g64_parse_progress_event(const uint8_t *d, size_t len, g64_progress *out) {
    g64_progress p;
    if (!d || len != G64_EVENT_PROGRESS_SIZE || d[0] != G64_EVENT_PROGRESS)
        return -1;
    p.levels = get32(d + 1);
    p.all_garibs = get32(d + 5);
    p.hub_target = d[9];
    p.hub_level = d[10];
    p.worlds_done = d[11];
    if (p.hub_target > G64_MAX_HUB || p.hub_level > G64_MAX_HUB || (p.worlds_done >> G64_NUM_WORLDS) != 0)
        return -1;
    *out = p;
    return 0;
}

int g64_parse_logack(const uint8_t *d, size_t len, uint8_t *instance, uint32_t *join, uint32_t *have) {
    if (!d || len != G64_LOGACK_SIZE || d[0] != G64_IMSG_LOGACK)
        return -1;
    *instance = d[1];
    *join = get32(d + 2);
    *have = get32(d + 6);
    if (*join > G64_MAX_LOG || *have > *join)
        return -1;
    return 0;
}

