# Lua

The script language of g64coopdx's mods (src/port/g64lua.c; docs/discovery/lua-trial.md in the repository around
this copy of weemitt).

- Source: https://www.lua.org/ftp/lua-5.4.8.tar.gz
- Version: 5.4.8
- SHA-256 of the archive, as lua.org's list gives it:
  `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`
- Files taken from its `src/`: the headers, the core (`lapi.c` to `lzio.c`), the auxiliary library (`lauxlib.c`) and
  four libraries (`lbaselib.c`, `ltablib.c`, `lstrlib.c`, `lmathlib.c`). Not taken: the programs (`lua.c`, `luac.c`)
  and the libraries that reach the system (`liolib.c`, `loslib.c`, `loadlib.c`, `ldblib.c`, `lcorolib.c`,
  `lutf8lib.c`, `linit.c`).
- Two changes. The second: in `luaconf.h`, `G64_LUA_OWN_STRX2NUMBER` keeps Lua from reading a hexadecimal number
  with the system's `strtod`. Lua's own reader then does it on every system, as it already did on Windows.
- The first: in `llimits.h`, a guard around `point2uint`, which turns an address into a hash. The build gives its
  own (`src/port/g64lua_conf.h`): the address's place in the scripts' block of memory. Every machine then walks a
  table with table or function keys in the same order.
- License: MIT. The text is at the end of `lua.h`.

To update: download the new release, check its checksum against lua.org's list, replace the files, put the two
changes back, and run the game's checks.
