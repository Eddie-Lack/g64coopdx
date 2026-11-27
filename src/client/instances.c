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
    uint8_t raisedBit;
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
void G64ApplyProgress(const G64Save *save);
void port_game_start_level(int level);

/* enum ExitType and enum Level (weemitt/include/enums.h): the exits that lead to the hub, and its last level. */
enum { WORLD_EXIT = 3, ATLANTIS_EXIT = 6, SPACE_EXIT = 11, HUB_EXIT = 13, HUB8 = 7, CAVE = 8 };
/* weemitt's G64_RAISED_CAVE (g64players.h). */
#define RAISED_CAVE 0xFF


static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

static size_t log_frame_size(int players) { return 2 + 4 * (size_t)players; }

static void to_save(const g64_progress *p, G64Save *s) {
    memset(s, 0, sizeof *s);
    s->levels = p->levels;
    s->allGaribs = p->all_garibs;
    s->hubTarget = p->hub_target;
    s->hubLevel = p->hub_level;
    s->worldComplete = p->worlds_done;
}

static int same_progress(const g64_progress *a, const g64_progress *b) {
    return a->levels == b->levels && a->all_garibs == b->all_garibs && a->hub_target == b->hub_target &&
           a->hub_level == b->hub_level && a->worlds_done == b->worlds_done;
}

/* A session event, in the game that's about to run the event's frame (rb_engine_set_event_fn). The bytes come from
   the network: a malformed event changes nothing. */
extern unsigned g64PlayerColor[16][2];
void G64SetSessionWorld(int world);
/* The session's values for scripts (weemitt's g64lua.c): one value into a running game, all of them for a game
   that starts, and what a game's script asked for in its last frame. */
void G64LuaSetSession(int key, unsigned value);
void G64SetScriptSession(const unsigned *values);
int G64LuaRequests(int out[][2], int max);

static void apply_event(const uint8_t *data, size_t len) {
    g64_progress p;
    G64Save s;
    int player;
    uint32_t glove, ball;
    if (g64_parse_script_event(data, len, &player, &glove) == 0) {
        G64LuaSetSession(player, glove);
        return;
    }
    if (g64_parse_world_event(data, len, &player) == 0) {
        G64SetSessionWorld(player);
        return;
    }
    if (g64_parse_colors_event(data, len, &player, &glove, &ball) == 0) {
        /* The colors are part of the game's state: every machine sets them at this frame. */
        g64PlayerColor[player][0] = glove;
        g64PlayerColor[player][1] = ball;
        return;
    }
    if (g64_parse_progress_event(data, len, &p) != 0)
        return;
    to_save(&p, &s);
    G64ApplyProgress(&s);
}

/* ---- Building an instance's game */

typedef struct {
    int level, world;
    unsigned members;
    g64_progress progress;
    uint32_t colors[16][2]; /* every player's colors at the start */
    int session_world;      /* the world the session is in at the start, or 0 */
    uint32_t values[G64_SCRIPT_KEYS]; /* the session's values for scripts at the start */
} start_record;

/* The host's "any ball" rule (weemitt's g64players.c). Every game that is built starts with it. */
extern int g64AnyBall;
static int anyBallRule;
/* The host's "one world at a time" rule (weemitt's g64players.c). */
extern int g64OneWorld;
static int oneWorldRule;

void g64_instances_any_ball(int on) { anyBallRule = on != 0; }
void g64_instances_one_world(int on) { oneWorldRule = on != 0; }

