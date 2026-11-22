/* g64coopdx: scripts. One Lua state for the game, on a block of memory that is part of the game's own data: a
   savestate holds the block, and a rollback takes the scripts back with the game. The block holds addresses, so its
   bytes differ between machines. The state hash that machines compare covers the game's memory (port_ram) and not
   this block; G64LuaCounts gives numbers that are the same on every machine for the same script and inputs, and
   the online game adds them to the hash it compares (src/rollback/engine_game.c)
   (docs/discovery/lua-trial.md in the repository around this copy of weemitt).

   This file is C, as Lua is, and is built with Lua's options (g64lua_conf.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g64lua_conf.h"
#include "../../third_party/lua/lua.h"
#include "../../third_party/lua/lauxlib.h"
#include "../../third_party/lua/lualib.h"

unsigned char g64LuaArena[G64_LUA_ARENA] __attribute__((aligned(16)));

/* The allocator: blocks with their size in front, a free list in the order of addresses, first fit. Its whole
   state is here and in the block, so the same calls give the same places on every machine. */
typedef struct Block {
    size_t size;
    struct Block *next;
} Block;
#define HEAD 16
static size_t arenaTop;
static Block *arenaFree;
/* Bytes in use, and the allocations and frees so far. */
static unsigned int luaLive, luaAllocs, luaFrees;
static lua_State *luaState;

/* The work that is left to the call into the script that runs now: one for each step of Lua and for each byte the
   script asks for. At 0 the script is stopped (spent, below).
   CALL_BUDGET is the work of one call. The script's first run also compiles its text and builds Lua's libraries,
   so it gets LOAD_BUDGET. The hook runs every HOOK_EVERY steps of Lua, and at every call (CALL_COST). */
#define HOOK_EVERY 1000
#define CALL_COST 50
#define CALL_BUDGET 2000000
#define LOAD_BUDGET 50000000
static long long luaBudget;
static void spent(void);

static void *arena_alloc(size_t n) {
    Block **p, *b;
    if (n > G64_LUA_ARENA) {
        return NULL;
    }
    n = (n + 15) & ~(size_t)15;
    for (p = &arenaFree; (b = *p) != NULL; p = &b->next) {
        if (b->size >= n) {
            if (b->size >= n + 2 * HEAD) {
                Block *rest = (Block *)((unsigned char *)b + HEAD + n);
                rest->size = b->size - n - HEAD;
                rest->next = b->next;
                b->size = n;
                *p = rest;
            } else {
                *p = b->next;
            }
            return (unsigned char *)b + HEAD;
        }
    }
    if (arenaTop + HEAD + n > G64_LUA_ARENA) {
        return NULL;
    }
    b = (Block *)(g64LuaArena + arenaTop);
    b->size = n;
    arenaTop += HEAD + n;
    return (unsigned char *)b + HEAD;
}

/* 1 when block b ends where block c starts. */
static int touches(const Block *b, const Block *c) {
    return (const unsigned char *)b + HEAD + b->size == (const unsigned char *)c;
}

/* Puts a block on the free list and joins it with a free block right after it and right before it. Without the
   joining, a script that asks for ever larger blocks fills the 2 MB with small free ones. */
static void arena_free(void *q) {
    Block *b = (Block *)((unsigned char *)q - HEAD), *prev = NULL, *next;
    for (next = arenaFree; next != NULL && next < b; next = next->next) {
        prev = next;
    }
    if (next != NULL && touches(b, next)) {
        b->size += HEAD + next->size;
        next = next->next;
    }
    b->next = next;
    if (prev == NULL) {
        arenaFree = b;
    } else if (touches(prev, b)) {
        prev->size += HEAD + b->size;
        prev->next = next;
    } else {
        prev->next = b;
    }
}

/* Lua's allocator (lua_Alloc). A request the block cannot meet gives NULL, and Lua ends the script's call with a
   memory error: at the same point on every machine. Each new byte also counts as work, so a function of Lua's
   libraries that builds a large text cannot run past the limit. */
