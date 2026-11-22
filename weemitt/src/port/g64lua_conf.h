/* g64coopdx: the options Lua is built with (third_party/lua), given before each of its files. Every machine of a
   session must run scripts to the same result, so nothing in Lua may depend on the time, on an address, or on the
   system's own C library where the program can bring its own. */
#ifndef G64LUA_CONF_H
#define G64LUA_CONF_H

/* MinGW's own printf and strtod, which are in the program, in place of the Windows C library's: a number becomes
   text, and text a number, the same way on every Windows machine. */
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1

/* Lua's power operator calls the C library's pow, whose last bit may differ between systems. The game's own
   (g64lua.c) uses only multiplication, division and the square root, which are exact everywhere. */
struct lua_State;
double g64_lua_pow(struct lua_State *L, double a, double b);
#define luai_numpow(L, a, b) g64_lua_pow(L, a, b)

#endif

#include <stddef.h>

/* The seed of Lua's string hash. Lua takes it from the time and from addresses. A constant gives every machine the
   same order when a script walks a table. */
#define luai_makeseed(L) 0x9E3779B9u

/* The scripts' block of memory (g64lua.c). It is in the game's own data, so a savestate holds it. */
#define G64_LUA_ARENA (2u << 20)
extern unsigned char g64LuaArena[];

/* A table, a function or another object as a table's key is hashed by its address. Its place in the block is the
   same on every machine; its address is not. An address outside the block (a C function) hashes to 0. */
#define point2uint(p) \
    ((size_t)((const unsigned char *)(p) - g64LuaArena) < G64_LUA_ARENA \
         ? (unsigned int)((const unsigned char *)(p) - g64LuaArena) \
         : 0u)

#endif
