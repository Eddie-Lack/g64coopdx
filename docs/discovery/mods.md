# Mods

This file covers question 8: a scripting layer, custom levels from Glovebox, custom models and textures, and how mods
reach other players without anyone sharing game data. The owner wants Lua and custom levels in the first version.

Paths start with the repository: `weemitt/`, `gloveworld/`, `Glovebox/`. **VERIFIED** means read in the code or docs.
**INFERRED** means concluded from them. **UNKNOWN** means not found or not tried.

## Summary

- A Glover level is one command stream (a `.lev` file) that names models and textures by a CRC id. The models and
  textures live in banks in the ROM. A level that only places existing objects needs no new bank (VERIFIED).
- weemitt reads every ROM byte through a few functions over one memory buffer. Loading a level or a bank from a file
  at run time is a small change: about 30 lines for the level, a little more for banks (INFERRED).
- Glovebox already writes levels and models in the game's own formats. It needs one new export, a **level pack**: a
  zip file of one level, its scripts and the author's own models, with no game data ([details](#the-level-pack)). Its imported models must then go into banks of their own, which its
  `modCustom` path already supports (VERIFIED: `Glovebox/docs/technical.md:907-911`).
- Lua: vendor PUC Lua 5.4 (MIT), compile it with the game's float flags, and give it a fixed memory arena inside the
  game's state. Then every rollback savestate holds the scripts' state too, at no extra cost per frame beyond the
  pages the scripts write.
- Mods reach other players as packs, by hash, from the host. Every player supplies the ROM. Nothing from the ROM goes
  over the network.

## What a level is

| Piece | What it is | Where |
| --- | --- | --- |
| Level file (`.lev`) | A big-endian stream: a length, a name, then 16-bit tokens with fields until token 0x7D00. It places actors, platforms, enemies, puzzles, garibs, power-ups, water, wind, the backdrop, the spawn points and the cameo | `gloveworld/src/parse.c:92-170` reads it |
| Object banks | Models, named by the CRC of their file name. FLA2-compressed. 56 banks in the ROM | `gloveworld/src/banks.c:493-760`; `Glovebox/crates/core/data/rom_map.json` |
| Texture banks | Textures by CRC, 19 banks plus one static | `gloveworld/src/banks.c:132-290` |
| Collision | Not stored. `InitPlanes` builds it from each landscape mesh at load | `gloveworld/src/parse.c:262-283` |
| Level table | `levelInfo[]`, 0x38 bytes a level: name, object bank, sound distance, song, bonus clock | `gloveworld/include/game.h:45-53` |

The game keeps 5 object bank slots and 5 texture bank slots (`weemitt/include/banks.h:13,27`, VERIFIED).
`FindObject` and `FindTexture` search only those. A level normally uses about 3 (`Glovebox/docs/technical.md`).

The load runs `StartNewLevel`, `LoadLevel` (`weemitt/src/game/game.c:808`), `InitMorePersonal` (`game.c:444`),
`ParseLandscapeFile` and `LoadLandscapeFile`. weemitt converts banks as they load (`weemitt/src/port/loaders.c`) and
parses the `.lev` from its big-endian bytes field by field (`PORT_COPY`).

## Custom levels at run time

### Where ROM bytes come from (VERIFIED)

- The ROM is one buffer that `load_rom` reads (`weemitt/src/port/platform.c:124-152`); the library copies the host's
  ROM the same way (`weemitt/src/lib/glover.c:218`).
- Reads go through `port_dma` (`weemitt/src/port/os.c:115-126`, a `memcpy` from `port_rom`), `osPiRawReadIo`, the
  sound player's reads, the main segment's load and the symbol reader (`loaders.c:21`, `loaders.c:93-94`).
- The game never writes the ROM.
- weemitt finds each bank's ROM address in the ROM's own code (`port_set_rom_symbols`, `loaders.c:87-100`). A ROM
  that Glovebox repacked therefore already works in weemitt.

### Options, cheapest first (INFERRED)

