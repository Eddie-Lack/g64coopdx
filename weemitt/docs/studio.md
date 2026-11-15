# The studio

The studio is the window program with the brute forcer (`bruteforce/`). It records every game frame you play as a
timeline. You can pause, scrub back and forth like a video, play on from any frame, and start a level directly.
You mark a start point and the points to reach, and the brute forcer's search looks for a faster way from the
start to them, in the background. Its best result goes onto the timeline when you take it: you watch it, or play on
from any frame of it (the plan: [bruteforce/docs/plan.md](../bruteforce/docs/plan.md)).

```sh
make studio -j4             # build/studio/weemitt-studio
make studio-windows -j4     # build/studio-windows/weemitt-studio.exe (copy SDL2.dll next to it)
```

## Using it

The game plays as in the normal program, with these differences:

- The physics are always Original: whole game frames, as on the N64.
- The sound is off. The real sound player keeps state outside the game's memory, which the timeline cannot restore.
- The Enhancements and Cheats menus are gone. They change the game's logic in a way the timeline's recorded input
  cannot replay. The menu has a Studio menu instead.
- The save file is not written. The game's save stays in memory.

| Control | What it does |
| --- | --- |
| All four C buttons together | pause, or play on from the frame on show |
| While paused: D-pad left or right | a frame back or on (hold to repeat) |
| While paused: L or R | a second (20 frames) back or on |
| While paused: A | play on from here with your input; the frames after this one are dropped |
| While paused: B | play the recording from here; it pauses at its end |
| The studio bar (while paused, or with the menu open) | drag the timeline, step, jump to the start or the end |
| Menu, Studio, Start a level | starts a level directly; the timeline starts again from there |

A step on at the end of the timeline adds a frame with no buttons held.

## Points and the search

The Points and search window shows with the studio bar.

1. Go to the frame to start from and press **Start here**. A small green sphere marks the glove there.
2. Go to a later frame where the glove (or the ball) should be, set the radius, and press **Point: glove here**
   (or **Point: ball here**). **Point: the level is done** asks for the level's exit instead. Add points in the
   order they must be reached; the last one is the end point (a red sphere, the others yellow).
3. Press **Search**. The search changes the inputs from the start point up to the frame of the last point marked,
   and plays on with your inputs after them. The window shows the best: the frame at which it reaches the end
   point, against the first result (your route). A load that the inputs pass before the end point (the level is
   left) counts 70 frames: a console takes about 3.5 seconds for it, and the game here takes one frame. Workers: how many processes search (0: one per logical
   processor). Input limits: what a runner can play. **strict** (the default) keeps every press and every gap
   between presses at least 3 frames long and the stick's change to 48 a frame; **loose** 2 frames and 96;
   **none** allows any input on any frame. **Stop the search** ends it.
4. **Take the result** puts the best inputs on the timeline from the start point to the frame they reach the end
   point, and goes to the start point. Press B to watch it, or go to any frame of it and play on from there.
   **Undo the take** brings back the timeline from before.

A search lives in a folder of its own, `studio/search-NNNN` in the program's preference folder (on Windows
`%APPDATA%\weemitt\Glover\studio`). It holds the start state, the run, the config, the search's results
(`best.txt`, `log.txt`) and its output (`search.log`). The state holds the game's memory: do not share it.

## Checking a strat on a console

The studio counts game frames. On a console, a game frame takes 3 video frames (at 60 Hz), and more when the
console cannot finish its work in time (a lag frame). Lag differs between routes, so check a found strat on a
console with Glover-Practice-Rom.

1. Take the result and learn it in the studio: press B to watch it, or step through it frame by frame.
2. On the console, turn on the practice ROM's frame counter and lag counter (HUD). Save a state at the start
   point.
3. Play your old route from the state and note both counters at the end point. Load the state, play the new route
   and note them again.
4. The time of each route in video frames is 3 times its game frames, plus its lag frames. The route with fewer
   video frames is faster.

## How it works

`src/port/studio.c` keeps the input of every update (`inputs[f]` is what update `f` got) and a saved state every 60
frames. Frame `f` is the game after `f` updates. Going to a frame loads the last saved state before it and replays
the recorded inputs from there. With more than 256 saved states, the spacing doubles and every other state goes.

- The savestates are the brute forcer's (`bruteforce/docs/engine.md`). The studio build links the game's objects
  into one object, `game.o`, whose writable data lies in sections of its own, as the engine's build does
  (`studio.mk`). The platform layer, the renderer and the menu stay outside it. `devview.c` too: it is game code
  that only the overlays read. A state saved by the studio loads in the same program's search workers.
- `platform.c` and `menu.cpp` call the studio where `PORT_STUDIO` is defined. The normal build is unchanged
  (`tools/regress.py check`).
- The game's clock (`port_time_us`) is fixed at 0 in the studio. It feeds only the profiler timers, and a fixed
  value keeps the game's memory the same in every run.
- Every update goes through `run_update`. `studio_before_update` puts the recorded input there on a frame the
  timeline holds, or records the input. `studio_after_update` counts the frame and saves a state when one is due.
- A seek replays with the full draw. The brute forcer's trimmed draw would leave picture data that lasts: a level
  computes its landscape's static matrices once, when it starts, and the trimmed draw skips writing them.
- After a seek the renderer starts a new interpolation (`gfx_interp_record` with cut 1).
- Starting a level puts the jump in the first state of a new timeline. The jump is due at the next update, so a
  replay from that state repeats it.
- `weemitt-studio COMMAND` runs the brute forcer's commands (`search`, `bench`, `play`, `saveload`) in the same
  program, without a window. The search's workers are such processes, so a state that the studio saves loads in
  them. On Windows they start without a console window (`proc_spawn`, `proc_start` in `bruteforce/src/procs.c`).
- **Search** writes the start state (`start.state`), a run of whole frames from it (`base.events`: `#state
  start.state`, then one `F INPUT` line per frame of the timeline from the start point) and `search.json` with the
  points and `"loadFrames": 70` (bruteforce/README.md, "The search"). It starts `weemitt-studio search search.json` in the background and
  reads `best.txt` twice a second: the best inputs, the frame they reach the end point at and the loads they pass.

## Tests

`WEEMITT_STUDIO_SELFTEST=1` with `--headless` records the run, then goes to 400 frames of the timeline (random
jumps, steps on and back, frames with a saved hash) and compares each with the state the game had there: the state
trace, and every 50 frames the hash of the whole state. It prints the number of failures and exits with 1 when
there are any.

```sh
WEEMITT_STUDIO_SELFTEST=1 build/studio/weemitt-studio --rom ROM --level 13 --headless 1500 --input INPUT
build/studio/weemitt-studio saveload --rom ROM --level 13 --frames 600
```

The Windows build passes both under Wine. Wine on the build machine cannot open an OpenGL window, so the Windows
window is tested on Windows.
