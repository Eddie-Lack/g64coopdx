/* Instances (instances.h). */
#include "instances.h"

#include <stdlib.h>
#include <string.h>

/* weemitt's side: src/game/g64players.h and src/port/frame.h, which use the game's types. */
typedef struct {
    uint32_t levels;
    uint32_t allGaribs;
    uint8_t hubTarget;
    uint8_t hubLevel;
    uint8_t worldComplete;
    uint8_t pad;
} G64Save;
typedef struct {
    int32_t player, type, complete, level, world;
    G64Save save;
} G64ExitRecord;
extern int g64Online;
extern G64ExitRecord g64Exits[16];
extern int g64ExitCount;
void G64StartPlayers(unsigned present);
void G64SetStartRecord(int world, const G64Save *save);
void port_game_start_level(int level);


static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

static size_t log_frame_size(int players) { return 2 + 4 * (size_t)players; }

/* ---- Building an instance's game */

typedef struct {
    int level, world;
    unsigned members;
    g64_progress progress;
} start_record;

static void build_setup(void *ctx) {
    const start_record *r = ctx;
    G64Save s;
    memset(&s, 0, sizeof s);
    s.levels = r->progress.levels;
    s.allGaribs = r->progress.all_garibs;
    s.hubTarget = r->progress.hub_target;
    s.hubLevel = r->progress.hub_level;
    s.worldComplete = r->progress.worlds_done;
    g64Online = 1;
    G64StartPlayers(r->members);
    G64SetStartRecord(r->world, &s);
    port_game_start_level(r->level);
}

static rb_engine_game *build(const void *boot, const start_record *r) {
    return rb_engine_game_build(boot, build_setup, (void *)r);
}

static unsigned all_players(int players) { return players >= 16 ? 0xFFFFu : (1u << players) - 1u; }

/* ---- The host */

typedef struct inst {
    struct g64_ihost *h;
    int used;
    uint8_t id;
    start_record start;
    rb_engine_game *game;
    rb_host *host;
    rb_input *log; /* log[f * players + p] */
    uint32_t log_len, log_cap;
} inst;

typedef struct {
    int active;
    int slot;
    uint8_t id;
    uint32_t join;
    uint32_t acked;
    int got_ack;
} move_state;

typedef struct {
    int slot;
    G64ExitRecord r;
} pending_exit;

#define MAX_PENDING 64
/* Log chunks sent to one moving player per host frame, and frames between two sends of a move. */
#define CHUNKS_PER_TICK 8
#define MOVE_RESEND 8

struct g64_ihost {
    Engine *e;
    const void *boot;
    const rb_game_ops *ops;
    int players;
    uint32_t deadline;
    g64_isend_fn send;
    void *user;
    inst inst[G64_MAX_INSTANCES];
    uint8_t next_id;
    int where[RB_MAX_PLAYERS]; /* each player's instance slot, or -1 */
    move_state move[RB_MAX_PLAYERS];
    g64_progress progress;
    pending_exit exits[MAX_PENDING];
    int exit_count;
    uint32_t clock;
    uint32_t matches, mismatches; /* of instances that have ended */
};

static void host_rb_send(void *user, int to, const uint8_t *data, size_t len) {
    inst *in = user;
    uint8_t m[2 + RB_MAX_MESSAGE];
    if (len > RB_MAX_MESSAGE)
        return;
    m[0] = G64_IMSG_RB;
    m[1] = in->id;
    memcpy(m + 2, data, len);
    in->h->send(in->h->user, to, m, len + 2);
}

static void host_on_confirm(void *user, uint32_t frame, const rb_input *in, int players) {
    inst *i = user;
    (void)frame;
    if (i->log_len >= G64_MAX_LOG)
        return;
    if (i->log_len == i->log_cap) {
        uint32_t cap = i->log_cap ? i->log_cap * 2 : 1024;
        rb_input *n = realloc(i->log, sizeof(rb_input) * (size_t)cap * (size_t)players);
        if (!n)
            return;
        i->log = n;
        i->log_cap = cap;
    }
    memcpy(i->log + (size_t)i->log_len * (size_t)players, in, sizeof(rb_input) * (size_t)players);
    i->log_len++;
}

