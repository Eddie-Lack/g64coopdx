# weemitt and g64coopdx

This file covers questions 2 and 3: where the PC port stands, what it already gives a network game, how g64coopdx
should relate to it, and what weemitt needs to add first.

weemitt was read at commit `f509ef7` (8 November 2026). Paths without a repository name are in weemitt. **VERIFIED** means read in the code or docs, or measured in this session. **INFERRED** means concluded
from them. Measurements were made on the session's machine: 4 cores, Linux, Wine 9 for the Windows builds.

## Where the port stands

### What works

The table is VERIFIED from weemitt's docs. The builds named in it were also made in this session.

| Part | State | Source |
| --- | --- | --- |
| The game | All 48 levels play. The game's C from gloveworld, changed in about 12,700 lines of 45,000 (measured: `diff` of `gloveworld/src` against `src/game`) | `docs/architecture.md` |
| Builds | Linux and Windows, 32-bit and 64-bit; headless test builds; the library for Linux, Windows, macOS and iOS. Built here: `headless`, `headless64`, `headless64-mus`, the 32-bit and 64-bit libraries and test hosts for Linux and Windows, the brute forcer | `docs/building.md`, `docs/library.md` |
| Memory | The game's data at its N64 offsets in `port_ram` (24 MB); pointers stored as 32-bit words, so 64-bit builds keep the layout | `docs/architecture.md`, `docs/port64.md` |
| Renderer | OpenGL 3.3, F3DEX and Sprite2D; resolution scale, anti-aliasing, widescreen | `docs/menu.md`, `docs/library.md` |
| Sound | libmus and libaudio's synthesizer translated from the ROM, bit-exact against the ROM's microcode; a silent stand-in for tests | `docs/audio.md`, `docs/testing.md` |
| Frame rate | Two modes. Smooth: the logic runs at any update length (delta time). Original: whole game frames with blended drawing | `docs/delta-time.md`, `docs/interpolation.md` |
| Exactness | At updates of one whole frame the logic is bit for bit the same as the baseline (`tools/regress.py`, 49 runs) | `docs/testing.md` |
| Against the N64 | 21,000 frames of the attract mode match an emulator bit for bit in the traced state, but for known timing effects of the N64's threads | `docs/n64-comparison.md` |
| Recording and replay | Every update's length and input; checks every 30 updates; a window run replays in the headless build line for line | `docs/testing.md` |
| The library | `libglover.so` and `glover.dll`: one C header, API version 4, the game one update at a time, input, state, memory (read only), sound, drawing | `docs/library.md` |
| The host world | A host's triangles, bodies, water, places and camera, through function pointers that are NULL by default | `docs/host-world.md` |
| The brute forcer and the studio | Cheap savestates (page tracking), a search, a timeline window; a sync test against the ROM in an emulator | `bruteforce/docs/`, `docs/studio.md` |
| Extras | An in-game menu (ImGui), Enhancements and Cheats, the Ballworld mode, the Space demo | `docs/menu.md` |

### What is unfinished (VERIFIED from the docs)

From `docs/port64.md`, "What is left", and `docs/library.md`, "What the library does not do yet":

