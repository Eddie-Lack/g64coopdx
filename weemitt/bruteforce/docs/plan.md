# Plan

This is the working plan for the brute forcer. Agents keep it current: mark what is done, and correct what turned
out different.

## The goal now (3 November 2026)

The owner changed the goal. The brute forcer now looks for real-time speedrun strategies, routes and timesaves,
not TAS improvements. It moved from an earlier repository of its own into weemitt (`bruteforce/`). That
repository is no longer used. The Windows program comes first.

The owner's answers:

- The studio lives in weemitt. It plays the game in a window, records the play as a timeline of states, and hands
  control between the player and the search (below).
- Time is counted in game frames, and found strats are checked on a console. The console videos do not hold
  enough to calibrate a model of lag in play. Loads are added to the time where a strat changes them: each costs
  `loadFrames` (70 game frames, from the console videos, docs/game.md).
- Inputs that the search finds must be playable by a person: adjustable limits, from strict (a runner can do it)
  to loose (ideas).
- Results need no emulator check. The sync tests stay as the proof that the engine plays the real game. The
  BizHawk movie tools stay as they are, without new work.

The phases:

1. **Move** the engine, the programs, the tests and the docs into weemitt. Done: `bruteforce/`, built by its own
   Makefile from weemitt's game code. The normal weemitt build does not change.
2. **The studio**, in weemitt's window build ([docs/studio.md](../../docs/studio.md)). Done: the build
   (`make studio`, `make studio-windows`), the timeline, pause with all four C buttons, the studio bar, stepping,
   playing the recording, playing on from a frame, starting a level, the points (places and the level's end), the
   hand-off to the search and taking its result, the input limits (none, loose, strict). The plan:
   - A timeline of saved states while the player plays (the page tracker makes them cheap).
   - All four C buttons at once (or a menu item) pause the game. A video-player bar then scrubs the timeline:
     drag the playhead, step one frame back or forward, rewind, fast-forward.
   - The player marks a start point, mid-points and an end point. A point is a place (a sphere the glove or the
     ball must reach) or a game event (a condition such as the level's exit, as in the search's config).
   - The search runs in worker processes from the start point and tries to reach the points in order, in the
     fewest frames, within the input limits. Its best result appears on the timeline. The player can watch it,
     and take over from any frame of it.
3. **Time**: done. The search counts game frames and the loads a play passes (`loadFrames`). A runner checks a
   found strat on a console with Glover-Practice-Rom's frame and lag counters ([docs/studio.md](../../docs/studio.md),
   "Checking a strat on a console").

## The method

A TAS author has a base movie: the inputs for every game frame. The brute forcer:

1. Runs the base movie to the start of a chosen window of frames and saves the game state there.
2. Makes a candidate: a copy of the window's inputs with small random changes (perturbations).
3. Loads the state, plays the candidate, and stops at the window's end or when a stop rule fires.
4. Scores the result with a fitness function: for example where the glove is, how fast it moves, the frame on which
   it reached a place, or a state such as a pick-up collected.
5. Keeps the candidate when it scores better (or by a looser rule, see Search), and goes on from it.
6. Runs many workers at once, each with its own copy of the game, and shares the best candidates between them.

Results were written as movies that the emulator plays, and were checked there before they counted. Since the change
of goal (above), results need no emulator check.

## Facts to start from

- **gloveworld** is the matching decompilation of the USA ROM: all game code is C, and it builds the exact ROM.
  Its `tools/emu` runs the real ROM in mupen64plus with scripted input and reads RDRAM.
- **weemitt** is the native PC port of the decompilation. Its headless build (`make headless`) runs the game's
  logic with no window and no sound, game frame by game frame. Measured: about 25,000 game frames a second on one
  core of the build machine (an earlier figure of 3,000 was wrong).
- In weemitt, `port_ram` is one block that stands for the N64's RDRAM. Every game global sits at its N64 offset in
  it, and so does the heap. Most of the game's state is therefore in one block. Some port-side state lives outside
  it (static variables in `src/port` and a few in `src/game` added for the frame-rate work); a savestate must hold
  those too.