/* After each confirmed frame the engine still holds the instance's reference game: its exits are final. */
static void host_on_final(void *user, uint32_t frame, const void *state) {
    inst *i = user;
    g64_ihost *h = i->h;
    int k;
    (void)frame, (void)state;
    for (k = 0; k < g64ExitCount && k < 16 && h->exit_count < MAX_PENDING; k++) {
        h->exits[h->exit_count].slot = (int)(i - h->inst);
        h->exits[h->exit_count].r = g64Exits[k];
        h->exit_count++;
    }
}

static int find_instance(g64_ihost *h, int level, int world) {
    int i;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used && h->inst[i].start.level == level && h->inst[i].start.world == world)
            return i;
    return -1;
}

static int new_instance(g64_ihost *h, int level, int world, unsigned members) {
    int i, p;
    inst *in = NULL;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (!h->inst[i].used) {
            in = &h->inst[i];
            break;
        }
    if (!in)
        return -1;
    memset(in, 0, sizeof *in);
    in->h = h;
    in->id = h->next_id++;
    in->start.level = level;
    in->start.world = world;
    in->start.members = members;
    in->start.progress = h->progress;
    in->game = build(h->boot, &in->start);
    if (!in->game)
        return -1;
    in->host = rb_host_new(h->ops, in->game, h->players, h->deadline, host_rb_send, host_on_confirm, host_on_final,
                           in);
    if (!in->host) {
        rb_engine_game_free(in->game);
        return -1;
    }
    for (p = 0; p < h->players; p++)
        if (!(members & (1u << p)))
            rb_host_set_present(in->host, p, 0);
    in->used = 1;
    return i;
}

static void free_instance(g64_ihost *h, int i) {
    inst *in = &h->inst[i];
    if (!in->used)
        return;
    h->matches += rb_host_hash_matches(in->host);
    h->mismatches += rb_host_hash_mismatches(in->host);
    rb_host_free(in->host);
    rb_engine_game_free(in->game);
    free(in->log);
    memset(in, 0, sizeof *in);
}

static void send_move(g64_ihost *h, int p) {
    const move_state *m = &h->move[p];
    const inst *in = &h->inst[m->slot];
    g64_move mv;
    uint8_t d[G64_MOVE_SIZE];
    mv.instance = in->id;
    mv.level = (uint8_t)in->start.level;
    mv.world = (uint8_t)in->start.world;
    mv.players = (uint8_t)h->players;
    mv.members = (uint16_t)in->start.members;
    mv.join = m->join;
    mv.progress = in->start.progress;
    g64_write_move(d, &mv);
    h->send(h->user, p, d, sizeof d);
}

static void send_log(g64_ihost *h, int p) {
    const move_state *m = &h->move[p];
    const inst *in = &h->inst[m->slot];
    uint8_t d[G64_LOG_HEAD + 255 * (2 + 4 * RB_MAX_PLAYERS)];
    uint32_t per = (uint32_t)((RB_MAX_WIRE - G64_LOG_HEAD) / log_frame_size(h->players)), first = m->acked;
    int chunk;
    if (per > 255)
        per = 255;
    for (chunk = 0; chunk < CHUNKS_PER_TICK && first < m->join; chunk++) {
        uint32_t count = m->join - first < per ? m->join - first : per, f;
        int q;
        d[0] = G64_IMSG_LOG;
        d[1] = in->id;
        d[2] = (uint8_t)h->players;
        put32(d + 3, first);
        d[7] = (uint8_t)count;
        d[8] = 0;
        for (f = 0; f < count; f++) {
            uint8_t *o = d + G64_LOG_HEAD + (size_t)f * log_frame_size(h->players);
            const rb_input *src = in->log + (size_t)(first + f) * (size_t)h->players;
            uint32_t present = 0;
            for (q = 0; q < h->players; q++) {
                put16(o + 2 + 4 * q, src[q].buttons);
                o[2 + 4 * q + 2] = (uint8_t)src[q].x;
                o[2 + 4 * q + 3] = (uint8_t)src[q].y;
                present |= (uint32_t)(src[q].present & 1) << q;
            }
            put16(o, present);
        }
        h->send(h->user, p, d, G64_LOG_HEAD + (size_t)count * log_frame_size(h->players));
        first += count;
    }
}

static int members_of(const g64_ihost *h, int slot) {
    int p, n = 0;
    for (p = 0; p < h->players; p++)
        n += h->where[p] == slot;
    return n;
}

