# Discovery: online co-op for Glover

This is the report of the discovery phase for g64coopdx: an online multiplayer version of Glover (N64, USA) on PC,
with mods, in the spirit of sm64coopdx (the online version of the Super Mario 64 PC port). The discovery built no part
of the feature. The owner's decisions are listed in [open-questions.md](open-questions.md#decided). In short: 16
players by design, 8 promised; each player a glove with its own ball; Windows first; Lua and custom levels in the first
version; players may be in different levels of the same world; no lives; each player exits alone. The
[terms](#terms) used throughout are defined at the end of this file.

## Summary

**Feasibility.** The project is feasible. A rollback network game (see [Terms](#terms)) needs game logic that gives
the same result on every machine, and savestates that are cheap to save and load. The PC port, weemitt, has both. At
the time of the discovery, the fast savestates were only in its 32-bit brute forcer. Game change 8 made them work at
64 bits in phase 0 (`make savestates`). This discovery measured the game logic and the savestates:

- The same inputs gave the same game state on every run and every build tested: Linux and Windows, 32-bit and
  64-bit. The test ran all 48 levels and the attract mode. 196 pairs of repeated runs and 147 pairs of different
  builds matched in their per-frame state trace and their memory hash. 49 more pairs, with the real sound player
  against the silent one, matched in their trace. [netcode.md](netcode.md#the-measurements) lists each set of runs.
- A savestate of about 5 MB saves or loads in 0.02 ms with page tracking (see Terms, "Brute forcer"), or 0.4 ms as a
  plain copy. A game frame lasts 50 ms of real
  time in a level, and computing one takes 0.03 to 0.05 ms.

The work is in the game's code, which assumes one player throughout: 225 of its 1,234 functions touch the glove or the
ball.

**The recommended design.** The session is in one world at a time. Every player has a glove and a ball, and may be in
any level of that world, its boss, its bonus level or the hub. A portal to another world opens when every player is
back in the hub. Each machine shows its own player's camera. There are no lives: a player who dies respawns at the
last checkpoint their own ball passed. Each player leaves a level alone by bringing a ball to the exit. The host
chooses whether a glove may pick up only its own ball or any ball; a lost ball always kills its real owner.
[Details](#recommended-design).

**The recommended network model.** Rollback with host-confirmed inputs, one simulation per occupied level (an
**instance**). Every machine runs its own level's instance on the inputs of the players in it. It shows its own player
at once, predicts the others, and steps back through savestates when the real inputs differ. The host decides each
frame's final inputs for every instance. When a player's input arrives too late, the host repeats that player's last
input, and the others go on without waiting. Only inputs and session messages cross the network: a few bytes per
player per frame, and no game data. A player who enters a level that others are already playing replays that
instance's inputs from its start. [netcode.md](netcode.md).

**How the project relates to weemitt.** g64coopdx carries its own copy of weemitt, taken at a pinned commit, and
changes the game code in it freely. weemitt's rules for an exact copy of the original game don't apply to g64coopdx.
Fixes from weemitt are taken by hand when they help. The copy's first commit is weemitt unchanged and must pass
weemitt's regression test; from then on g64coopdx keeps its own baseline. g64coopdx's own folders hold the network,
the sessions, the interface, Lua and the packs. [weemitt.md](weemitt.md#how-g64coopdx-relates-to-weemitt).

**The biggest risks.**

1. The size of the multi-player change to the game's code, above all the bosses, bonus levels and cameos, which
   each have their own code, and players who join and leave a running level.
2. Whether the frame limits (256 object matrices, the display list) can be raised far enough for 16 gloves.
3. The sound: the logic reads sound state, so it must be split from the sound's output before rollback works well.
4. The instances: entering a level by replaying its inputs, and session events (a level completed, a save bit) that
   must reach every instance at the same frame on every machine.

**Rough size.** About 31 to 51 weeks of focused work for one developer, in seven phases that each end in something
playable or testable. [roadmap.md](roadmap.md).

## The documents

| File | Question | Contents |
| --- | --- | --- |
| [sm64coopdx.md](sm64coopdx.md) | 1 | What sm64coopdx is and how it works, with sources; what carries over |
| [weemitt.md](weemitt.md) | 2, 3 | Where the port stands; the options for building on it; the changes to the game code; the experiments |
| [state-inventory.md](state-inventory.md) | 4 | Every single-player assumption in the game's code; the size of the refactor; the frame's limits |
| [netcode.md](netcode.md) | 6, 7 | The network models, the measurements, the recommendation; transport, services, the handshake |
| [mods.md](mods.md) | 8 | Lua, custom levels and models from Glovebox, sharing mods without game data |
| [level-pack.md](level-pack.md) | 8 | The level pack's format as g64coopdx reads it today |
| [level-file-safety.md](level-file-safety.md) | 8 | What a level file must satisfy for the game's own parser to stay inside its memory |
| [model-banks.md](model-banks.md) | 8 | The plan for models in a level pack: the file, the check of a bank, and how the game loads it |
| [lua-trial.md](lua-trial.md) | 8 | A trial of Lua on Linux and Windows: what is the same on both, and what the design must do about the rest |
| [repository.md](repository.md) | 12 | The layout, build, CI, license, ROM rules, a draft of the rules file for agents (CLAUDE.md), names |
| [roadmap.md](roadmap.md) | | Phases, sizes and dependencies |
| [open-questions.md](open-questions.md) | | The owner's decisions |
| [experiments/](experiments/) | | The scripts behind the measurements |

The discovery had 13 questions. The column above, and the other files' introductions, refer to them by number:

1. What sm64coopdx is: its features, network model and mod system, and which carry over to Glover.
2. Where weemitt stands, and what it already gives a network game.
3. How g64coopdx should relate to weemitt, and what weemitt must add first.
4. The game's single-player assumptions.
5. The design of co-op play.
6. The network model, with measurements.
7. Transport and services.
8. Mods.
9. Where the new state lives, given weemitt's memory layout rule.
10. Player presentation and the interface.
11. Testing.
12. The new repository.
13. Risks and unknowns.

This file answers questions 5, 9, 10, 11 and 13.

## Method

- Read: weemitt's CLAUDE.md and docs (architecture, delta time, interpolation, the library, the host world, the
  64-bit port, testing, the studio), the brute forcer's docs, Glovebox's technical.md, gloveworld's docs, and
  sm64coopdx's repository (a shallow clone at commit `8cd6e59`, read only). The host world is weemitt's way for a
  program that loads its library to put the glove and the ball into a world of its own.
- Ran: weemitt's headless builds, 32-bit and 64-bit; its library for Linux and Windows (under Wine 9); the brute
  forcer's benchmark; a clang scan of every use of the player globals; gdb probes of the frame limits; a tiny program
  that loads the library.
- Four research agents worked in parallel (sm64coopdx, the state inventory, mods and Glovebox, network libraries).
  Their claims were spot-checked against the sources, and their notes are the base of the detail sections.
- Marks: **VERIFIED** means read in a named source or measured here; **INFERRED** means concluded from that;
  **UNKNOWN** means not known yet, with a way to settle it. The other files use the same marks.
- Paths into weemitt, gloveworld and Glovebox, and their line numbers, refer to the commits that were read: weemitt
  `f509ef7` (8 November 2026), gloveworld `aa652b5`, Glovebox `5c25495`. This holds for paths written `weemitt/...`
  too. Some of their line numbers no longer match g64coopdx's copy of weemitt.
- The ROM (Glover USA, SHA-1 `270be17b3c8da9b88a7b99c2a545b0bce16837f4`, the same as gloveworld's `NGVE.sha1`) stayed
  outside every repository. Memory dumps were deleted after use. Nothing from the ROM is in this report.
- The discovery made no commit or change in weemitt, gloveworld or Glovebox: `git status` was clean in all three.
  Builds went into weemitt's build folders, which git ignores.

## Recommended design

The owner's decisions shape this section: each player a glove with its own ball; players may be in different levels
of the same world; no lives; each player exits alone; ball pickup is a host option; respawn at the player's own
checkpoint. The full list of decisions is in [open-questions.md](open-questions.md#decided).

### Worlds and instances

- The session is in **one world at a time**: the hub, Atlantis, Carnival, Pirates, Prehistoric, Fear or Space
  (`enum World`, `weemitt/include/enums.h:285-294`). Players may be in any level of that world, its boss level, its bonus level, or
  the hub's sections.
- A portal from the hub to another world opens only when every player is in the hub. Then the session's world
  changes.
- Each occupied level is an **instance**: one simulation of that level, run on the inputs of the players in it.
  Two players in Atlantis 1 share an instance; a third in Atlantis 2 has another.
- A player who enters a level that has no instance starts one. A player who enters a level with an instance joins
  it: the machine builds the level from the instance's start record and runs its input log
  ([netcode.md](netcode.md#instances)).
- An instance ends when its last player leaves. Entering the level again starts it fresh, as in the single-player
  game.
- Players start a new instance around the level's start point, spread in a ring. A player who joins a running
  instance appears at the level's start point.

### The ball

- Each player has a ball, and every ball has a **real owner** that never changes. The ball's holder (`ball.link`)
  is a separate thing ([state-inventory.md](state-inventory.md#the-owners-co-op-rules)).
- A host option, for the whole session: **own ball only** (a glove picks up only its own ball) or **any ball** (any
  glove may take any free ball).
- A ball that is lost (thrown off a cliff, burst, drowned) kills its real owner, whoever held it last. With any-ball
  pickup, one player can get another killed this way. The rule allows it.
- Ball changes from power-ups (bowling, crystal, ball-bearing and the others) apply to the ball that touched the
  power-up, or to the ball of the player who took it when a glove takes it.
- Gloves and balls don't collide with each other in the first version. The game has no such physics, and new
  physics would be new game logic to prove.
- Any ball works for puzzles: a switch a ball presses, a gate a ball falls through.

### The camera

- Every player has a camera, simulated in the instance, because the glove's controls, the enemies' update range,
  some puzzles and the sounds depend on it ([state-inventory.md](state-inventory.md#rollback-and-the-inventory)).
- Each machine draws from its own player's camera.
- A boss's or a cameo's scripted camera applies to every player in that instance.

### Death and respawn

- **No lives.** A death never costs a life and never ends the game. The game already has a cheat bit that stops the
  loss of lives (bit 8 of `cheatActivated`, `weemitt/src/game/hand.c:3804`); the co-op rule goes further and turns the
  game over path off entirely.
- A player who dies respawns at **the last checkpoint their own ball passed** in that level
  (`handControl.checkpoint` is part of the glove's state, so each glove already has its own; a gate fires when a
  ball falls through it,
  `weemitt/src/game/platform.c:1513-1536`). Before any checkpoint, at the level's start point.
- The other players play on. The game's `RestartPlayer` restarts every enemy (`platform.c:1550`); the co-op respawn
  must restart only the dead player's glove, ball and camera.
- The gates' look (touched or not) is shared in the instance; which gate a player respawns at is per player.

### Level exits

- **Each player exits alone**: a player who brings a ball to the exit leaves the level, and the others play on.
  The game's own exit rule decides where the player goes next (`ExitLevel`, `weemitt/src/game/game.c:3716`), usually
  the world's hub section.
- With any-ball pickup, a player may carry another player's ball to the exit. The ball's real owner then gets a new
  ball at their checkpoint.
- The exit timers (`levelExitTimer` and the others) become per player.
- The ball that a player leaves behind in the hub when they change level (`ballLeftBehind`) is kept per player.

### Garibs and progress

- Within an instance, a garib taken by anyone counts for everyone in it; the nearest glove or ball takes it.
- The level's "all garibs" bit is set when one instance's players have them all.
- Score per player, for the end screen.
- Progress is the host's save. A level counts as done when any player exits it. Clients never write their own saves
  from a session; a session can start from any of the host's save slots. sm64coopdx does the same with the host's
  save ([sm64coopdx.md](sm64coopdx.md), 2.10).
- Progress made in one instance reaches the others as a session event at a confirmed frame (for example, a hub door
  that opens when a level is done) ([netcode.md](netcode.md#session-events)).

### Bosses, bonus levels and cameos

- A boss level is an instance like any other. A boss targets the nearest player in it, as the enemies do. Its
  health is shared by the players fighting it; any player's ball hurts it. The five bosses with code
  (`weemitt/src/game/boss.c`) each need their own change for several players. The Atlantis boss has no function of
  its own, only a script.
- Bonus levels: the players in the bonus instance play at once, with its shared timer. In the single-player game,
  losing a life in a bonus level ends it (`hand.c:3799-3803`). With no lives, a death in a bonus level sends only
  that player out; the others play on.
- Cameos (scripted scenes) run for everyone in the instance. They move the glove of the player who triggered them.
  The others in that level are held still and hidden while the cameo runs.

### Pause and menus

- The pause menu is a local overlay; the game goes on. The host may stop the game for everyone.
- Signs (tips) show as a local overlay without stopping anyone.
- The options of weemitt's Enhancements and Cheats menus are session settings that the host sets. weemitt's cheat
  list in the pause menu is off online (game change 7).

## Where the new state lives

weemitt keeps every global of the game at its N64 offset in `port_ram`, and never changes the type or size of one.
g64coopdx doesn't follow that rule (the owner's decision). Two facts still shape where new state goes:

- **Pointers in the game's memory can only name memory in `port_ram`** on the 64-bit builds: each is a 32-bit word,
  the offset into `port_ram` plus a fixed base (`weemitt/docs/port64.md`, "The representation"). Game structures point
  at the gloves and balls (`ball.link`, `bullet->target`, the actor lists). So the second and later players' gloves,
  balls and cameras live in `port_ram`.
- **Growing a global costs more than allocating.** weemitt's tables in `gen/` give every global its N64 offset, and
  regenerating them needs a built gloveworld and the MIPS tools. Data loaded from the ROM (the main segment, 0xF5677
  bytes from 0x80100000) can't move; the player globals lie past it, in memory the game zeroes
  ([weemitt.md](weemitt.md#changes-to-the-game-code)).

**Recommendation:**

- Player 0 keeps the N64 addresses: `hand` at 0x802902D8, `ball` at 0x8029F978, and the others
  ([state-inventory.md](state-inventory.md)).
- The other players' slots, up to 15, are allocated on the game's heap. The game makes one for each other player in
  a level's first frame of play (`G64ExtraPlayersStart` in `weemitt/src/game/g64players.c`). A player who joins a
  running instance gets a new slot at the frame whose confirmed inputs add them (`G64SetPresent`, `G64AddPlayer`).
  The heap is in `port_ram` and has about 21 MB free in a level (`weemitt/docs/host-world.md`); weemitt's host world
  already allocates there.
- The game's code keeps using the N64 globals. Before each other player's part of the frame, that player's slot and
  the globals trade places, and after it they trade back (the swap, [roadmap.md](roadmap.md#phase-1-two-gloves-offline),
  game change 2). The design first planned a table of pointers per player (`players[i].hand` and so on) at every use.
  That is no longer the plan.
- Larger frame buffers (the object matrices, the display list, the vertices) go on the heap too, or into enlarged
  globals where a pointer can't easily be redirected.
- The savestate covers all of `port_ram` and the game's variables outside it (`weemitt/bruteforce/docs/engine.md`,
  "Savestates"), so rollback sees every player.

## Player presentation and the interface

| Need | Plan |
| --- | --- |
| Several gloves on screen | Every glove and ball is drawn by the game's own draw path, which every machine runs the same way ([state-inventory.md](state-inventory.md#rollback-and-the-inventory)) |
| Telling gloves apart | A color per player. The renderer tints an actor's draw by its player. weemitt's draw marks already tell the renderer which actor each command belongs to (`port_draw_mark_hook`, `weemitt/docs/library.md`). The tint is drawing only, so the logic stays the same |
| Custom glove and ball models | Model packs, drawn in place of the player's model ([mods.md](mods.md#player-models)) |
| Names | Text above each glove, drawn after the game's lists from the local camera; hidden behind walls by the depth buffer; with distance fade |
| The HUD | The game's own HUD for the local player; small hearts beside each name for the others |
| Off-screen players | An arrow at the screen's edge toward each one |
| Chat | A box over the game (Dear ImGui, which weemitt's menu already uses); a length limit; `/` commands for the host |
| The player list | Name, color, ping, the level each player is in, the host's kick and ban buttons with a confirm step |
| Host screen | Session name, password, player limit, save slot, settings, packs, direct or lobby |
| Join screen | The lobby browser (version-filtered, as sm64coopdx's), a direct address, a password; the pack list with a prompt before download |
| Splitscreen | Not planned; one player per machine |

## Testing

What weemitt's tools already cover, and what g64coopdx adds:

| Tool | Covers | Use in g64coopdx |
| --- | --- | --- |
| `tools/regress.py` (weemitt) | the logic at one player, every frame's state and memory hash against a baseline | the copy's first commit, against weemitt's baseline; after that, against g64coopdx's own baseline |
| `tools/dtcheck.py` (weemitt) | the Smooth mode | not used: online play runs whole frames |
| `make libcheck` (weemitt) | the library on Linux and Windows | not used: g64coopdx links the game statically |
| `tests/sync.py` (weemitt's brute forcer) | the engine against the ROM in an emulator | not used: g64coopdx doesn't need to match the console |
| `--record` and `--replay` (weemitt) | a run's inputs and checks | the model for the session's input log |
| `--trace`, `--hash-every` | the per-frame state | the desync reports |
| this report's `determinism.py` | runs and builds compared | grows into the N-player determinism test |

New tests:

1. **The toy game.** A small deterministic game behind the same interface lets every rollback, network and session
   test run in CI without a ROM ([repository.md](repository.md#ci)).
2. **The session harness.** A host and N clients in one run, on loopback, each fed a recorded input file, with a
   simulated network (latency, jitter, loss, reordering, duplication). The check: every client's state hash at every
   confirmed frame equals the host's, and equals one offline run of the host's confirmed input log. With the static
   build, each client is its own process. (weemitt's library also allows several games in one process, one per copy
   of the library file: [weemitt.md](weemitt.md#a-separate-program-on-the-library).)
3. **Across systems.** The same session's input log replayed on Linux and on Windows (Wine in CI) gives the same
   hashes.
4. **Instances, late join and repair.** Players who move between levels, a client that enters a running level at
   frame F, and a client whose state is broken on purpose, all end equal to the host's copy of each instance.
5. **Fuzzing.** Every message parser, the handshake and the pack reader.
6. **Bots.** Scripted inputs that play each level for N players, for the frame-limit peaks and long runs.

## Risks and unknowns

| # | Risk or unknown | How to resolve it |
| --- | --- | --- |
| 1 | The multi-player refactor is larger than estimated (225 functions, about 140 identity tests, five boss functions, five bonus levels, cameos). An identity test compares an actor with the glove's or the ball's address | The swap spike in phase 1 finds the shared state early; then system by system, each step checked by the determinism tests; measure the pace after the glove and ball, and re-plan. The spike is a prototype that swaps each extra player's state into the game's globals for that player's part of the frame. g64coopdx later kept the swap as its design ([roadmap.md](roadmap.md#phase-1-two-gloves-offline)) |
| 2 | The frame limits for 16 gloves: object matrices (256, peak 158 with one idle glove), the display list's per-task check (the game drops a task of 20,000 bytes or more), vertices (800) | Measure with bots and 16 players in phase 3; raise them on the game's heap or by growing the globals |
| 3 | The logic reads sound state (a Samtex enemy's sound handle; sounds that stop by camera distance) | List every read of sound state by the logic; split the logical part from the output in phase 2 (game change 12) |
| 4 | Determinism holds on the paths the tests didn't reach (rope swinging, rare enemies, the bosses with many players) | The bots in phase 3 run every level on Linux and Windows; every session checks hashes, so a break shows at once with the frame |
| 5 | Wine isn't Windows; a different CPU or a Windows update could change a math function | Run the cross-system test on real Windows before release; the game's math avoids the C library at whole frames, but check each libm call the logic makes |
| 6 | A level start isn't fully set by a small start record. Entering a level that others play, late join and repair all assume it | The experiment of game change 9 in phase 1: build a level from a record and compare it with the level reached by play; fallback: a player joins a level only when it starts, and others wait for a new instance |
| 7 | Rollback corrections look bad for remote balls with many players | Smooth the drawing of remote players; tune the input delay; measure at 100 to 200 ms |
| 8 | Lua scripts break determinism in ways the sandbox misses | Hash Lua's memory arena with the game; the harness runs every bundled script under rollback |
| 9 | The copy drifts from weemitt, and a fix made there is missed | Keep co-op changes in their own functions and files where possible; review weemitt's log at each phase's end and take what helps |
| 10 | Legal: the published builds contain decompiled code (decided: builds are published); sharing edited vanilla levels | Never ship ROM data; weemitt needs a license before a release; edited vanilla levels travel only as diffs against the player's own ROM |
| 11 | Abuse: hostile players, crafted packets, crafted packs | Inputs-only model; strict parsers with fuzzing; prompts and hashes for packs; host moderation |
| 12 | The services cost money and time to run | Direct connection first; the lobby and relay in phase 4, or never if the owner prefers |
| 13 | 16 players' inputs mispredicted often enough that rollbacks get deep | Measure in the harness; an input delay of 1 to 2 frames; the host's deadline bounds the depth |
| 14 | Session events (a level done, a hub door) reach different instances at different frames, or differently on two machines | Events enter an instance only as confirmed inputs at a frame the host picks; the harness checks every instance's hashes |
| 15 | Players joining and leaving a running level is new game logic: a glove and ball appear or vanish at a frame | Game change 10; the harness joins and leaves players at random frames |

## Terms

| Term | Meaning |
| --- | --- |
| The owner | The person who owns g64coopdx, weemitt, gloveworld and Glovebox, and decides for the project |
| The discovery | The study this report describes. It measured and read; it built none of the feature |
| Baseline | Recorded runs that a new build is checked against, frame by frame. weemitt's is made from a fixed earlier commit by `tools/regress.py` (49 runs). g64coopdx records its own, and records it again on purpose when a change is meant to alter the game |
| Hub and world | The game's levels are grouped in worlds: the hub (whose sections lead to the worlds) and six worlds, each with levels, a boss and a bonus level |
| The library | weemitt built as `libglover.so` or `glover.dll`, which another program can load to run the game |
| Frame and update | A game frame is one step of the game's logic: 1/20 s of game time in a level. An update is one call that advances the game; at the original timing one update is one frame |
| Pack | A folder with one custom level, its script and its author's models, and no game data ([level-pack.md](level-pack.md)). The design in [mods.md](mods.md#the-level-pack) also allows a zip file |
| Lua | A small scripting language; mods are written in it ([mods.md](mods.md#the-lua-api)) |
| Glover | An N64 platform game. The player steers a glove (the **glove**, `hand` in the code) that walks, jumps and carries, rolls or throws a **ball**, and must bring the ball to each level's exit |
| Garib | The game's collectables; each level has a set number |
| Cameo | A short scripted scene inside a level |
| gloveworld | The matching decompilation of Glover: C that compiles back to the original ROM |
| weemitt | The native PC port of Glover, built from gloveworld's C |
| Glovebox | The owner's level editor for Glover |
| sm64coopdx | An online co-op version of the Super Mario 64 PC port, with Lua mods; studied here as a model |
| `port_ram` | weemitt's 24 MB block that stands for the N64's memory. Every global of the game sits at its N64 offset in it |
| Savestate | A copy of the game's whole state, which can be loaded back later |
| Brute forcer | A tool inside weemitt that searches for faster routes through levels. It has the cheapest savestates: after the first one it copies only the 4 KB pages that changed (**page tracking**) |
| Rollback | A network model. Each machine shows its own player at once and predicts the others' inputs. When the real inputs arrive and differ, it loads the savestate of the last frame it knew and runs the frames again |
| Host | The machine that runs the session: it admits players and confirms each frame's inputs. (In weemitt's own docs, "host" also means a program that loads weemitt's library.) |
| Confirmed frame | A frame whose inputs the host has fixed for every player |
| Instance | One simulation of one level, run on the inputs of the players in it |
| Start record | The small set of values from which any machine can build an instance's first frame with its own ROM |
| Input log | Every confirmed input of an instance, from its first frame |
| Session event | A change to the session's shared state (a level done, the world) that the host puts into an instance's confirmed inputs at a frame |
| Game change N | Row N of the list of changes g64coopdx makes to its copy of weemitt ([weemitt.md](weemitt.md#changes-to-the-game-code)) |
| Phase N | A phase of [roadmap.md](roadmap.md) |
| Original and Smooth modes | weemitt's two frame-rate modes. Original runs whole N64 frames and blends the drawing. Smooth runs the logic at any update length, which isn't exact across systems |
| NAT traversal, ICE, STUN, TURN | Ways for two machines behind home routers to reach each other. ICE tries the possible routes; STUN tells a machine its public address; a TURN server relays the traffic when nothing else works |
