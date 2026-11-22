/* g64coopdx: scripts. One Lua state for the game, on a block of memory that is part of the game's own data: a
   savestate holds the block, and a rollback takes the scripts back with the game. The block holds addresses, so its
   bytes differ between machines. The state hash that machines compare covers the game's memory (port_ram) and not
   this block; G64LuaCounts gives numbers that are the same on every machine for the same script and inputs
   (docs/discovery/lua-trial.md in the repository around this copy of weemitt).

   This file is C, as Lua is, and is built with Lua's options (g64lua_conf.h). */
#include <math.h>
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

static void *arena_alloc(size_t n) {
    Block **p, *b;
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
    if (n > G64_LUA_ARENA || arenaTop + HEAD + n > G64_LUA_ARENA) {
        return NULL;
    }
    b = (Block *)(g64LuaArena + arenaTop);
    b->size = n;
    arenaTop += HEAD + n;
    return (unsigned char *)b + HEAD;
}

static void arena_free(void *q) {
    Block *b = (Block *)((unsigned char *)q - HEAD), **p;
    for (p = &arenaFree; *p != NULL && *p < b; p = &(*p)->next) {
    }
    b->next = *p;
    *p = b;
}

/* Lua's allocator (lua_Alloc). A request the block cannot meet gives NULL, and Lua ends the script's call with a
   memory error: at the same point on every machine. */
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

/* A line for the program's log. It is for the eyes only: nothing in the game reads it. */
static int api_log(lua_State *L) {
    size_t n = 0;
    const char *s = luaL_checklstring(L, 1, &n);
    char line[200];
    size_t i;
    for (i = 0; i < n && i + 1 < sizeof line; i++) {
        line[i] = s[i] >= 32 && s[i] < 127 ? s[i] : '?';
    }
    line[i] = 0;
    port_log("script: %s", line);
    return 0;
}

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

/* The work one call into a script may do, in steps of the count hook. A script that takes more is ended. */
#define HOOK_EVERY 1000
#define CALL_BUDGET 2000
static int luaBudget;

static void count_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    if (--luaBudget <= 0) {
        luaL_error(L, "the script ran too long");
    }
}

/* Calls the function on the stack with no arguments. An error ends the scripts for this game: on every machine
   at the same point, since they all run the same script on the same game. */
static int call_script(lua_State *L, const char *what) {
    luaBudget = CALL_BUDGET;
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char *why = lua_tostring(L, -1);
        port_log("script: %s failed: %s", what, why != NULL ? why : "(no message)");
        G64LuaReset();
        return -1;
    }
    return 0;
}

/* Loads and runs a script's source. Returns 0, or -1 when it does not load or its first run fails. */
int G64LuaLoad(const char *source, unsigned int size) {
    static const char *const base_out[] = {"collectgarbage", "dofile", "load", "loadfile", "print", "tostring", NULL};
    static const char *const math_out[] = {"random", "randomseed", "sin",  "cos", "tan", "asin",
                                           "acos",   "atan",       "exp",  "log", NULL};
    lua_State *L = G64LuaNewState();
    int i;
    if (L == NULL) {
        return -1;
    }
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
    remove_field(L, "string", "dump");
    luaL_newlib(L, api);
    lua_setglobal(L, "game");
    lua_sethook(L, count_hook, LUA_MASKCOUNT, HOOK_EVERY);
    /* Source only: a script's compiled form is never taken. */
    if (luaL_loadbufferx(L, source, size, "script", "t") != LUA_OK) {
        const char *why = lua_tostring(L, -1);
        port_log("script: it does not load: %s", why != NULL ? why : "(no message)");
        G64LuaReset();
        return -1;
    }
    return call_script(L, "the script's start");
}

/* Once a frame, before the game's own step: the script's on_frame function, if it has one. */
void G64LuaFrame(void) {
    lua_State *L = luaState;
    if (L == NULL) {
        return;
    }
    if (lua_getglobal(L, "on_frame") != LUA_TFUNCTION) {
        lua_pop(L, 1);
        return;
    }
    call_script(L, "on_frame");
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