static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    void *n;
    (void)ud;
    if (nsize == 0) {
        if (ptr != NULL) {
            arena_free(ptr);
            luaFrees++;
            luaLive -= (unsigned int)osize;
        }
        return NULL;
    }
    if (ptr != NULL) {
        /* A block that already has the room stays where it is. Lua relies on this for a block that shrinks: that
           must not fail. The part that is no longer used goes back to the free list. */
        Block *b = (Block *)((unsigned char *)ptr - HEAD);
        size_t want = (nsize + 15) & ~(size_t)15;
        if (nsize <= G64_LUA_ARENA && want <= b->size) {
            if (b->size >= want + 2 * HEAD) {
                Block *rest = (Block *)((unsigned char *)ptr + want);
                rest->size = b->size - want - HEAD;
                b->size = want;
                arena_free((unsigned char *)rest + HEAD);
            }
            luaLive = luaLive - (unsigned int)osize + (unsigned int)nsize;
            return ptr;
        }
    }
    if (luaBudget <= 0) {
        return NULL;
    }
    luaBudget -= (long long)nsize;
    if (luaBudget <= 0) {
        spent();
        return NULL;
    }
    n = arena_alloc(nsize);
    if (n == NULL) {
        return NULL;
    }
    luaAllocs++;
    if (ptr != NULL) {
        memcpy(n, ptr, osize < nsize ? osize : nsize);
        arena_free(ptr);
        luaLive -= (unsigned int)osize;
    }
    luaLive += (unsigned int)nsize;
    return n;
}

/* Ends the scripts: the block is empty again. */
void G64LuaReset(void) {
    luaState = NULL;
    arenaTop = 0;
    arenaFree = NULL;
    luaLive = luaAllocs = luaFrees = 0;
    luaBudget = 0;
}

/* 1 when a script is loaded. */
int G64LuaActive(void) {
    return luaState != NULL;
}

/* Numbers that are the same on every machine for the same script and inputs: the block's high mark, the bytes in
   use, and the allocations and frees so far. */
void G64LuaCounts(unsigned int out[4]) {
    out[0] = (unsigned int)arenaTop;
    out[1] = luaLive;
    out[2] = luaAllocs;
    out[3] = luaFrees;
}

/* A new, empty state on the block, for the code that loads a script. NULL when the block has no room. */
lua_State *G64LuaNewState(void) {
    G64LuaReset();
    luaBudget = LOAD_BUDGET;
    luaState = lua_newstate(lua_alloc, NULL);
    return luaState;
}

lua_State *G64LuaState(void) {
    return luaState;
}

/* ---- The power operator */

double g64_lua_pow(lua_State *L, double a, double b) {
    double r = 1.0, x = a;
    long long n;
    if (b == 0.5) {
        return sqrt(a);
    }
    /* A whole exponent of a moderate size: by squaring. Anything else has no exact answer that every system's C
       library agrees on, and is an error in a script. */
    if (!(b >= -1024.0 && b <= 1024.0) || b != (double)(long long)b) {
        if (L != NULL) {
            luaL_error(L, "the power operator takes a whole exponent from -1024 to 1024, or 0.5");
        }
        return 0.0;
    }
    n = (long long)b;
    if (n < 0) {
        n = -n;
        x = 1.0 / x;
    }
    while (n > 0) {
        if (n & 1) {
            r *= x;
        }
        x *= x;
        n >>= 1;
    }
    return r;
}

/* ---- What a script may call */

int G64PlayerPosition(int p, float *x, float *y, float *z);
void G64PlacePlayer(int p, float x, float y, float z);
int G64HasPlayer(int p);
int G64FramesRun(void);
int G64Level(void);
void port_log(const char *fmt, ...);
void port_dma(void *dst, unsigned int devAddr, unsigned int size);

/* A player's number from a script: 0 to 15. */
static int player_arg(lua_State *L, int i) {
    lua_Integer p = luaL_checkinteger(L, i);
    luaL_argcheck(L, p >= 0 && p < 16, i, "a player's number is 0 to 15");
    return (int)p;
}