| Option | What changes | Cost | Limits |
| --- | --- | --- | --- |
| A. Level from a buffer | `LoadLandscapeFile` (`loaders.c:213-227`) returns the custom `.lev` when the level being loaded is the custom level (option C says which number that is) | about 30 lines | Uses the shipped banks only |
| B. Banks from files | `LoadObjectBank` and `LoadTextureBank` (switches in `weemitt/src/game/banks.c`) take bank numbers that the ROM does not use from a buffer; `DMAMemory` and `convert()` handle the rest | about 100 lines | Extra bank slots must live outside `port_ram` (weemitt's layout rule): new lists that `FindObject` and `FindTexture` also search |
| C. Custom level slots | A custom level borrows a shipped level's number: its row in `levelInfo` (the game's table of levels: name, song, banks) and its world. It swaps in its own `.lev` and extra banks. Glovebox's console mod (next section) borrows Space levels' numbers the same way (`Glovebox/mod/slots.c`) | small | Keeps `NUM_LEVELS` and the save layout unchanged |
| D. Rebuild the ROM at start | Apply a Glovebox BPS patch (a binary diff against the retail ROM, which Glovebox exports) to the player's ROM in memory | no weemitt change | Loses the console mod's features (its MIPS code does not run in weemitt); one ROM per level is the wrong shape for a session that changes levels |

Recommendation: A, B and C together. They keep the game's own loader, drawing, puzzles, enemies and platforms.

### The Glovebox console mod's features on the PC

Glovebox's console mod (about 13.5 KB of MIPS code, `Glovebox/mod/`) lifts limits of the retail game. weemitt does
not run MIPS code, so each feature needs C in weemitt's game code, as part of upstream item 8 (VERIFIED from
`Glovebox/docs/technical.md`):

| Feature | On the PC |
| --- | --- |
| Rotation and scale on every object type (`mod/transforms.c`) | Needs porting: weemitt's parser applies `TOKEN_LANDROT` and `TOKEN_LANDSCALE` to the last landscape actor only (`weemitt/src/game/parse.c:664-670`, VERIFIED). Only levels that use the new case behave differently |
| The heap above 4 MB (`mod/heap.c`) | Not needed: weemitt's heap runs to 16 MB past RDRAM (`weemitt/docs/architecture.md`) |
| More bank slots (`mod/banks.c`) | Option B |
| The matrix guard (`mod/guard.c`) | Needed: the 256-matrix limit is in the game's C. With 8 or more gloves it is a real limit: see [state-inventory.md](state-inventory.md#the-frames-hard-limits) |
| Extra level slots (`mod/slots.c`) | Option C |

### The host world and custom levels

weemitt's host world (`weemitt/docs/host-world.md`) lets a host give the game triangles to collide with, moving
bodies, water and a camera. It does not draw them (`host-world.md:340`). It suits a foreign engine that uses Glover
as a character. A custom Glover level is better served by the game's own loader.

## Custom models and textures

- A model is an `ObjectInstance` in a bank, found by CRC. A new model needs a bank: a single-object bank, loaded
  with option B.
- Glovebox already makes such banks: `custom/<set>/<id>.obj.bin` and `textures.tex.bin`
  (`Glovebox/docs/technical.md:910-922`). They are in the game's own format, so weemitt's converters
  (`port_convert_object_bank`, `port_convert_texture_bank`) should take them as they are (INFERRED, not tried).
- weemitt's renderer interprets F3DEX display lists and textures from any address in `port_ram`
  (`weemitt/src/gfx/gfx.h`). The limits of the game's C stay: 800 vertices a frame in `objectsVtx`
  (`gloveworld/docs/game.md:117`), 2,048 texels per texture load.
- UNKNOWN: whether a custom model with an all-zero first rotation key, used as a platform, gives NaN in weemitt's
  `QuaternionToMatrix`. Glovebox's builder does not allow custom models as platforms on the console. Test it before allowing it.

### Player models

A custom glove or ball for one player is a model bank plus a swap of the model pointer of that player's actor while
it draws. The game draws actors from their `Object` pointers, so this needs no change to the logic. Models only change
the picture, so a player's look can be local: other players need the bank to see it, but the game stays the same.

## Game data and the author's work

| Output of Glovebox | Holds game data? | Shareable in g64coopdx? |
| --- | --- | --- |
| Built ROM | Yes, all of it | No |
| BPS patch of a level that places existing objects | Only the changed level bytes, and the console mod's code | Not needed on the PC; packs replace it |
| BPS patch with imported models | Yes: the level's object bank and the world's texture bank are rewritten whole (a 4.9 KB model gave a 165 KB patch, `technical.md:322-326`) | No |
| Level XML of a level built from scratch | No: object ids, positions and scripts | Yes (INFERRED) |
| Level XML of an edited shipped level | The unchanged parts are the original team's level design | Only as a diff against the shipped level, which the player's own ROM rebuilds (decided) |
| Custom model set (`custom/<set>/`) | No: the packer builds the banks from the author's triangles and images | Yes, if the art is the author's |
| Kit models | No (`technical.md:603-610`) | Yes |