/* Takes player p out of their instance; the instance ends when nobody is left in it. */
static void leave_instance(g64_ihost *h, int p) {
    int s = h->where[p];
    if (s < 0)
        return;
    rb_host_set_present(h->inst[s].host, p, 0);
    h->where[p] = -1;
    if (members_of(h, s) == 0)
        free_instance(h, s);
}

static void merge_progress(g64_progress *a, const G64Save *s) {
    a->levels |= s->levels;
    a->all_garibs |= s->allGaribs;
    if (s->hubTarget > a->hub_target)
        a->hub_target = s->hubTarget;
    if (s->hubLevel > a->hub_level)
        a->hub_level = s->hubLevel;
    a->worlds_done |= (uint8_t)(s->worldComplete & 0x7F);
}

static void take_exit(g64_ihost *h, const pending_exit *e) {
    int p = e->r.player, s = e->slot, t;
    if (p < 0 || p >= h->players || h->where[p] != s || e->r.level < 0 || e->r.level >= G64_NUM_LEVELS ||
        e->r.world < 0 || e->r.world >= G64_NUM_WORLDS)
        return;
    merge_progress(&h->progress, &e->r.save);
    leave_instance(h, p);
    t = find_instance(h, e->r.level, e->r.world);
    memset(&h->move[p], 0, sizeof h->move[p]);
    if (t < 0) {
        t = new_instance(h, e->r.level, e->r.world, 1u << p);
        if (t < 0)
            return;
        h->move[p].join = 0;
    } else {
        h->move[p].join = rb_host_confirmed(h->inst[t].host);
        if (h->move[p].join > G64_MAX_LOG)
            return;
        rb_host_set_present(h->inst[t].host, p, 1);
    }
    h->where[p] = t;
    h->move[p].active = 1;
    h->move[p].slot = t;
    h->move[p].id = h->inst[t].id;
    send_move(h, p);
}

g64_ihost *g64_ihost_new(Engine *e, const void *boot, int players, int level, uint32_t deadline, g64_isend_fn send,
                         void *user) {
    g64_ihost *h;
    int p, s;
    if (!e || !boot || players < 1 || players > RB_MAX_PLAYERS || level < 0 || level >= G64_NUM_LEVELS || !send)
        return NULL;
    h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->e = e;
    h->boot = boot;
    h->ops = rb_engine_ops(e);
    h->players = players;
    h->deadline = deadline;
    h->send = send;
    h->user = user;
    s = new_instance(h, level, 0, all_players(players));
    if (s < 0) {
        free(h);
        return NULL;
    }
    for (p = 0; p < RB_MAX_PLAYERS; p++)
        h->where[p] = p < players ? s : -1;
    return h;
}

void g64_ihost_free(g64_ihost *h) {
    int i;
    if (!h)
        return;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        free_instance(h, i);
    free(h);
}

void g64_ihost_tick(g64_ihost *h) {
    int i, p;
    h->clock++;
    h->exit_count = 0;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used)
            rb_host_tick(h->inst[i].host);
    for (i = 0; i < h->exit_count; i++)
        take_exit(h, &h->exits[i]);
    for (p = 0; p < h->players; p++) {
        move_state *m = &h->move[p];
        if (!m->active)
            continue;
        if (!h->inst[m->slot].used || h->where[p] != m->slot) {
            m->active = 0;
            continue;
        }
        if (!m->got_ack) {
            if ((h->clock + (uint32_t)p) % MOVE_RESEND == 0)
                send_move(h, p);
        } else if (m->acked < m->join) {
            send_log(h, p);
        } else {
            m->active = 0;
        }
    }
}

int g64_ihost_receive(g64_ihost *h, int from, const uint8_t *data, size_t len) {
    int s;
    if (!data || len < 2 || from < 0 || from >= h->players)
        return -1;
    switch (data[0]) {
    case G64_IMSG_RB:
        s = h->where[from];
        /* A message for an instance the player has left (they were moved) is old: it is dropped. */
        if (s < 0 || h->inst[s].id != data[1])
            return 0;
        return rb_host_receive(h->inst[s].host, from, data + 2, len - 2);
    case G64_IMSG_LOGACK: {
        uint8_t id;
        uint32_t join, have;
        move_state *m = &h->move[from];
        if (g64_parse_logack(data, len, &id, &join, &have) != 0)
            return -1;
        if (m->active && id == m->id && join == m->join) {
            m->got_ack = 1;
            if (have > m->acked)
                m->acked = have;
        }
        return 0;
    }
    default:
        return -1;
    }
}