/* The session's level pack (weemitt's g64players.c, G64SetPack). */
void G64SetPack(int level, unsigned rom, unsigned size);
void G64SetPackScript(unsigned rom, unsigned size);
void G64SetPackModels(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
void G64SetPackRetail(int first, const unsigned *tex, int numTex, const unsigned *obj, int numObj);
void G64LuaStart(void);
static int packLevel = -1;
static unsigned packRom, packSize, packScriptRom, packScriptSize;
static unsigned packTexRom, packTexSize, packObjRom, packObjSize;
static unsigned packRetailTex[4], packRetailObj[4];
static int packNumRetailTex, packNumRetailObj, packRetailFirst;

void g64_instances_pack(int level, unsigned rom, unsigned size) {
    packLevel = size ? level : -1;
    packRom = rom;
    packSize = size;
}

void g64_instances_pack_script(unsigned rom, unsigned size) {
    packScriptRom = rom;
    packScriptSize = size;
}

void g64_instances_pack_models(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize) {
    packTexRom = texRom;
    packTexSize = texSize;
    packObjRom = objRom;
    packObjSize = objSize;
    packNumRetailTex = packNumRetailObj = packRetailFirst = 0;
}

void g64_instances_pack_retail(int first, const unsigned char *tex, int numTex, const unsigned char *obj, int numObj) {
    int i;
    packNumRetailTex = numTex < 0 ? 0 : numTex > 4 ? 4 : numTex;
    packNumRetailObj = numObj < 0 ? 0 : numObj > 4 ? 4 : numObj;
    for (i = 0; i < packNumRetailTex; i++)
        packRetailTex[i] = tex[i];
    for (i = 0; i < packNumRetailObj; i++)
        packRetailObj[i] = obj[i];
    packRetailFirst = first != 0;
}

static void build_setup(void *ctx) {
    const start_record *r = ctx;
    G64Save s;
    int p;
    to_save(&r->progress, &s);
    g64Online = 1;
    g64AnyBall = anyBallRule;
    g64OneWorld = oneWorldRule;
    G64SetPack(packLevel, packRom, packSize);
    G64SetPackScript(packScriptRom, packScriptSize);
    G64SetPackModels(packTexRom, packTexSize, packObjRom, packObjSize);
    G64SetPackRetail(packRetailFirst, packRetailTex, packNumRetailTex, packRetailObj, packNumRetailObj);
    G64SetSessionWorld(r->session_world);
    {
        unsigned values[G64_SCRIPT_KEYS];
        for (p = 0; p < G64_SCRIPT_KEYS; p++)
            values[p] = r->values[p];
        G64SetScriptSession(values);
    }
    for (p = 0; p < 16; p++) {
        g64PlayerColor[p][0] = r->colors[p][0];
        g64PlayerColor[p][1] = r->colors[p][1];
    }
    G64StartPlayers(r->members);
    G64SetStartRecord(r->world, &s);
    port_game_start_level(r->level);
    /* The pack's script starts with the game: its first run is before the first frame. */
    G64LuaStart();
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
    rb_event *log_event; /* log_event[f]: each logged frame's session event */
    uint32_t log_len, log_cap;
    g64_progress sent; /* the progress the instance has: its start's, then the last progress event's */
    uint32_t colors[16][2]; /* the colors the instance has: its start's, then the colors events' */
    /* The frame the instance's last event went on, plus 1, or 0: one event fits on a frame. */
    uint32_t event_frame;
    int event_colors; /* that event set colors, the session's world or a script's value, which a progress event must
                         not replace */
    uint32_t values[G64_SCRIPT_KEYS]; /* the scripts' values the instance has: its start's, then the events' */
    int next_key;                     /* the key the next script event starts looking at */
    int world;        /* the session's world as the instance has it */
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
    uint32_t values[G64_SCRIPT_KEYS]; /* the session's values for scripts: 0 at the session's start */
    const void *boot;
    const rb_game_ops *ops;
    int players;
    int level; /* the session's first level: a player who joins later starts there */
    uint32_t colors[16][2]; /* every player's colors now */
    uint32_t deadline;
    g64_isend_fn send;
    void *user;
    inst inst[G64_MAX_INSTANCES];
    uint8_t next_id;
    int where[RB_MAX_PLAYERS]; /* each player's instance slot, or -1 */
    move_state move[RB_MAX_PLAYERS];
    g64_progress progress;
    /* Why the hub's target rose (G64Save.raisedBit): the levels whose first world exit raised it, and the first visit
       to the cave. */
    uint32_t raised;
    int raised_cave;
    /* A player who waits to come back into the instance they left (take_exit). */
    struct {
        int wait, level, world, type, from;
        int keep; /* 1: the way out of the cave to the stage the player came from, while that stage runs */
        int id; /* the instance they wait for, or -1 for any */
        uint32_t frame;
    } back[RB_MAX_PLAYERS];
    /* The hub stage each player went into the crystal cave from, or -1: the cave's exit leads back there. */
    int caveFrom[RB_MAX_PLAYERS];
    pending_exit exits[MAX_PENDING];
    int exit_count;
    uint32_t clock;
    uint32_t matches, mismatches; /* of instances that have ended */
    g64_final_fn on_final;
    void *final_user;
    g64_move_fn on_move;
    void *move_user;
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

static void host_on_confirm(void *user, uint32_t frame, const rb_input *in, int players, const rb_event *event) {
    inst *i = user;
    (void)frame;
    if (i->log_len >= G64_MAX_LOG)
        return;
    if (i->log_len == i->log_cap) {
        uint32_t cap = i->log_cap ? i->log_cap * 2 : 1024;
        rb_input *n = realloc(i->log, sizeof(rb_input) * (size_t)cap * (size_t)players);
        rb_event *e;
        if (!n)
            return;
        i->log = n;
        e = realloc(i->log_event, sizeof(rb_event) * (size_t)cap);
        if (!e)
            return;
        i->log_event = e;
        i->log_cap = cap;
    }
    memcpy(i->log + (size_t)i->log_len * (size_t)players, in, sizeof(rb_input) * (size_t)players);
    i->log_event[i->log_len] = *event;
    i->log_len++;
}

/* After each confirmed frame the engine still holds the instance's reference game: its exits are final. */
static void host_on_final(void *user, uint32_t frame, const void *state) {
    inst *i = user;
    g64_ihost *h = i->h;
    int k;
    if (h->on_final)
        h->on_final(h->final_user, i->id, frame, state);
    for (k = 0; k < g64ExitCount && k < 16 && h->exit_count < MAX_PENDING; k++) {
        h->exits[h->exit_count].slot = (int)(i - h->inst);
        h->exits[h->exit_count].r = g64Exits[k];
        h->exit_count++;
    }
    {
        /* What the instance's script asked to add to the session's values in this frame. The sum is taken here, once
           for each confirmed frame, in whole numbers of 32 bits that wrap. */
        int req[8][2], n = G64LuaRequests(req, 8);
        for (k = 0; k < n; k++)
            if (req[k][0] >= 0 && req[k][0] < G64_SCRIPT_KEYS)
                h->values[req[k][0]] += (uint32_t)req[k][1];
    }
}

static int find_instance(g64_ihost *h, int level, int world) {
    int i;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used && h->inst[i].start.level == level && h->inst[i].start.world == world)
            return i;
    return -1;
}

static int members_of(const g64_ihost *h, int slot);

/* The world the session is in: the world of an instance that has players, or of one that's about to be made
   (starting), or 0 while every player is in the hub. */
static int session_world(const g64_ihost *h, int starting) {
    int i;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        if (h->inst[i].used && h->inst[i].start.world != 0 && members_of(h, i) > 0)
            return h->inst[i].start.world;
    return starting;
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
    memcpy(in->start.colors, h->colors, sizeof in->start.colors);
    memcpy(in->colors, h->colors, sizeof in->colors);
    in->start.session_world = in->world = session_world(h, world);
    memcpy(in->start.values, h->values, sizeof in->start.values);
    memcpy(in->values, h->values, sizeof in->values);
    in->next_key = 0;
    in->sent = h->progress;
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
    free(in->log_event);
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
    mv.session_world = (uint8_t)in->start.session_world;
    memcpy(mv.colors, in->start.colors, sizeof mv.colors);
    memcpy(mv.values, in->start.values, sizeof mv.values);
    g64_write_move(d, &mv);
    h->send(h->user, p, d, sizeof d);
}

static void send_log(g64_ihost *h, int p) {
    const move_state *m = &h->move[p];
    const inst *in = &h->inst[m->slot];
    uint8_t d[G64_LOG_HEAD + 255 * (2 + 4 * RB_MAX_PLAYERS) + 1 + 255 * (2 + RB_EVENT_MAX)];
    uint32_t first = m->acked;
    int chunk;
    for (chunk = 0; chunk < CHUNKS_PER_TICK && first < m->join; chunk++) {
        uint32_t count, f;
        size_t size = G64_LOG_HEAD + 1;
        int q;
        /* As many frames as fit in RB_MAX_WIRE bytes with their events, at most 255. */
        for (count = 0; count < 255 && first + count < m->join; count++) {
            const rb_event *e = &in->log_event[first + count];
            size_t more = log_frame_size(h->players) + (e->len ? 2u + e->len : 0u);
            if (count > 0 && size + more > RB_MAX_WIRE)
                break;
            size += more;
        }
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
        size = G64_LOG_HEAD + (size_t)count * log_frame_size(h->players);
        size += rb_events_write(d + size, in->log_event + first, count);
        h->send(h->user, p, d, size);
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

static int bits_set(uint32_t v) {
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

static void merge_progress(g64_ihost *h, const G64Save *s) {
    g64_progress *a = &h->progress;
    a->levels |= s->levels;
    a->all_garibs |= s->allGaribs;
    if (s->hubTarget > a->hub_target)
        a->hub_target = s->hubTarget;
    /* Each level's first world exit raises the target once, wherever it was taken. Two levels finished in two
       instances before either heard of the other each report the same raised value: the count of levels is right. */
    if (s->raisedBit >= 1 && s->raisedBit <= 32)
        h->raised |= 1u << (s->raisedBit - 1);
    else if (s->raisedBit == RAISED_CAVE)
        h->raised_cave = 1;
    if (bits_set(h->raised) + h->raised_cave > a->hub_target)
        a->hub_target = (uint8_t)(bits_set(h->raised) + h->raised_cave);
    if (a->hub_target > G64_MAX_HUB)
        a->hub_target = G64_MAX_HUB;
    if (s->hubLevel > a->hub_level)
        a->hub_level = s->hubLevel;
    a->worlds_done |= (uint8_t)(s->worldComplete & 0x7F);
}

static int valid_exit(const g64_ihost *h, const pending_exit *e) {
    int p = e->r.player;
    return p >= 0 && p < h->players && h->where[p] == e->slot && e->r.level >= 0 && e->r.level < G64_NUM_LEVELS &&
           e->r.world >= 0 && e->r.world < G64_NUM_WORLDS;
}

/* Puts player p, who is in no instance, into the instance of level and world, or into a new one, and sends them
   the move. type and from are for the log (on_move): the exit they took and the level they left, or -1 for a player
   who joins the session. Returns 0, or -1 when there's no room. */
static int enter(g64_ihost *h, int p, int level, int world, int type, int from) {
    int t = find_instance(h, level, world);
    memset(&h->move[p], 0, sizeof h->move[p]);
    if (t < 0) {
        t = new_instance(h, level, world, 1u << p);
        if (t < 0)
            return -1;
        h->move[p].join = 0;
    } else {
        h->move[p].join = rb_host_confirmed(h->inst[t].host);
        if (h->move[p].join > G64_MAX_LOG)
            return -1;
        rb_host_set_present(h->inst[t].host, p, 1);
    }
    h->where[p] = t;
    h->move[p].active = 1;
    h->move[p].slot = t;
    h->move[p].id = h->inst[t].id;
    if (h->on_move)
        h->on_move(h->move_user, p, type, from, level, world, h->move[p].join, &h->progress);
    send_move(h, p);
    return 0;
}

/* Moves the player of an exit. The exits' progress is already merged (g64_ihost_tick). */
static void take_exit(g64_ihost *h, const pending_exit *e) {
    int p = e->r.player, s = e->slot, from, level = e->r.level;
    if (!valid_exit(h, e))
        return;
    /* An exit to the hub leads to its newest stage. The level the player leaves may not have heard yet of a crystal
       put in the fountain elsewhere. */
    if ((e->r.type == WORLD_EXIT || e->r.type == HUB_EXIT) && level <= HUB8)
        level = h->progress.hub_level <= HUB8 ? h->progress.hub_level : HUB8;
    from = h->inst[s].start.level;
    /* The way out of the crystal cave leads back to the hub stage the player came from, where the friends they left
       still are. The game's own exit would take the newest stage: the first visit to the cave counts as a crystal.
       With nobody left in that stage, its instance has ended. A new one would start the stage from its beginning:
       the first stage would play its opening again and have the first ball in its place. The player then goes to
       the newest stage, as in the game. */
    if (level == CAVE && from <= HUB8)
        h->caveFrom[p] = from;
    if (from == CAVE && level <= HUB8 && e->r.world == 0 && h->caveFrom[p] >= 0) {
        if (find_instance(h, h->caveFrom[p], 0) >= 0)
            level = h->caveFrom[p];
        h->caveFrom[p] = -1;
    }
    leave_instance(h, p);
    {
        /* One world at a time: the game shuts the hub's portals to other worlds, but two players may walk into
           two portals before either's game has heard of the other. The second one comes out in the hub again. */
        int world = e->r.world, now = session_world(h, 0);
        if (oneWorldRule && e->r.type >= ATLANTIS_EXIT && e->r.type <= SPACE_EXIT && now != 0 && now != world) {
            level = h->progress.hub_level <= HUB8 ? h->progress.hub_level : HUB8;
            world = 0;
        }
        /* An exit that leads back into the instance the player has just left: in that game they're gone from
           the next confirmed frame, and must be absent for a frame before they come again. They enter once the
           instance has confirmed that frame (g64_ihost_tick). */
        h->back[p].level = level;
        h->back[p].world = world;
        h->back[p].type = e->r.type;
        h->back[p].from = from;
        h->back[p].keep = from == CAVE && level != (h->progress.hub_level <= HUB8 ? h->progress.hub_level : HUB8);
        if (h->inst[s].used && find_instance(h, level, world) == s) {
            h->back[p].wait = 1;
            h->back[p].id = h->inst[s].id;
            h->back[p].frame = rb_host_confirmed(h->inst[s].host) + 1;
            return;
        }
        /* No room for the player now (every instance is in use, or the level's log is full): the host tries again
           on its next frames. */
        if (enter(h, p, level, world, e->r.type, from) != 0) {
            h->back[p].wait = 1;
            h->back[p].id = -1;
        }
    }
}

void g64_ihost_set_colors(g64_ihost *h, int p, uint32_t glove, uint32_t ball) {
    if (p < 0 || p >= h->players || !g64_valid_color(glove) || !g64_valid_color(ball))
        return;
    h->colors[p][0] = glove;
    h->colors[p][1] = ball;
}

int g64_ihost_add(g64_ihost *h, int p) {
    int level = h->level;
    if (p < 0 || p >= h->players)
        return -1;
    if (h->where[p] >= 0)
        return 0;
    /* A first level in the hub means its newest stage, as an exit to the hub does. */
    if (level <= HUB8)
        level = h->progress.hub_level <= HUB8 ? h->progress.hub_level : HUB8;
    return enter(h, p, level, 0, -1, -1);
}

g64_ihost *g64_ihost_new(Engine *e, const void *boot, int players, int level, uint32_t deadline, g64_isend_fn send,
                         void *user) {
    return g64_ihost_new_with(e, boot, players, all_players(players), NULL, level, deadline, send, user);
}

g64_ihost *g64_ihost_new_with(Engine *e, const void *boot, int players, unsigned members,
                              const uint32_t colors[16][2], int level, uint32_t deadline, g64_isend_fn send,
                              void *user) {
    g64_ihost *h;
    int p, s;
    if (!e || !boot || players < 1 || players > RB_MAX_PLAYERS || level < 0 || level >= G64_NUM_LEVELS || !send ||
        members == 0 || (members & ~all_players(players)) != 0)
        return NULL;
    h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->e = e;
    h->boot = boot;
    h->ops = rb_engine_ops(e);
    rb_engine_set_event_fn(apply_event);
    h->players = players;
    h->level = level;
    h->deadline = deadline;
    h->send = send;
    h->user = user;
    /* Without a table, the colors are the ones the game has now. */
    for (p = 0; p < 16; p++) {
        h->colors[p][0] = colors ? colors[p][0] : g64PlayerColor[p][0];
        h->colors[p][1] = colors ? colors[p][1] : g64PlayerColor[p][1];
    }
    for (p = 0; p < RB_MAX_PLAYERS; p++)
        h->where[p] = h->caveFrom[p] = -1;
    s = new_instance(h, level, 0, members);
    if (s < 0) {
        free(h);
        return NULL;
    }
    for (p = 0; p < players; p++)
        if (members & (1u << p))
            h->where[p] = s;
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
    /* The progress of every exit of this frame first: an exit to the hub then goes to the newest stage, even when the
       exit that made it comes later in the list. */
    for (i = 0; i < h->exit_count; i++)
        if (valid_exit(h, &h->exits[i]))
            merge_progress(h, &h->exits[i].r.save);
    for (i = 0; i < h->exit_count; i++)
        take_exit(h, &h->exits[i]);
    for (p = 0; p < h->players; p++) {
        int t;
        if (!h->back[p].wait)
            continue;
        /* The hub's newest stage, as it is now. The way out of the cave keeps its stage while friends are in it. */
        if (h->back[p].keep && find_instance(h, h->back[p].level, 0) < 0)
            h->back[p].keep = 0;
        if (h->back[p].world == 0 && h->back[p].level <= HUB8 && !h->back[p].keep)
            h->back[p].level = h->progress.hub_level <= HUB8 ? h->progress.hub_level : HUB8;
        t = find_instance(h, h->back[p].level, h->back[p].world);
        /* They wait only for the instance they left. If it has ended, another one of the level is new to them. */
        if (t >= 0 && h->inst[t].id == h->back[p].id && rb_host_confirmed(h->inst[t].host) <= h->back[p].frame)
            continue;
        if (h->where[p] >= 0 ||
            enter(h, p, h->back[p].level, h->back[p].world, h->back[p].type, h->back[p].from) == 0)
            h->back[p].wait = 0;
        else
            h->back[p].id = -1;
    }
    /* Progress made in one instance goes to every other, at its next confirmed frame: the whole progress, so a newer
       event may replace one not yet confirmed. */
    for (i = 0; i < G64_MAX_INSTANCES; i++) {
        inst *in = &h->inst[i];
        uint8_t ev[RB_EVENT_MAX];
        uint32_t next;
        if (!in->used)
            continue;
        next = rb_host_confirmed(in->host);
        if (!same_progress(&in->sent, &h->progress)) {
            /* The whole progress: it may replace a progress event that isn't confirmed yet, but not a colors
               event, which waits for a frame of its own. */
            if (in->event_frame == next + 1 && in->event_colors)
                continue;
            if (rb_host_set_event(in->host, ev, g64_write_progress_event(ev, &h->progress)) == 0) {
                in->sent = h->progress;
                in->event_frame = next + 1;
                in->event_colors = 0;
            }
            continue;
        }
        /* The session's world, then a player's new colors: one of them on a frame. */
        if (in->event_frame == next + 1)
            continue;
        if (in->world != session_world(h, 0)) {
            if (rb_host_set_event(in->host, ev, g64_write_world_event(ev, session_world(h, 0))) == 0) {
                in->world = session_world(h, 0);
                in->event_frame = next + 1;
                in->event_colors = 1;
            }
            continue;
        }
        for (p = 0; p < h->players; p++)
            if (in->colors[p][0] != h->colors[p][0] || in->colors[p][1] != h->colors[p][1]) {
                if (rb_host_set_event(in->host, ev,
                                      g64_write_colors_event(ev, p, h->colors[p][0], h->colors[p][1])) == 0) {
                    in->colors[p][0] = h->colors[p][0];
                    in->colors[p][1] = h->colors[p][1];
                    in->event_frame = next + 1;
                    in->event_colors = 1;
                }
                break;
            }
        if (p < h->players)
            continue;
        /* A script's value that the instance doesn't have yet: one on a frame, the keys in turn. A key that changes
           often doesn't hold the others back. The event carries the value as it is now. */
        for (p = 0; p < G64_SCRIPT_KEYS; p++) {
            int k = (in->next_key + p) % G64_SCRIPT_KEYS;
            if (in->values[k] == h->values[k])
                continue;
            if (rb_host_set_event(in->host, ev, g64_write_script_event(ev, k, h->values[k])) == 0) {
                in->values[k] = h->values[k];
                in->next_key = (k + 1) % G64_SCRIPT_KEYS;
                in->event_frame = next + 1;
                in->event_colors = 1;
            }
            break;
        }
    }
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
    /* A player who is in the session and in no instance asks for one. */
    if (data && len == G64_READY_SIZE && data[0] == G64_IMSG_READY && from >= 0 && from < h->players) {
        if (h->where[from] < 0 && !h->back[from].wait)
            g64_ihost_add(h, from);
        return 0;
    }
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
        h->back[p].wait = 0;
        h->move[p].active = 0;
        h->caveFrom[p] = -1;
        leave_instance(h, p);
    }
}

void g64_ihost_on_move(g64_ihost *h, g64_move_fn fn, void *user) {
    h->on_move = fn;
    h->move_user = user;
}

void g64_ihost_on_final(g64_ihost *h, g64_final_fn fn, void *user) {
    h->on_final = fn;
    h->final_user = user;
}

int g64_ihost_instances(const g64_ihost *h) {
    int i, n = 0;
    for (i = 0; i < G64_MAX_INSTANCES; i++)
        n += h->inst[i].used;
    return n;
}

int g64_ihost_world(const g64_ihost *h) { return session_world(h, 0); }

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
    int players, me, window, input_delay;
    uint32_t deadline;
    g64_isend_fn send;
    void *user;
    uint8_t id;
    int level;
    rb_engine_game *game;
    rb_client *rb;
    int moving;
    uint32_t asked;
    g64_move mv;
    rb_input *log;
    rb_event *log_event;
    uint32_t have;
    uint32_t desyncs;
    uint64_t rollback_frames;
    g64_final_fn on_final;
    void *final_user;
};

static void client_final(void *user, uint32_t frame, const void *state) {
    g64_iclient *c = user;
    if (c->on_final)
        c->on_final(c->final_user, c->id, frame, state);
}

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
    r.session_world = c->mv.session_world;
    memcpy(r.colors, c->mv.colors, sizeof r.colors);
    memcpy(r.values, c->mv.values, sizeof r.values);
    c->game = build(c->boot, &r);
    if (!c->game)
        return -1;
    for (f = 0; f < c->mv.join; f++) {
        const rb_event *e = &c->log_event[f];
        if (e->len && c->ops->event)
            c->ops->event(c->game, e->data, e->len);
        c->ops->step(c->game, c->log + (size_t)f * (size_t)c->players, c->players);
    }
    if (c->mv.join > 0) {
        memcpy(last, c->log + (size_t)(c->mv.join - 1) * (size_t)c->players, sizeof(rb_input) * (size_t)c->players);
    } else {
        memset(last, 0, sizeof last);
        for (p = 0; p < c->players; p++)
            last[p].present = (uint8_t)((r.members >> p) & 1);
    }
    c->rb = rb_client_new_at(c->ops, c->game, c->players, c->me, c->window, c->mv.join, last, client_rb_send,
                             client_final, c);
    if (!c->rb)
        return -1;
    rb_client_set_input_delay(c->rb, c->input_delay);
    c->id = c->mv.instance;
    c->level = c->mv.level;
    c->moving = 0;
    free(c->log);
    free(c->log_event);
    c->log = NULL;
    c->log_event = NULL;
    return 0;
}

g64_iclient *g64_iclient_new(Engine *e, const void *boot, int players, int me, int level, int window,
                             uint32_t deadline, g64_isend_fn send, void *user) {
    return g64_iclient_new_with(e, boot, players, all_players(players), NULL, me, level, window, deadline, send, user);
}

g64_iclient *g64_iclient_new_with(Engine *e, const void *boot, int players, unsigned members,
                                  const uint32_t colors[16][2], int me, int level, int window, uint32_t deadline,
                                  g64_isend_fn send, void *user) {
    g64_iclient *c;
    start_record r;
    int p;
    if (!e || !boot || players < 1 || players > RB_MAX_PLAYERS || me < 0 || me >= players || level < 0 ||
        level >= G64_NUM_LEVELS || !send || (members & ~all_players(players)) != 0)
        return NULL;
    c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->e = e;
    c->boot = boot;
    c->ops = rb_engine_ops(e);
    rb_engine_set_event_fn(apply_event);
    c->players = players;
    c->me = me;
    c->window = window;
    c->deadline = deadline;
    c->send = send;
    c->user = user;
    c->level = level;
    /* A player who isn't there at the start has no game yet: they ask the host for one (g64_iclient_step). */
    if (!(members & (1u << me)))
        return c;
    memset(&r, 0, sizeof r);
    r.level = level;
    r.members = members;
    for (p = 0; p < 16; p++) {
        r.colors[p][0] = colors ? colors[p][0] : g64PlayerColor[p][0];
        r.colors[p][1] = colors ? colors[p][1] : g64PlayerColor[p][1];
    }
    c->game = build(boot, &r);
    if (c->game) {
        rb_input first[RB_MAX_PLAYERS];
        memset(first, 0, sizeof first);
        for (p = 0; p < players; p++)
            first[p].present = (uint8_t)((members >> p) & 1);
        c->rb = rb_client_new_at(c->ops, c->game, players, me, window, 0, first, client_rb_send, client_final, c);
    }
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
    free(c->log_event);
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
            send_ack(c); /* the host didn't get the ack */
            return 0;
        }
        if (!c->moving && c->rb && mv.instance == c->id && mv.join == c->mv.join) {
            send_ack(c); /* a resent move that was already done: the host didn't get the last ack */
            return 0;
        }
        drop_game(c);
        free(c->log);
        free(c->log_event);
        c->log = NULL;
        c->log_event = NULL;
        c->mv = mv;
        c->have = 0;
        c->moving = 1;
        if (mv.join > 0) {
            c->log = calloc((size_t)mv.join * (size_t)c->players, sizeof(rb_input));
            c->log_event = calloc((size_t)mv.join, sizeof(rb_event));
            if (!c->log || !c->log_event) {
                /* No room for the log: the move isn't taken. The host sends it again. */
                free(c->log);
                free(c->log_event);
                c->log = NULL;
                c->log_event = NULL;
                c->moving = 0;
                return -1;
            }
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
            for (i = 0; i < ch.count && c->have < c->mv.join; i++, c->have++) {
                g64_log_frame(&ch, (int)i, c->log + (size_t)c->have * (size_t)c->players);
                c->log_event[c->have] = ch.events[i];
            }
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

/* Extra frames a player runs in one step while it's behind the host's clock (after it joined a running instance). */
#define CATCH_UP 8

int g64_iclient_step(g64_iclient *c, rb_input local) {
    int n;
    if (!c->moving && !c->rb) {
        /* In the session and in no instance: ask the host for one, a few times a second, until its move comes. */
        if (c->asked++ % 8 == 0) {
            uint8_t m[G64_READY_SIZE] = {G64_IMSG_READY, 0};
            c->send(c->user, 0, m, sizeof m);
        }
        return 0;
    }
    if (c->moving || !c->rb)
        return 0;
    if (!rb_client_step(c->rb, local)) {
        rb_client_idle(c->rb);
        return 0;
    }
    /* A player behind the host's clock (after it joined a running instance) runs more frames now, with the same input,
       up to the frame the client aims for (rb_client_target). */
    for (n = 0; n < CATCH_UP && rb_client_frame(c->rb) < rb_client_target(c->rb); n++)
        if (!rb_client_step(c->rb, local))
            break;
    return 1;
}

int g64_iclient_set_input_delay(g64_iclient *c, int frames) {
    if (c->rb && rb_client_set_input_delay(c->rb, frames) != 0)
        return -1;
    c->input_delay = frames;
    return 0;
}

void g64_iclient_on_final(g64_iclient *c, g64_final_fn fn, void *user) {
    c->on_final = fn;
    c->final_user = user;
}

int g64_iclient_instance(const g64_iclient *c) { return c->id; }
int g64_iclient_moving(const g64_iclient *c) { return c->moving; }
rb_engine_game *g64_iclient_game(g64_iclient *c) { return c->moving ? NULL : c->game; }
int g64_iclient_level(const g64_iclient *c) { return c->level; }
uint32_t g64_iclient_frame(const g64_iclient *c) { return c->rb ? rb_client_frame(c->rb) : 0; }
uint32_t g64_iclient_confirmed(const g64_iclient *c) { return c->rb ? rb_client_confirmed(c->rb) : 0; }
uint32_t g64_iclient_desyncs(const g64_iclient *c) { return c->desyncs + (c->rb ? rb_client_desyncs(c->rb) : 0); }
uint64_t g64_iclient_rollback_frames(const g64_iclient *c) {
    return c->rollback_frames + (c->rb ? rb_client_rollback_frames(c->rb) : 0);
}