## The level pack

A level pack is a zip file or a folder:

| File | What it holds |
| --- | --- |
| `pack.json` | name, author, version, the pack format version, the Lua API version, the base level whose world and row it reuses, the object and texture ids it uses, the banks it needs, and the SHA-1 of the retail ROM |
| `level.lev` or `level.xml` | the level. For an edited shipped level: a diff against the shipped level's XML, which the player's own ROM rebuilds |
| `custom/<set>/` | the author's model banks |
| `scripts/*.lua` | the level's scripts |

The pack's id is the SHA-256 of a canonical form of its files.

What each project needs:

- Glovebox: an **Export level pack** action beside Export patch, and imported models in banks of their own (its
  `modCustom` path) in place of copies of the level's banks. Its checks still apply, except the console's hardware
  rules.
- weemitt or g64coopdx: options A to C, a check of the pack's ids against the player's ROM with a plain error, and the
  rotation and scale rule.

## The Lua API

### Runtime (VERIFIED licenses; the choice is INFERRED)

| | PUC Lua 5.4 | LuaJIT 2.1 | Luau |
| --- | --- | --- | --- |
| License | MIT (lua.org/license.html) | MIT (luajit.org/luajit.html) | MIT (luau-lang/luau LICENSE.txt) |
| Language | 5.4: integers and doubles | 5.1, doubles only | 5.1 with types |
| 32-bit Windows (MinGW) | plain C99 | needs SSE2, harder to build | C++17, interpreter only on 32-bit |
| Determinism | portable C; the basic float operations are exact with SSE2 and no fused multiply-add | the JIT and the interpreter can take different paths; its bytecode "is not deterministic" (luajit.org/extensions.html) | interpreter is portable |
| Sandbox | by hand | by hand; its FFI escapes any sandbox | built in |
| Memory and step limits | custom allocator; `lua_sethook` with a count | the count hook does not reach compiled loops | interrupt callback |

**Recommendation: PUC Lua 5.4, vendored at a fixed version**, compiled with the game's flags (`-ffp-contract=off`,
SSE2, no fast math). sm64coopdx uses Lua 5.3 the same way ([sm64coopdx.md](sm64coopdx.md)). Choose Luau only if the
sandbox matters more than the C dependency.

### Scripts in a rollback game

The network model ([netcode.md](netcode.md)) is rollback: every machine runs the same game and the same scripts on
the same inputs, and steps back and forth through savestates. Three rules follow.