/* A coordinate from a script: a finite number the level can hold. */
static float coord_arg(lua_State *L, int i) {
    lua_Number v = luaL_checknumber(L, i);
    luaL_argcheck(L, v >= -100000.0 && v <= 100000.0, i, "a coordinate is from -100000 to 100000");
    return (float)v;
}

static int api_frame(lua_State *L) {
    lua_pushinteger(L, G64FramesRun());
    return 1;
}

static int api_level(lua_State *L) {
    lua_pushinteger(L, G64Level());
    return 1;
}

static int api_has_player(lua_State *L) {
    lua_pushboolean(L, G64HasPlayer(player_arg(L, 1)));
    return 1;
}

static int api_pos(lua_State *L) {
    float x = 0, y = 0, z = 0;
    if (!G64PlayerPosition(player_arg(L, 1), &x, &y, &z)) {
        return 0;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int api_place(lua_State *L) {
    int p = player_arg(L, 1);
    float x = coord_arg(L, 2), y = coord_arg(L, 3), z = coord_arg(L, 4);
    if (G64HasPlayer(p)) {
        G64PlacePlayer(p, x, y, z);
    }
    return 0;
}

static int api_log(lua_State *L);

static const luaL_Reg api[] = {{"frame", api_frame}, {"level", api_level}, {"has_player", api_has_player},
                               {"pos", api_pos},     {"place", api_place}, {"log", api_log},
                               {NULL, NULL}};

/* ---- The sandbox */

/* What a script must not reach: files, other code, the collector's controls, and anything whose result depends on
   the machine (the C library's transcendental functions, random numbers, the text of an address). */
static void remove_field(lua_State *L, const char *table, const char *name) {
    if (table != NULL) {
        lua_getglobal(L, table);
    } else {
        lua_pushglobaltable(L);
    }
    lua_pushnil(L);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

static void count_hook(lua_State *L, lua_Debug *ar);

/* The work is used up. From here the hook runs at every step of Lua and raises an error each time, and nothing
   new is allocated. A script that catches the error with pcall gets it again at its next step, so the call ends. */
static void spent(void) {
    luaBudget = 0;
    if (luaState != NULL) {
        lua_sethook(luaState, count_hook, LUA_MASKCOUNT | LUA_MASKCALL, 1);
    }
}

static void count_hook(lua_State *L, lua_Debug *ar) {
    /* A call costs CALL_COST units, whether the function is Lua or C. Some calls do much more than one step of Lua:
       a pcall whose function fails builds an error text each time. */
    int cost = ar->event == LUA_HOOKCOUNT ? HOOK_EVERY : CALL_COST;
    if (luaBudget > cost) {
        luaBudget -= cost;
        return;
    }
    spent();
    luaL_error(L, "the script ran too long");
}

/* Work that a function of this file does outside Lua's steps. */
static void spend(lua_State *L, unsigned long long cost) {
    if (luaBudget <= 0 || cost >= (unsigned long long)luaBudget) {
        spent();
        luaL_error(L, "the script ran too long");
    }
    luaBudget -= (long long)cost;
}

/* A text for the log from a script: its printable characters, cut to one line. */
static void log_text(const char *what, const char *s, size_t n) {
    char line[200];
    size_t i;
    for (i = 0; i < n && i + 1 < sizeof line; i++) {
        line[i] = s[i] >= 32 && s[i] < 127 ? s[i] : '?';
    }
    line[i] = 0;
    port_log("script: %s%s", what, line);
}

/* A line for the program's log. It is for the eyes only: nothing in the game reads it. */
static int api_log(lua_State *L) {
    size_t n = 0;
    const char *s = luaL_checklstring(L, 1, &n);
    log_text("", s, n);
    return 0;
}

/* Calls the wrapped function of Lua's library (this function's first upvalue) with the arguments on the stack. */
static int call_wrapped(lua_State *L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

/* string.format without the conversions whose text differs between machines: %p gives an address, %a and %q
   write a number in a form that the Windows and Linux C libraries spell differently, and %s of a table or a
   function gives its address. */
static int safe_format(lua_State *L) {
    size_t n = 0, i;
    const char *f = luaL_checklstring(L, 1, &n);
    int arg = 1;
    for (i = 0; i < n; i++) {
        if (f[i] != '%') {
            continue;
        }
        i++;
        if (i < n && f[i] == '%') {
            continue;
        }
        while (i < n && f[i] != 0 && strchr("-+ #0123456789.", f[i]) != NULL) {
            i++;
        }
        if (i >= n) {
            break;
        }
        arg++;
        if (f[i] == 'p' || f[i] == 'a' || f[i] == 'A' || f[i] == 'q') {
            return luaL_error(L, "string.format has no %%%c here", f[i]);
        }
        if (f[i] == 's' && lua_type(L, arg) != LUA_TSTRING && lua_type(L, arg) != LUA_TNUMBER) {
            return luaL_argerror(L, arg, "%s takes a text or a number");
        }
    }
    return call_wrapped(L);
}

/* string.find for plain text only. Lua's patterns can take a time that grows without limit in one call, which the
   count of steps does not see. The search counts as work. */
static int safe_find(lua_State *L) {
    size_t ls = 0, lp = 0;
    lua_Integer init = luaL_optinteger(L, 3, 1);
    luaL_checklstring(L, 1, &ls);
    luaL_checklstring(L, 2, &lp);
    spend(L, (unsigned long long)ls * (1 + lp / 64));
    lua_settop(L, 2);
    lua_pushinteger(L, init);
    lua_pushboolean(L, 1);
    return call_wrapped(L);
}

/* string.rep. Lua's own loops once for each copy even when the text and the separator are both empty. */
static int safe_rep(lua_State *L) {
    size_t l = 0, lsep = 0;
    luaL_checklstring(L, 1, &l);
    luaL_checkinteger(L, 2);
    luaL_optlstring(L, 3, "", &lsep);
    if (l + lsep == 0) {
        lua_pushliteral(L, "");
        return 1;
    }
    return call_wrapped(L);
}

/* The table library for plain tables only. With a metatable, a table can claim any length and answer every read
   from a C function, and Lua's own then loops that many times in one call, which the count of steps does not see. */
static int safe_table(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_getmetatable(L, 1)) {
        return luaL_argerror(L, 1, "a table with a metatable cannot be used here");
    }
    return call_wrapped(L);
}

/* setmetatable without __gc. Lua runs such a function when it frees the object, with the count of steps off. */
static int safe_setmetatable(lua_State *L) {
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_pushliteral(L, "__gc");
        if (lua_rawget(L, 2) != LUA_TNIL) {
            return luaL_argerror(L, 2, "a metatable cannot have __gc");
        }
        lua_pop(L, 1);
    }
    return call_wrapped(L);
}

/* Puts a function of this file around table.name (the global table when table is NULL). */
static void wrap_field(lua_State *L, const char *table, const char *name, lua_CFunction with) {
    if (table != NULL) {
        lua_getglobal(L, table);
    } else {
        lua_pushglobaltable(L);
    }
    lua_getfield(L, -1, name);
    lua_pushcclosure(L, with, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

/* Calls the function on the stack with no arguments, with the given work to spend. An error ends the scripts for
   this game: on every machine at the same point, since they all run the same script on the same game. So does a
   call that used up its work, also when the script caught that error itself. */
static int call_script(lua_State *L, const char *what, long long budget) {
    int failed;
    luaBudget = budget;
    failed = lua_pcall(L, 0, 0, 0) != LUA_OK;
    if (luaBudget <= 0) {
        port_log("script: %s failed: the script ran too long", what);
    } else if (failed) {
        size_t n = 0;
        const char *why = lua_tolstring(L, -1, &n);
        char head[64];
        snprintf(head, sizeof head, "%s failed: ", what);
        if (why == NULL) {
            why = "(no message)";
            n = strlen(why);
        }
        log_text(head, why, n);
    } else {
        return 0;
    }
    G64LuaReset();
    return -1;
}

/* The script's source for load_script. */
static const char *loadSource;
static unsigned int loadSize;

/* Builds the sandbox, then compiles and runs the script's text. It runs under call_script: an error anywhere in
   it, a memory error too, ends there. */
static int load_script(lua_State *L) {
    static const char *const base_out[] = {"collectgarbage", "dofile", "load",  "loadfile",
                                           "print",          "tostring", "warn", NULL};
    static const char *const math_out[] = {"random", "randomseed", "sin",  "cos", "tan", "asin",
                                           "acos",   "atan",       "exp",  "log", NULL};
    /* dump gives compiled code. pack, unpack and packsize depend on the sizes of the machine's C types. match,
       gmatch and gsub take patterns (safe_find). */
    static const char *const string_out[] = {"dump", "pack", "unpack", "packsize", "match", "gmatch", "gsub", NULL};
    int i;
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 4);
    for (i = 0; base_out[i] != NULL; i++) {
        remove_field(L, NULL, base_out[i]);
    }
    for (i = 0; math_out[i] != NULL; i++) {
        remove_field(L, "math", math_out[i]);
    }
    for (i = 0; string_out[i] != NULL; i++) {
        remove_field(L, "string", string_out[i]);
    }
    /* table.move walks a range of any size in one call. */
    remove_field(L, "table", "move");
    wrap_field(L, "string", "format", safe_format);
    wrap_field(L, "string", "find", safe_find);
    wrap_field(L, "string", "rep", safe_rep);
    {
        static const char *const plain[] = {"insert", "remove", "sort", "concat", "unpack", NULL};
        for (i = 0; plain[i] != NULL; i++) {
            wrap_field(L, "table", plain[i], safe_table);
        }
    }
    wrap_field(L, NULL, "setmetatable", safe_setmetatable);
    luaL_newlib(L, api);
    lua_setglobal(L, "game");
    /* Source only: a script's compiled form is never taken. */
    if (luaL_loadbufferx(L, loadSource, loadSize, "script", "t") != LUA_OK) {
        return lua_error(L);
    }
    lua_call(L, 0, 0);
    return 0;
}

/* Loads and runs a script's source. Returns 0, or -1 when it does not load or its first run fails. */
int G64LuaLoad(const char *source, unsigned int size) {
    lua_State *L;
    int r;
    luaBudget = LOAD_BUDGET;
    L = G64LuaNewState();
    if (L == NULL) {
        G64LuaReset();
        return -1;
    }
    lua_sethook(L, count_hook, LUA_MASKCOUNT | LUA_MASKCALL, HOOK_EVERY);
    loadSource = source;
    loadSize = size;
    lua_pushcfunction(L, load_script);
    r = call_script(L, "the script's start", LOAD_BUDGET);
    loadSource = NULL;
    loadSize = 0;
    return r;
}

/* Calls the script's on_frame function, if it has one. The name is looked up without metamethods, and under
   call_script, so a script cannot run code outside the count of steps. */
static int frame_call(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushliteral(L, "on_frame");
    if (lua_rawget(L, -2) == LUA_TFUNCTION) {
        lua_call(L, 0, 0);
    }
    return 0;
}

/* Once a frame, before the game's own step. */
void G64LuaFrame(void) {
    lua_State *L = luaState;
    if (L == NULL) {
        return;
    }
    lua_pushcfunction(L, frame_call);
    call_script(L, "on_frame", CALL_BUDGET);
}

/* ---- A level pack's script */

/* Where the pack's script is in the ROM buffer, after the ROM's own bytes, and its size (0: none). The client sets
   them the same on every machine (G64SetPackScript). */
static unsigned int packScriptRom, packScriptSize;

void G64SetPackScript(unsigned int rom, unsigned int size) {
    packScriptRom = size ? rom : 0;
    packScriptSize = size;
}

/* At the start of a game: the pack's script, if the pack has one. */
void G64LuaStart(void) {
    char *source;
    G64LuaReset();
    if (packScriptSize == 0 || packScriptSize > (256u << 10)) {
        return;
    }
    source = malloc(packScriptSize);
    if (source == NULL) {
        return;
    }
    port_dma(source, packScriptRom, packScriptSize);
    G64LuaLoad(source, packScriptSize);
    free(source);
}