void g64_ihost_drop(g64_ihost *h, int p) {
    if (p >= 0 && p < h->players) {
        h->move[p].active = 0;
        leave_instance(h, p);
    }
}

int g64_ihost_instances(const g64_ihost *h) {
    int i, n = 0;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        n += h->inst[i].used;
    return n;
}

int g64_ihost_level_of(const g64_ihost *h, int p) {
    if (p < 0 || p >= h->players || h->where[p] < 0)
        return -1;
    return h->inst[h->where[p]].start.level;
}

uint32_t g64_ihost_hash_matches(const g64_ihost *h) {
    uint32_t n = h->matches;
    int i;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used)
            n += rb_host_hash_matches(h->inst[i].host);
    return n;
}

uint32_t g64_ihost_hash_mismatches(const g64_ihost *h) {
    uint32_t n = h->mismatches;
    int i;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used)
            n += rb_host_hash_mismatches(h->inst[i].host);
    return n;
}

/* ---- The player */

struct g64_iclient {
    Engine *e;
    const void *boot;
    const rb_game_ops *ops;
    int players, me, window;
    uint32_t deadline;
    g64_isend_fn send;
    void *user;
    uint8_t id;
    int level;
    rb_engine_game *game;
    rb_client *rb;
    int moving;
    g64_move mv;
    rb_input *log;
    uint32_t have;
    uint32_t desyncs;
    uint64_t rollback_frames;
};

static void client_rb_send(void *user, int to, const uint8_t *data, size_t len) {
    g64_iclient *c = user;
    uint8_t m[2 + RB_MAX_MESSAGE];
    (void)to;
    if (len > RB_MAX_MESSAGE)
        return;
    m[0] = G64_IMSG_RB;
    m[1] = c->id;
    memcpy(m + 2, data, len);
    c->send(c->user, 0, m, len + 2);
}

static void send_ack(g64_iclient *c) {
    uint8_t d[G64_LOGACK_SIZE];
    d[0] = G64_IMSG_LOGACK;
    d[1] = c->mv.instance;
    put32(d + 2, c->mv.join);
    put32(d + 6, c->have);
    c->send(c->user, 0, d, sizeof d);
}

static void drop_game(g64_iclient *c) {
    if (c->rb) {
        c->desyncs += rb_client_desyncs(c->rb);
        c->rollback_frames += rb_client_rollback_frames(c->rb);
        rb_client_free(c->rb);
        c->rb = NULL;
    }
    if (c->game) {
        rb_engine_game_free(c->game);
        c->game = NULL;
    }
}

/* The move's log is complete: build the instance, run its log, and start at the join frame. */
static int finish_move(g64_iclient *c) {
    start_record r;
    rb_input last[RB_MAX_PLAYERS];
    uint32_t f;
    int p;
    r.level = c->mv.level;
    r.world = c->mv.world;
    r.members = c->mv.members;
    r.progress = c->mv.progress;
    c->game = build(c->boot, &r);
    if (!c->game)
        return -1;
    for (f = 0; f < c->mv.join; f++)
        c->ops->step(c->game, c->log + (size_t)f * (size_t)c->players, c->players);
    if (c->mv.join > 0) {
        memcpy(last, c->log + (size_t)(c->mv.join - 1) * (size_t)c->players, sizeof(rb_input) * (size_t)c->players);
    } else {
        memset(last, 0, sizeof last);
        for (p = 0; p < c->players; p++)
            last[p].present = (uint8_t)((r.members >> p) & 1);
    }
    c->rb = rb_client_new_at(c->ops, c->game, c->players, c->me, c->window, c->mv.join, last, client_rb_send, NULL,
                             c);
    if (!c->rb)
        return -1;
    c->id = c->mv.instance;
    c->level = c->mv.level;
    c->moving = 0;
    free(c->log);
    c->log = NULL;
    return 0;
}