1. **The scripts' state must be in the savestate.** Give Lua an allocator over a fixed arena, and link Lua and the
   arena into the game's object (`game.o`) of the brute forcer's engine. The engine saves all of the game's
   variables outside `port_ram` (`weemitt/bruteforce/docs/engine.md`, "Savestates"). The arena is one of those variables. The
   page tracker copies only the pages a script wrote. The arena holds pointers into the machine's own memory, so
   such a state loads only in the same process. That is enough for rollback; late join uses the inputs ([netcode.md](netcode.md#instances)).
2. **Scripts must be deterministic.** Every machine must reach the same bits.
3. **Scripts run per instance.** Players may be in different levels, and each occupied level is its own simulation
   ([netcode.md](netcode.md#instances)). A script's state in one instance is not seen by another. State that a mod
   keeps across levels (a score for the whole session, say) lives in the session and changes only through session
   events, which the host puts into each instance's confirmed inputs ([netcode.md](netcode.md#session-events)).

| Breaks determinism | Fix |
| --- | --- |
| `math.sin`, `cos`, `exp`, `log`, `pow`, `fmod` (the C library's; they differ between systems) | Replace with the game's own `sinf` and `cosf` (`weemitt/src/port/libultra.c`, bit-exact polynomials) and functions made only of `+ - * /` |
| `os.time`, `os.clock`, `os.date` | Remove; give `game.frame()` |
| `math.random` | Replace with the session's seeded generator; never the game's `rseed` unless the script means to change the game |
| `pairs` and `next` order over hash tables: Lua 5.4 seeds its string hash from the time and from addresses (`lstate.c:58-80` in Lua 5.4.7, VERIFIED) | Define `luai_makeseed` as a constant when building Lua (the source allows it: `#if !defined(luai_makeseed)`), and forbid tables and functions as keys in logic, since their hash is their address |
| `tostring` of tables, `%p` | Remove |
| `__gc`, weak tables, `collectgarbage` | Remove; run the collector at a fixed point of the tick with a fixed step |
| Number to text in logic | Allowed only for display |
| Loading code (`load`, `dofile`, `require` of files) | Only from the pack, by hash |
| Bytecode | Never accepted; source only |
| Lua built with other options on another machine | One vendored Lua, one set of options, in the version handshake |

The memory hash used for desync checks then covers the arena too.

### Hook points

The port already has a hook pattern: function pointers in `weemitt/src/port/porthooks.h`, NULL by default
(VERIFIED). The Lua layer uses the same pattern. Each hook below exists as a function today (VERIFIED); the hook
itself is new.

| Event | Where |
| --- | --- |
| Before and after each tick | `port_pre_update_hook`, `port_post_update_hook` (`weemitt/src/port/frame.c`), only when `deltaTick` is 1 |
| Level loaded | after `ParseLandscapeFile` in `InitMorePersonal` (`weemitt/src/game/game.c:444`) |
| Level freed | `port_free_level_hook` (`game.c:574`) |
| Enemy spawned, killed | `LoadEnemy` (`weemitt/src/game/enemy.c:151`), `KillEnemy` (`enemy.c:1501`) |
| Glove action | `ApplyAction` (`weemitt/src/game/hand.c:679`) |
| Ball changed, hurt, burst | `ChangeBallInto` (`ball.c:1275`), `HurtBall` (`ball.c:202`), `BurstBall` (`ball.c:570`) |
| Garib collected | `GainGarib` (`weemitt/src/game/collect.c:860`) |
| Death, life lost | `KillHand` (`hand.c:3648`), `LoseLife` (`hand.c:3798`) |
| Exit | `CheckForExit` (`game.c:2546`), `ExitLevel` (`game.c:3716`) |
| Puzzle condition, puzzle action | `PuzzleEvaluateCondition` (`puzzle.c:857`), `PuzzleExecuteAction` (`puzzle.c:386`): a Lua condition or action can be a new puzzle type, which wires scripts into level data |
| Player joined, left | new, from the session layer (as tick-stamped inputs) |
| HUD drawn | new, in the overlay pass; display only, outside the logic |

### What the API exposes

- Read: the players (glove and ball positions, velocities, action, ball type, garibs, health), the level,
  the game mode, the actor lists, the puzzles' states.
- Write, in tick hooks only: positions with velocities cleared (as `glover_place` does), the players' counters,
  spawns through the game's own functions, puzzle actions through the puzzle system.
- Actors as handles (an index and a generation number), checked on every use. A raw pointer dies with the level's
  heap.
- Never: pointers, `Ptr32` words, display lists, the heap's block table, or a change of type or size of anything in
  `port_ram`.
- Players by their **session index**, the same on every machine. sm64coopdx's player indices differ between
  machines, which its own docs call a trap (`docs/lua/lua.md`, "Important notes on player indices").
- Script messages from a person (a vote, a chat command) enter the game as inputs stamped with a tick.
- Bindings and their docs generated from annotated C headers, as sm64coopdx does (`autogen/`).
- A Lua API version in `pack.json` and in the handshake.

## How mods reach other players

1. The host's lobby entry lists its packs by name and SHA-256.
2. A joiner compares the list with its cache (keyed by SHA-256).
3. The joiner shows the list and asks the player before anything downloads or runs. sm64coopdx enables downloaded
   mods with no prompt (`packet_download.c:187-203`, [sm64coopdx.md](sm64coopdx.md)).
4. The host sends the missing packs in chunks; the joiner checks each file's hash and the pack's hash.
5. Every file path is checked: relative, no `..`, no drive letters, no links, a length limit, an allowed character
   set, inside the pack's folder. A size limit per pack and per session.
6. Scripts load from source only, into the sandbox.
7. Every player's ROM must have the SHA-1 the pack names. The ROM itself never travels.

What never goes over the network: the ROM, banks or textures from it, memory dumps, savestates (they hold the
game's memory, which holds ROM data). See [netcode.md](netcode.md#instances) for why late join does not need them.

## Open points

- Whether Glovebox's custom banks load in weemitt unchanged: try in phase 5 of the [roadmap](roadmap.md).
- The custom-platform NaN question above.
- The Lua API's first scope is decided: rules, HUD, puzzle conditions and actions, and spawns through the game's own
  functions ([open-questions.md](open-questions.md#decided)).