1. Savestates in the library, a new game after `glover_destroy`, and more than one game in a process.
2. A pacer in the library.
3. Sound pulled by a host thread with a real lock.
4. Interpolated drawing in the library (the window program has it).
5. A Direct3D path.
6. The window program, the headless mode and the studio on the library; a static `libglover.a`.
7. The Developer and Cheats features in the library, the trace of each update, symbol lookup.
8. The Steam Runtime (the 64-bit library needs glibc 2.38's `fmod`).
9. The brute forcer on the shared ROM checks.

Known differences from the N64 (`docs/n64-comparison.md`, "What still differs"): the N64's thread timing in the
first frames of a level and in the second fly-through. These do not matter for a network game: what matters is
that every PC build agrees with every other, which the measurements below show.

## What the port gives a network game

| Property | What was found | Evidence |
| --- | --- | --- |
| **Bit-exact logic at one whole frame per update** | Two runs with the same input give the same trace and memory hash, frame by frame | Measured: all 48 levels for 6,000 frames and the attract mode, hashes every 10 frames ([experiments](#experiments)) |
| **The same across builds** | Linux 32-bit and Linux 64-bit: the same trace and memory hash. Linux 64-bit and Windows 64-bit: the same. Linux 32-bit and Windows 32-bit libraries: the same trace and hash. (The 32-bit Windows window program loads low, so its hash treats some numbers as pointers and is not comparable, `docs/testing.md`) | Measured ([experiments](#experiments)) |
| **No other source of change** | The game's only random source is `rseed`; its only clock feeds profiler timers | `docs/testing.md`, "Recording and replay" |
| Delta time | Not exact across systems: at updates shorter than a frame, helpers use the C library's `pow` and `exp`, which differ in the last bits between Linux and Windows | `docs/testing.md`; `docs/delta-time.md` |
| Interpolation | The Original mode runs whole frames and blends the drawing. This is the mode a network game uses: exact logic at 20 frames a second, smooth pictures at any rate | `docs/interpolation.md` |
| The memory hash | Covers all 24 MB; counts pointers as offsets, so a build that moved its code gives the same hash. It costs about 8 ms (measured: 2,000 frames took 0.11 s without it and 16.3 s with it every frame) | `src/port/statehash.c`; measured |
| The library | A separate program can drive the game. Two copies of the library file in one process run two independent games with equal hashes, on Linux and on Windows | Measured with `experiments/g64probe.c` |
| The host world | Proves the hook pattern: game changes behind function pointers, NULL by default, with the baseline unchanged | `src/port/porthooks.h`, `docs/host-world.md` |
| 64-bit builds | Hashes compare across Linux and Windows | Measured |
| **The brute forcer's savestates** | 4.7 to 5.0 MB a state. Save after a frame 0.014 to 0.019 ms; load 0.017 to 0.025 ms; a full copy about 0.4 ms. A game frame 0.028 to 0.047 ms | Measured with `glover bench`, levels 13, 5 and 1, and with `GLOVER_FULL_STATES=1` |

From these numbers (INFERRED): a rollback of 8 frames costs one load, 8 frames and 8 saves. That takes 0.4 to 0.6 ms
with page tracking and about 4 ms with full copies, against a game frame of 50 ms. Rollback is cheap enough to be the
network model ([netcode.md](netcode.md)).

## How g64coopdx relates to weemitt

### Where the multi-player change must live

Co-op with own gloves and own balls needs the game's logic to know about several players: enemies, puzzles, exits,
the camera and the HUD read the one `hand` and the one `ball` ([state-inventory.md](state-inventory.md)). This change
is inside the game's C, in the same files that weemitt changes all the time. In the 8 days from 1 to 8 November, 21
commits touched `src/game` (the Ballworld mode, the host world, the draw marks, the camera scale). Wherever the
multi-player change lives, it meets that stream.

### The options

| | A. A fork of weemitt kept in step | B. weemitt as a submodule or a vendored copy, co-op code beside it | C. A separate program on weemitt's library |
| --- | --- | --- | --- |
| Where the game change lives | In the fork's `src/game` | In patches over the submodule's `src/game`, or in the vendored copy | In weemitt, behind hooks and a player count, off by default |
| Where the network, UI, Lua, mods live | In the fork's new folders | In g64coopdx | In g64coopdx |
| How fixes flow | Merges from weemitt into the fork. Each touches the same functions as the co-op change | Rebase the patch series on each new weemitt; conflicts as in A | weemitt's fixes arrive with the next pinned version; co-op's game fixes go to weemitt as pull requests |
| weemitt's tests | Run in the fork, but the fork's game is no longer weemitt's: `regress.py`'s baseline must be re-recorded or kept at one player | Same as A | Run in weemitt on every change, with the player count at 1: the single-player game must stay bit for bit the baseline |
| What weemitt must add first | Nothing | Nothing | The player count, savestates in the library, input for several players, write access through calls ([below](#upstream-changes-weemitt-needs)) |
| Risk | High: sm64coopdx's history | High: patch rot | Medium: the library's interface must grow; one owner decides both sides |

### sm64coopdx's history as a case

sm64coopdx is a fork of sm64ex-coop, a fork of sm64ex, a fork of the decompilation's PC port
(`credits.txt:83` in sm64coopdx, VERIFIED). Its `CHANGES` file is the decompilation's log, frozen at "Refresh 13"
(VERIFIED). No sign of regular merges from upstream was found (INFERRED; the clone was shallow). The co-op changes
went into the game's files (`mario.c`, `interaction.c`, objects), and from then on the fork could not follow its
upstream. It kept its new systems in `src/pc/` (VERIFIED), which kept its own code readable, but did not keep the
game in step. A fork of weemitt would most likely end the same way, because weemitt's game code changes faster than
the decompilation's did.

### Recommendation: C with the brute forcer's way of building

g64coopdx is its own program. weemitt is a git submodule at a pinned commit. g64coopdx's build compiles weemitt's
game and runtime sources with weemitt's own flags, read from weemitt's Makefile, the way the brute forcer already
does (`bruteforce/Makefile`, VERIFIED). It links them statically, or uses `libglover` where that is enough.

- **Every change to the game's C goes to weemitt.** It sits behind a player count and hooks that change nothing
  when the count is 1. `regress.py` checks that the single-player game is unchanged. weemitt gains a multi-player-capable game core;
  g64coopdx never patches it.
- **Everything else is g64coopdx's:** the network layer, sessions, rollback, the lobby, the interface, chat, Lua,
  packs, and the drawing of names.
- The static build gives g64coopdx the brute forcer's savestates (renamed data sections and page tracking,
  `bruteforce/docs/engine.md`) without waiting for savestates in the library. A later move to `libglover` stays
  possible: the interface is the same set of calls.
- **g64coopdx is 64-bit from the start, for Windows and Linux** (decided). The 64-bit builds store the game's
  pointers as offsets (`docs/port64.md`), and their memory hashes were equal on Linux and Windows in all 49 runs of
  this discovery's tests.
- **The savestates must first work at 64 bits** (upstream item 10, in phase 0). The brute forcer and its savestates
  exist only as 32-bit programs today (`docs/building.md`: "The studio, the Space demo and the brute forcer stay
  32-bit").
  - Windows: the 64-bit objects have no `.data.rel` sections (measured with `objdump -h`). The engine has never been
    built at 64 bits, so the work is to build and test it there. This is likely small (INFERRED).
  - Linux: the 64-bit program is position-independent (`docs/architecture.md`), and 6 of the game's and runtime's
    objects in `build/headless64` have `.data.rel` sections (measured). The engine's section check does not accept
    them yet (`docs/port64.md`, "What is left", item 1).
  - Until the page tracker works there, full-copy savestates do: about 0.4 ms each, about 4 ms for a rollback of 8
    frames.
- **Why not 32-bit:** a 32-bit static build stores the machine's own addresses in the game's memory, so its hashes may
  differ between Linux and Windows. weemitt's docs say the 32-bit Windows program's hash is not comparable
  (`docs/testing.md`), and the brute forcer's notes name raw sound data in `pbankBuffer` as one cause
  (`bruteforce/docs/upstream.md`). Fixing that would mean the 64-bit builds' offset pointers anyway (INFERRED).
- Fixes flow one way for the game: into weemitt, then into g64coopdx by moving the submodule. g64coopdx's CI builds
  each new weemitt commit with its own tests ([repository.md](repository.md#ci)).

Why not the library alone today (INFERRED): the library holds one game per process, has no savestates, gives
read-only memory and one controller. All four are upstream work. The static build needs only items 1, 2 and 10 below
to start.

## Upstream changes weemitt needs

Each item keeps the rule: at the player count 1, `regress.py check` says "same as the baseline" and `dtcheck.py`
passes. New state goes in new globals outside `port_ram`. Ordered by when g64coopdx needs them.

| # | Change | Why | Size (rough) |
| --- | --- | --- | --- |
| 1 | **A build entry for an outside program.** A make target or variable list that an outside build reads to compile the game and runtime with weemitt's flags. The brute forcer reads weemitt's Makefile with `make -pn` today; a documented list is less fragile | g64coopdx's build | small |
| 2 | **Controller input for several players.** `Pad pad[4]` (`include/controller.h:27`) is in `port_ram`, and the game reads `pad[ActiveController]` (for example `src/game/ball.c:791`, `src/game/actor.c:730`). weemitt uses `pad[4]`, one past the end, as its debug pad (`docs/testing.md`). Each player's input needs its own pad: the four N64 pads can serve players 1 to 4 if the debug pad moves; more players need pads outside `port_ram` | every player | small |
| 3 | **The player count and per-player state.** The second and later gloves and balls in new arrays outside `port_ram`; player 0 stays at its N64 addresses. The game's per-player code runs once per player | the core of co-op | large: [state-inventory.md](state-inventory.md) |
| 4 | **Hooks for the session layer.** Level start and end, exits, death and respawn, garib and power-up collection, puzzle events, the camera per player: function pointers in `src/port/porthooks.h`, NULL by default | rules of co-op, Lua | medium |
| 5 | **Frame limits.** The object matrix table (256), the display list buffer, the vertex buffer (800), the actor pools, raised for many players, in new storage outside `port_ram` | 8 or more players | medium |
| 6 | **A cheap state hash.** A hash over the pages that changed since the last one, or over chosen structures, in well under a millisecond | desync checks every frame or second | small |
| 7 | **Savestates in the library** (`docs/port64.md` item 1): the renamed sections in the `-fPIC` build, a snapshot before `port_game_init` | the library route | medium |
| 8 | **Level packs:** a `.lev` and banks from buffers, extra bank lists outside `port_ram`, rotation and scale on every object type | custom levels ([mods.md](mods.md)) | medium |
| 9 | **An option that turns off every PC addition the controller can reach** (`bruteforce/docs/upstream.md` asks for it too) | a network game must not let one player open a local cheat menu that changes the shared game | small |
| 10 | **64-bit savestates:** on Linux, the engine's renamed sections must accept the position-independent build's `.data.rel`, `.data.rel.local` and `.data.rel.ro*`; on Windows, the engine must be built and tested at 64 bits. Full copies (0.4 ms) are enough at first | 64-bit builds of g64coopdx | small to medium |
| 11 | **Start a level from a record:** the level, the save, the players' carried state and the random seed, with no play before it; and a test that compares it with a level reached by play | instances, late join, repair ([netcode.md](netcode.md#instances)) | medium |
| 12 | **Players who join and leave a running level** at a frame, without touching the others | split play, entering a level that others play | medium |
| 13 | **The owner's rules as options that change nothing with one player:** a ball's real owner apart from its holder, the pickup rule, a lost ball killing its owner, no lives, a respawn of one player without restarting enemies, an exit for one player | the co-op rules ([state-inventory.md](state-inventory.md#the-owners-co-op-rules)) | medium |

Items 1, 2 and 10 let a prototype start. Items 3, 4, 12 and 13 are most of the game work.

## Experiments

The scripts are in [experiments/](experiments/). They hold no game data. The ROM was the Glover (USA) ROM with SHA-1
`270be17b3c8da9b88a7b99c2a545b0bce16837f4`, kept outside every repository.

### Repeated runs with the same input

`experiments/determinism.py` runs a build twice with the input generator of `tools/regress.py` (walking, jumps, the
ball buttons, the camera, the pause menu), each run from a new empty save, and compares the trace line and the
memory hash every 10 frames. With `--other` it also runs another build and compares.

| Runs | Levels | Frames | Result |
| --- | --- | --- | --- |
| `headless` twice, then `headless64` | 0 to 47 and the attract mode | 6,000 each | Equal in all 49: each build's two runs, and the 32-bit against the 64-bit build, in trace and hash |
| `glover-host` (Linux 64-bit library) twice, then `glover-host.exe` (Windows 64-bit DLL, Wine) | 0 to 47 and the attract mode | 3,000 each | Equal in all 49, in trace and hash |
| `glover-host` (Linux 32-bit library) twice, then `glover-host.exe` (Windows 32-bit DLL, Wine) | 0 to 47 and the attract mode | 3,000 each | Equal in all 49, in trace and hash. The libraries store pointers as offsets on 32 bits too (`docs/library.md`), so their hashes compare; the 32-bit Windows window program's hash would not |
| `headless64` (silent sound player) twice, then `headless64-mus` (the real sound player), trace only for the second | 0 to 47 and the attract mode | 3,000 each | Equal traces in all 49 |

Limits: the input is generated, not played by a person. Rope swinging (`HAND_SWISH`) and other rare paths may not be
reached. Wine is not Windows. The Windows runs used the library's test host; the window program was not compared.

### Savestates

`bruteforce/build/linux/glover bench --level L --seconds 3 --workers 1`:

| Level | State | Save after a frame | Load after a frame | Load of a state 300 frames away | Frame (full draw) |
| --- | --- | --- | --- | --- | --- |
| 13 (Atlantis boss) | 4.67 MB | 0.014 ms | 0.017 ms | 0.046 ms | 0.028 ms |
| 5 (a hub part) | 4.98 MB | 0.016 ms | 0.025 ms | 0.044 ms | 0.042 ms |
| 1 (a hub part) | 4.99 MB | 0.019 ms | 0.023 ms | 0.050 ms | 0.047 ms |
| 13, `GLOVER_FULL_STATES=1` | 4.67 MB | 0.429 ms | 0.415 ms | 0.392 ms | 0.028 ms |

### How much memory changes in a frame

`--dump` of the game's 24 MB after frames 600, 601 and 620 with generated input, compared byte by byte
(`experiments/memdiff.py`; the dumps were deleted):

| Level | Changed in 1 frame | Changed in 20 frames | 24 MB compressed (zlib, xz) | 1-frame difference, xz |
| --- | --- | --- | --- | --- |
| 5 | 9,526 bytes in 48 pages | 25,600 bytes in 56 pages | 0.99 MB, 0.73 MB | 12 KB |
| 13 | 6,738 bytes in 43 pages | 17,804 bytes in 48 pages | 0.83 MB, 0.61 MB | |

Most changed bytes lie below the heap, where the display lists and matrices are. Sending whole-memory differences
would cost about 12 KB a frame, 240 KB a second, for each player: too much as a network model, enough for a rare
repair.

### A separate program on the library

`experiments/g64probe.c` loads `libglover` with `dlopen` (or `LoadLibrary` on Windows), starts level 5 and runs
2,000 frames.

- One copy: 2,000 frames in 101 ms. A second `glover_create` on the same copy returns `GLOVER_ERR_BUSY` (-4), as
  documented.
- Two copies of the library file (`libglover-a.so` and `libglover-b.so`), loaded in one process: two independent
  games, each with its own memory, with equal hashes at every 10th frame and the same final state.
- The same on Windows (64-bit DLL under Wine): the same final hash, `9e1201fc`, as on Linux.

So one process can hold several games, one per copy of the library file (VERIFIED). A use: a test harness that runs
several players' machines in one process, or a host that keeps a reference copy of every instance ([netcode.md](netcode.md#instances)).