g64_iclient *g64_iclient_new(Engine *e, const void *boot, int players, int me, int level, int window,
                             uint32_t deadline, g64_isend_fn send, void *user) {
    g64_iclient *c;
    start_record r;
    if (!e || !boot || players < 1 || players > RB_MAX_PLAYERS || me < 0 || me >= players || level < 0 ||
        level >= G64_NUM_LEVELS || !send)
        return NULL;
    c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->e = e;
    c->boot = boot;
    c->ops = rb_engine_ops(e);
    c->players = players;
    c->me = me;
    c->window = window;
    c->deadline = deadline;
    c->send = send;
    c->user = user;
    c->level = level;
    memset(&r, 0, sizeof r);
    r.level = level;
    r.members = all_players(players);
    c->game = build(boot, &r);
    c->rb = c->game ? rb_client_new(c->ops, c->game, players, me, window, client_rb_send, NULL, c) : NULL;
    if (!c->rb) {
        drop_game(c);
        free(c);
        return NULL;
    }
    return c;
}

void g64_iclient_free(g64_iclient *c) {
    if (!c)
        return;
    drop_game(c);
    free(c->log);
    free(c);
}

int g64_iclient_receive(g64_iclient *c, const uint8_t *data, size_t len) {
    if (!data || len < 2)
        return -1;
    switch (data[0]) {
    case G64_IMSG_RB:
        if (c->moving || !c->rb || data[1] != c->id)
            return 0;
        return rb_client_receive(c->rb, data + 2, len - 2);
    case G64_IMSG_MOVE: {
        g64_move mv;
        if (g64_parse_move(data, len, &mv) != 0 || mv.players != c->players)
            return -1;
        if (c->moving && mv.instance == c->mv.instance && mv.join == c->mv.join) {
            send_ack(c); /* the host did not get the ack */
            return 0;
        }
        if (!c->moving && c->rb && mv.instance == c->id && mv.join == c->mv.join) {
            send_ack(c); /* a resent move that was already done: the host did not get the last ack */
            return 0;
        }
        drop_game(c);
        free(c->log);
        c->log = NULL;
        c->mv = mv;
        c->have = 0;
        c->moving = 1;
        if (mv.join > 0) {
            c->log = calloc((size_t)mv.join * (size_t)c->players, sizeof(rb_input));
            if (!c->log)
                return -1;
        }
        send_ack(c);
        if (mv.join == 0)
            return finish_move(c);
        return 0;
    }
    case G64_IMSG_LOG: {
        g64_log_chunk ch;
        uint32_t i;
        if (g64_parse_log(data, len, &ch) != 0 || ch.players != c->players)
            return -1;
        if (!c->moving || ch.instance != c->mv.instance)
            return 0;
        if (ch.first == c->have) {
            for (i = 0; i < ch.count && c->have < c->mv.join; i++, c->have++)
                g64_log_frame(&ch, (int)i, c->log + (size_t)c->have * (size_t)c->players);
        }
        send_ack(c);
        if (c->have == c->mv.join)
            return finish_move(c);
        return 0;
    }
    default:
        return -1;
    }
}

/* Extra frames a player runs in one step while it is behind the host's clock (after it joined a running instance). */
#define CATCH_UP 8

int g64_iclient_step(g64_iclient *c, rb_input local) {
    int n;
    if (c->moving || !c->rb)
        return 0;
    if (!rb_client_step(c->rb, local)) {
        rb_client_idle(c->rb);
        return 0;
    }
    /* In step with the host, a player runs about deadline frames ahead of the confirmed ones. Fewer means it is
       behind the host's clock: it runs more frames now, with the same input. */
    for (n = 0; n < CATCH_UP && rb_client_frame(c->rb) < rb_client_confirmed(c->rb) + c->deadline; n++)
        if (!rb_client_step(c->rb, local))
            break;
    return 1;
}

int g64_iclient_moving(const g64_iclient *c) { return c->moving; }
rb_engine_game *g64_iclient_game(g64_iclient *c) { return c->moving ? NULL : c->game; }
int g64_iclient_level(const g64_iclient *c) { return c->level; }
uint32_t g64_iclient_frame(const g64_iclient *c) { return c->rb ? rb_client_frame(c->rb) : 0; }
uint32_t g64_iclient_confirmed(const g64_iclient *c) { return c->rb ? rb_client_confirmed(c->rb) : 0; }
uint32_t g64_iclient_desyncs(const g64_iclient *c) { return c->desyncs + (c->rb ? rb_client_desyncs(c->rb) : 0); }
uint64_t g64_iclient_rollback_frames(const g64_iclient *c) {
    return c->rollback_frames + (c->rb ? rb_client_rollback_frames(c->rb) : 0);
}