- weemitt is a 32-bit program (the game's structures hold 32-bit pointers).
- weemitt's frame-rate code is exact at the original timing: with update length 1 (`deltaTime` 1) it runs the
  game's logic as the decompilation does (`tools/regress.py`). The brute forcer only ever uses length 1.
- The game draws through `GraphicsStep`, and drawing calls `Random`. The display lists must be built every game
  frame (with no renderer), or the random sequence differs from the N64.
- The game reads the controller once per game frame (20 a second in levels). Facts about input and timing are in
  Glover-Practice-Rom's `docs/game.md`.
- Glover-Practice-Rom has savestates, an input recording format, a frame log and many state viewers on the N64.
  Its `docs/game.md` and `docs/build.md` describe what a savestate must hold on the console.

## Engine

Done: `engine/`, described in [engine.md](engine.md). The engine is a library: the game's logic built natively,
behind a small C interface, `engine/engine.h`: `engine_create`, `engine_step` (one game frame with one input),
`engine_save` and `engine_load`, reads of the game's memory by N64 address and `engine_symbol` (the N64 address of a
game global). `engine_read`, `engine_draw` and `engine_logic` run the three parts of a game frame apart, for the sync
test ([sync.md](sync.md)).

What turned out different from the first plan:

- The library is static (`libglover.a`). All the tools are one program, `glover`, with a command for each tool. The
  game's writable data is grouped by renaming the sections of one partly linked object (`gstate_data`,
  `gstate_bss`), which works on Linux and Windows alike. A shared library would load at a different address in each
  process, and separate programs would place the game at different addresses.
- A state holds `port_ram` below the heap and the parts of the heap that have been written. The heap's unused
  middle is read-only, and a fault handler tracks the writes there. Reading the allocator's blocks after each frame
  missed blocks freed within a frame (a level load).
- The Windows program loads at 0x08000000, without ASLR and without a relocation table. The Linux one is not
  position-independent and loads at 0x08048000.

The rest of the first plan still holds:

- Build it from weemitt's game and runtime code (a submodule), with the headless parts only. Prefer a build step in
  this repository over changes to weemitt; note in `docs/upstream.md` what would be cleaner upstream.
