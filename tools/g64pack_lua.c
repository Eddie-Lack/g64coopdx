/* Lua's parser for g64pack: the pack tool compiles a pack's script as the game does, and says where its text is
 * wrong. It's Lua's core and its auxiliary library in one file, built with the game's options (g64lua_conf.h, given
 * with -include). The tool never runs a script. */
#define LUA_CORE
#include "../weemitt/third_party/lua/lprefix.h"
#include "../weemitt/third_party/lua/lapi.c"
#include "../weemitt/third_party/lua/lcode.c"
#include "../weemitt/third_party/lua/lctype.c"
#include "../weemitt/third_party/lua/ldebug.c"
#include "../weemitt/third_party/lua/ldo.c"
#include "../weemitt/third_party/lua/ldump.c"
#include "../weemitt/third_party/lua/lfunc.c"
#include "../weemitt/third_party/lua/lgc.c"
#include "../weemitt/third_party/lua/llex.c"
#include "../weemitt/third_party/lua/lmem.c"
#include "../weemitt/third_party/lua/lobject.c"
#include "../weemitt/third_party/lua/lopcodes.c"
#include "../weemitt/third_party/lua/lparser.c"
#include "../weemitt/third_party/lua/lstate.c"
#include "../weemitt/third_party/lua/lstring.c"
#include "../weemitt/third_party/lua/ltable.c"
#include "../weemitt/third_party/lua/ltm.c"
#include "../weemitt/third_party/lua/lundump.c"
#include "../weemitt/third_party/lua/lvm.c"
#include "../weemitt/third_party/lua/lzio.c"
#include "../weemitt/third_party/lua/lauxlib.c"

#include <string.h>

/* The game's block for scripts (g64lua_conf.h hashes addresses by their place in it). The tool runs nothing in it. */
unsigned char g64LuaArena[1];

/* The game's rule for the power operator (g64_lua_pow in weemitt/src/port/g64lua.c): a whole exponent from -1024 to
 * 1024, or 0.5. The parser works out a power of two plain numbers while it reads the text, so a script that breaks
 * the rule there doesn't compile. Only the rule matters here, not the value. */
double g64_lua_pow(lua_State *L, double a, double b) {
    (void)a;
    if (b != 0.5 && (!(b >= -1024.0 && b <= 1024.0) || b != (double)(long long)b) && L != NULL)
        luaL_error(L, "the power operator takes a whole exponent from -1024 to 1024, or 0.5");
    return 0.0;
}

/* Compiles a script's text as the game loads it (text only). Returns NULL, or Lua's message in msg. */
const char *g64pack_compile(const char *text, size_t size, char *msg, size_t msgSize) {
    lua_State *L = luaL_newstate();
    const char *why = NULL;
    if (L == NULL) {
        snprintf(msg, msgSize, "no memory");
        return msg;
    }
    if (luaL_loadbufferx(L, text, size, "=script.lua", "t") != LUA_OK) {
        const char *m = lua_tostring(L, -1);
        snprintf(msg, msgSize, "%s", m ? m : "?");
        why = msg;
    }
    lua_close(L);
    return why;
}