- The owner runs it on a local Windows 11 PC. Development and the automated tests run on Linux (the build machine),
  and the Windows program is cross-compiled with MinGW-w64, as weemitt's Windows build is. Both are 32-bit
  programs (the game's structures hold 32-bit pointers); a 32-bit process on 64-bit Windows can use 4 GB when it is
  built large-address-aware.
- One process holds one game. Workers are separate processes of the one program, one per core. The game's data
  holds pointers into its own memory and the program, so every worker places them at the same addresses; a state
  saved in one worker then loads in any other, and workers share states through shared memory (a file mapping on
  Windows). Windows has no
  `fork`: each worker starts, loads the ROM once (well under a second), and then only loads states.

## Speed

The owner wants simulations as fast as possible. Speed is a first-class goal from the start, measured with a
benchmark that the owner can run on the Windows PC (`bench`: game frames a second per core and for all cores,
saves and loads a second).

Measured on the build machine (callgrind, 600 game frames of Atlantis 1 in weemitt's headless build): the game's
logic (`GameStep`) is 46% of the work, building the display lists (`GraphicsStep`) 36%, the level load 14%. A game
frame is about 280,000 instructions. Measured with the engine (Intel Xeon at 2.8 GHz, boss level 13): about 25,000
game frames a second on one core (0.04 ms a frame). A state is 4.6 MB; a save or a load takes 0.33 ms, as long as
8 game frames. So cheap loads (4) and a smaller state come first.

The benchmark (`bench`) and the save and load test (`saveload`) are done. The owner's numbers are in
[engine.md](engine.md) once they arrive.

In order of expected gain:

1. **Simulate only from the first changed frame.** Keep a state for every frame of the current best inputs in the
   window. A candidate starts from the state at its first change.
2. **A logic-only draw.** First step done: the trimmed draw ([engine.md](engine.md), "The trimmed draw") skips ten
   functions that only build the picture. A frame takes about 20% less time, and the search uses it. The draw's
   other work stays: it moves animations on, places collision spheres and platforms, calls `Random` and changes
   lists that the logic reads. Cutting more needs a copy of the object drawing (`CreateObjects`, `DrawActor`,
   `TransformAndDrawObject`) that computes the matrices only for objects with a sphere, a platform or a sprite.
3. **Stop early.** Stop a candidate when the glove dies or falls out, when it can no longer beat the best, or when its
   state matches one already seen (a hash of the values the score and the archive use).
4. **Cheap loads.** Done: a load copies back only the pages written since the state was saved, found with page
   protection on both systems ([engine.md](engine.md), "Copying only what changed"). The plain full copy stays as
   the reference: `GLOVER_FULL_STATES=1` turns the tracking off, and the tests pass both ways.
5. **Compiler tuning**: `-O3`, link-time optimisation, profile-guided optimisation with a training run, and the
   owner's CPU as the target (`-march=znver4`, with a generic build too). Measure each.
6. **Workers**: the owner's CPU has 8 cores and 16 threads, all of one kind. The benchmark measures 8 workers
   against 16 (two threads on one core share it; such code often gains 20 to 30%), and the faster count becomes the
   default. Options: fewer workers, and a lower process priority (on by default, so the PC stays usable). The
   machine is a laptop: its clock drops when it runs hot or on battery, so the benchmark runs long enough to show the
   steady speed, and the README tells the user to plug in and choose the best performance power mode.
7. **Memory**: 16 GB in all. Each worker needs the game's memory (24 MB in weemitt) and its cache of states along
   the window (4.6 MB a frame in the boss level, as measured; less once the state is made smaller). 16 workers with
   a 20-frame window need about 1.5 GB. The shared archive keeps inputs for every block and full states only for a
   limited number of blocks, within a budget set in the config (default: a quarter of the free RAM). A block without
   a state is rebuilt by replaying its inputs from the nearest saved state.
8. **Search methods that waste fewer frames** (see Search).

**32-bit, not 64-bit.** A 64-bit build would have twice the registers, perhaps 10 to 20% faster. But the game's
structures hold 32-bit pointers, as on the N64, and so does the data loaded from the ROM. A 64-bit build would need
every pointer in every structure turned into a 32-bit offset, through the whole game's code. That is a very large
change to code that must stay exact, and the sync risk is high. The engine stays 32-bit. Revisit this only if the
benchmark shows the engine is slow after all the other speed work.

**No GPU.** The game's code branches too much to run well on a GPU. The simulation stays on the CPU.

## Sync

The engine must match the real game. Without this nothing it finds is real.

1. Pick several TAS movies or long scripted inputs that cover many levels and actions.
2. Run each in the engine and in mupen64plus (gloveworld's `tools/emu`). Compare the game state each game frame:
   the glove's and the ball's position and speed, their actions, the camera, `frameCount`, `rseed`, the level.
3. Find and fix every difference. Likely causes: floating point (x86 and the VR4300 both use IEEE single, but
   library functions and the order of operations can differ), memory that the N64 leaves as it was and the port
   clears, the heap size, the timing of lag frames.
4. Keep this as an automated test (`tests/sync.py`) that runs on every change.

Lag frames: on the N64 a game frame can take more than its 3 retraces, and a TAS movie records input per controller
read. Find out how the emulator's movie format counts frames for Glover, and map movie frames to game frames
exactly (`docs/game.md`).

Status: done for steps 2 to 4 ([sync.md](sync.md)). The emulator is BizHawk 2.11.1's own Mupen64Plus core, built
from BizHawk's source: it times the game's threads differently from mupen64plus 2.5.9. The engine matches it in the
attract mode, in all seven boss levels and on the owner's first movie. Reads in the middle of a logic frame (lag
frames, loads) run at a stop inside the engine's logic frame. Open: values from BizHawk itself at the end of the
owner's movie, to confirm that the core here runs as BizHawk does (sync.md, "Open questions").

## Input files

- **BizHawk movie (`.bk2`)**: the owner's format (BizHawk's Mupen64Plus core). Read and write it first.
- **Mupen64 movie (`.m64`)**: used by Rosalie's Mupen GUI and mupen64plus. Read and write it too.
- Other formats when the owner asks: Project64 recordings.
- Glover-Practice-Rom's recordings and the game's own `.moo` demos: read them.
- The engine's own text format for a window of inputs: one line per game frame, easy to read and edit.
- Movie files are read and written by a separate converter (a library module with a small command-line tool). It
  turns a movie into the engine's form once, before a run: one input per game frame, with movie frames and lag
  already mapped. The search never parses a movie; it reads that array. Movies are written only when a new best is
  saved.
- The emulator check of a new best runs in its own process, beside the workers, and never makes them wait.

## Search

- **Perturbations**: change the stick's angle or length a little, swap or move a button press by a frame, add or
  remove a press, copy a stretch of inputs to another frame. Each with a weight set in the config.
- **Archive of states**: keep many distinct states, not one best. Each state goes into a block by a few rounded
  values (for example the glove's position on a grid, its action, the ball's state). Each block keeps the best
  inputs that reach it. A step picks a block (weighted towards promising ones), loads its state, plays a burst of
  changed inputs, and sorts every new state it reaches into its block. This explores many routes at once and keeps
  stepping stones that score worse for now. Make it the main search; the block keys and weights come from the
  config.
- **Other rules**: hill climbing (keep only better), simulated annealing (sometimes keep worse), a population of
  candidates with crossover.
- **Adaptive changes**: pick a kind of perturbation more often while it keeps finding improvements. The stick's
  angle and length are continuous; small steps around a good value work better than random values there.
- **Windows**: a short window (tens of frames) at a time, moved along the movie. A score may look past the window's
  end (play the base inputs after it).
- **Constraints**: which buttons may change, frames that must not change, the stick's range.

## Scores

- Built in: distance to a point or a plane, speed, height, the frame on which a condition first holds, a state value
  (an action, a flag, a counter), and weighted sums of these.
- Conditions and values are named by the game's symbols (gloveworld's names) and fields of known structures (the
  glove, the ball, the camera, actors).
- Configs are text files (JSON or TOML) that name the movie, the window, the score and the search rules.
- A score that needs real code is a small C file compiled into the run (a plug-in).

## Output

- The best movie so far, written out at intervals, and a log of the score over time.
- A short report: what changed (which frames and inputs), how the score changed, and the frames gained.

## What to build

There are no milestones. Build what the plan describes, in the order you judge best. Two things come before any
search result is trusted: the sync test (Sync) and the save and load test (save, run some frames, load, run them
again, the same state).

- The engine library for Linux and Windows (done), the benchmark (done) and the speed work (Speed).
- The movie converter (Input files: `.bk2` and `.m64` done), and a tool that plays a movie in the engine and prints
  the state per game frame (done: `glover play`), and one that compares two runs and shows where they split.
- The search (Search), the scores (Scores) and the output with the emulator check (Output), with many workers. Done:
  hill climbing over a window with many workers, goal, fail and progress scores, and the emulator check
  (`tools/check.py`). To do: the archive of states, annealing, adaptive changes, plug-in scores, the report.
- Tools that help: a path plot of the glove and the ball, a state viewer for one frame, live progress in the
  terminal. A GUI or a drawn view of the game (weemitt's renderer) if it helps.

## Questions for the owner

Answered:

- The TAS movies are BizHawk movies (`.bk2`), made with BizHawk's Mupen64Plus core. There is no movie yet: build
  and test with scripted input until one comes.
- The first segments to improve are the boss fights. A boss fight's score: the fewest game frames to the level's
  end (the start of the level's exit).
- Results are checked in an emulator here (now BizHawk's own core); the owner checks the final movies in BizHawk.
- The machine: a Windows 11 laptop with 8 cores and 16 threads, all of one kind, and 16 GB of RAM.
- 32-bit engine (Speed, the end of the section).

Open: none.
