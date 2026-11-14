# Discovery: online co-op for Glover

This is the report of the discovery phase for g64coopdx: an online multiplayer version of Glover (N64, USA) on PC,
with mods, in the spirit of sm64coopdx. Nothing here is the feature itself. The owner's decisions so far are listed in
[open-questions.md](open-questions.md#decided). In short: 16 players by design, 8 promised; each player a glove with
its own ball; Windows first; Lua and custom levels in the first version; players may be in different levels of the
same world; no lives; each player exits alone.

## Summary

**Is it feasible?** Yes. The PC port, weemitt, already has the two properties that the hardest part of a network game
needs, and this discovery measured both. Its game logic gives the same result, bit for bit, on every run and on every
build tested: Linux and Windows, 32-bit and 64-bit (147 comparisons of trace and memory hash over all 48 levels and
the attract mode, no difference; 49 more with the real sound player gave the same traces). And its savestates are cheap: a state of about 5 MB saves or loads in 0.02 ms (0.4 ms as a plain
copy), while a game frame lasts 50 ms. The work is in the game's code, which assumes one player throughout: 225 of its
1,234 functions touch the glove or the ball.

**The recommended design.** The session is in one world at a time. Every player has a glove and a ball, and may be in
any level of that world, its boss, its bonus level or the hub. A portal to another world opens when every player is
back in the hub. Each machine shows its own player's camera. There are no lives: a player who dies respawns at the
last checkpoint their own ball passed. Each player leaves a level alone by bringing a ball to the exit. The host
chooses whether a glove may pick up only its own ball or any ball; a lost ball always kills its real owner.
[Details](#recommended-design).

**The recommended network model.** Rollback with host-confirmed inputs, one simulation per occupied level (an
**instance**). Every machine runs its own level's instance on the inputs of the players in it. It shows its own player
at once, predicts the others, and steps back through savestates when the real inputs differ. The host decides each
frame's final inputs for every instance, so a slow player never stalls the rest. Only inputs and session messages
cross the network: a few bytes per player per frame, and no game data. A player who enters a level that others are
already playing replays that instance's inputs from its start. [netcode.md](netcode.md).

**How the project relates to weemitt.** g64coopdx is a separate program with weemitt as a pinned submodule. It
compiles weemitt's game code with weemitt's own flags, as weemitt's brute forcer does. Every change to the game's C
goes into weemitt, behind a player count that is inert at one player, so weemitt's `regress.py` keeps proving the
single-player game unchanged. g64coopdx holds the network, the sessions, the interface, Lua and the packs.
[weemitt.md](weemitt.md).

**The biggest risks.**

1. The size of the multi-player change to the game's code, above all the bosses, bonus levels and cameos, which
   each have their own code, and players who join and leave a running level.
2. Whether the frame limits (256 object matrices, the display list) can be raised far enough for 16 gloves.
3. The sound: the logic reads sound state, so it must be split from the sound's output before rollback works well.
4. The instances: entering a level by replaying its inputs, and session events (a level completed, a save bit) that
   must reach every instance at the same frame on every machine.

**Rough size.** About 32 to 52 weeks of focused work for one developer, in seven phases that each end in something
playable or testable. [roadmap.md](roadmap.md).

## The documents

| File | Question | Contents |
| --- | --- | --- |
| [sm64coopdx.md](sm64coopdx.md) | 1 | What sm64coopdx is and how it works, with sources; what carries over |
| [weemitt.md](weemitt.md) | 2, 3 | Where the port stands; the options for building on it; the upstream changes; the experiments |
| [state-inventory.md](state-inventory.md) | 4 | Every single-player assumption in the game's code; the size of the refactor; the frame's limits |
| [netcode.md](netcode.md) | 6, 7 | The network models, the measurements, the recommendation; transport, services, the handshake |
| [mods.md](mods.md) | 8 | Lua, custom levels and models from Glovebox, sharing mods without game data |
| [repository.md](repository.md) | 12 | The layout, build, CI, license, ROM rules, a draft CLAUDE.md, names |
| [roadmap.md](roadmap.md) | | Phases, sizes and dependencies |
| [open-questions.md](open-questions.md) | | The decisions only the owner can make |
| [experiments/](experiments/) | | The scripts behind the measurements |

This file answers questions 5, 9, 10, 11 and 13.

## How this was done

- Read: weemitt's CLAUDE.md and docs (architecture, delta time, interpolation, the library, the host world, the
  64-bit port, testing, the studio), the brute forcer's docs, Glovebox's technical.md, gloveworld's docs, and
  sm64coopdx's repository (a shallow clone at commit `8cd6e59`, read only).
- Ran: weemitt's headless builds, 32-bit and 64-bit; its library for Linux and Windows (under Wine 9); the brute
  forcer's benchmark; a clang scan of every use of the player globals; gdb probes of the frame limits; a tiny host
  program on the library.
- Four research agents worked in parallel (sm64coopdx, the state inventory, mods and Glovebox, network libraries).
  Their claims were spot-checked against the sources, and their notes are the base of the detail sections.
- Marks: **VERIFIED** means read in a named source or measured here; **INFERRED** means concluded from that;
  **UNKNOWN** means not known yet, with a way to settle it.
- The ROM (Glover USA, SHA-1 `270be17b3c8da9b88a7b99c2a545b0bce16837f4`, the same as gloveworld's `NGVE.sha1`) stayed
  outside every repository. Memory dumps were deleted after use. Nothing from the ROM is in this report.
- Nothing in weemitt, gloveworld or Glovebox was changed.

## Recommended design

The owner's decisions shape this section: each player a glove with its own ball; players may be in different levels
of the same world; no lives; each player exits alone; ball pickup is a host option; respawn at the player's own
checkpoint. What remains open is in [open-questions.md](open-questions.md).

### Worlds, levels and instances

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
  is a separate thing ([state-inventory.md](state-inventory.md#what-the-owners-rules-add)).
- A host option, for the whole session: **own ball only** (a glove picks up only its own ball) or **any ball** (any
  glove may take any free ball).
- A ball that is lost (thrown off a cliff, burst, drowned) kills its real owner, whoever held it last. With any-ball
  pickup this lets one player get another killed; it is part of the rule.
- Ball changes from power-ups (bowling, crystal, ball-bearing and the others) apply to the ball that touched the
  power-up, or to the ball of the player who took it when a glove takes it.
- Gloves and balls do not collide with each other in the first version: the game has no such physics, and new
  physics would be new game logic to prove (open question).
- Any ball works for puzzles: a switch a ball presses, a gate a ball falls through.

### The camera

- Every player has a camera, simulated in the instance, because the glove's controls, the enemies' update range,
  some puzzles and the sounds depend on it ([state-inventory.md](state-inventory.md#what-the-network-model-adds-to-the-inventory)).
- Each machine draws from its own player's camera.
- A boss's or a cameo's scripted camera applies to every player in that instance.

### Death and respawn

- **No lives.** A death never costs a life and never ends the game. The game already has a cheat bit that stops the
  loss of lives (bit 8 of `cheatActivated`, `weemitt/src/game/hand.c:3804`); the co-op rule goes further and turns the
  game over path off entirely.
- A player who dies respawns at **the last checkpoint their own ball passed** in that level
  (`handControl.checkpoint` is already per glove; a gate fires when a ball falls through it,
  `weemitt/src/game/platform.c:1513-1536`). Before any checkpoint, at the level's start point.
- The other players play on. The game's `RestartPlayer` restarts every enemy (`platform.c:1550`); the co-op respawn
  must restart only the dead player's glove, ball and camera.
- The gates' look (touched or not) is shared in the instance; which gate a player respawns at is per player.

### Level exits

- **Each player exits alone**: a player who brings a ball to the exit leaves the level, and the others play on.
  The game's own exit rule decides where the player goes next (`ExitLevel`, `weemitt/src/game/game.c:3716`), usually
  the world's hub section.
- With any-ball pickup, a player may carry another player's ball to the exit. The proposal: the ball's real owner
  then gets a new ball at their checkpoint. This detail is open ([open-questions.md](open-questions.md)).
- The exit timers (`levelExitTimer` and the others) become per player.
- The "left behind" ball in the hub (`ballLeftBehind`) is kept per player.

### Garibs and progress

- Within an instance, a garib taken by anyone counts for everyone in it; the nearest glove or ball takes it.
- The level's "all garibs" bit is set when one instance's players have them all.
- Score per player, for the end screen.
- Progress is the host's save. A level counts as done when any player exits it. Clients never write their own saves
  from a session; a session can start from any of the host's save slots. sm64coopdx does the same with the host's
  save ([sm64coopdx.md](sm64coopdx.md), 2.10).
- Progress made in one instance reaches the others as a session event at a confirmed frame (for example, a hub door
  that opens when a level is done) ([netcode.md](netcode.md#session-events)).

### Bosses, bonus levels, cameos

- A boss level is an instance like any other. A boss targets the nearest player in it, as the enemies do. Its
  health is shared by the players fighting it; any player's ball hurts it. The five bosses with code
  (`weemitt/src/game/boss.c`) each need a pass.
- Bonus levels: the players in the bonus instance play at once, with its shared timer. Losing a life in a bonus level
  ends it today (`hand.c:3799-3803`); with no lives, a death in a bonus level sends only that player out.
- Cameos (scripted scenes) run for everyone in the instance. They move the glove of the player who triggered them;
  the others are held still and hidden while the cameo runs.

### Pause and menus

- The pause menu is a local overlay; the game goes on. The host may stop the game for everyone.
- Signs (tips) show as a local overlay without stopping anyone.
- The Enhancements and Cheats are session settings that the host sets. weemitt's cheat list in the pause menu is off
  online (upstream item 9).

## Where the new state lives

weemitt's rule: the game's data lives at its N64 offsets in `port_ram`; the type or size of a global stored there
does not change; new state goes in new globals outside it.

**Recommendation: keep the rule in g64coopdx.**

- Player 0 keeps the N64 addresses: `hand` at 0x802902D8, `ball` at 0x8029F978, and the others
  ([state-inventory.md](state-inventory.md)). Players 1 and up live in new arrays outside `port_ram`. A table of
  pointers per player (`players[i].hand` and so on) points at the N64 globals for player 0.
- Larger frame buffers (the object matrices, the display list, the vertices) also go outside `port_ram`, and the
  game's code points at them when more than one player plays.
- Why: with one player, nothing in `port_ram` changes, so weemitt's `regress.py` hashes, its comparison with the N64
  in an emulator and the brute forcer's sync test keep working. The savestate already covers the game's variables
  outside `port_ram` (`weemitt/bruteforce/docs/engine.md`, "Savestates"), so rollback sees the new state too.
- What it costs: code that treats player 0 apart. A pointer per player hides that from most of the game's code.
- What breaking the rule would buy: simpler arrays. Not worth losing the tests that prove the game exact.

## Player presentation and the interface

| Need | Plan |
| --- | --- |
| Several gloves on screen | Every glove and ball is drawn by the game's own draw path, which every machine runs the same way ([state-inventory.md](state-inventory.md#what-the-network-model-adds-to-the-inventory)) |
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
| `tools/regress.py` (weemitt) | the logic at one player, every frame's state and memory hash against a baseline | every upstream change; CI on the pinned weemitt with the ROM |
| `tools/dtcheck.py` (weemitt) | the Smooth mode | single-player only; online play does not use it |
| `make libcheck` (weemitt) | the library on Linux and Windows | if g64coopdx moves to the library |
| `tests/sync.py` (weemitt's brute forcer) | the engine against the ROM in an emulator | after game changes, at one player |
| `--record` and `--replay` (weemitt) | a run's inputs and checks | the model for the session's input log |
| `--trace`, `--hash-every` | the per-frame state | the desync reports |
| this report's `determinism.py` | runs and builds compared | grows into the N-player determinism test |

New tests:

1. **The toy game.** A small deterministic game behind the same interface lets every rollback, network and session
   test run in CI without a ROM ([repository.md](repository.md#ci)).
2. **The session harness.** A host and N clients in one run, on loopback, each fed a recorded input file, with a
   simulated network (latency, jitter, loss, reordering, duplication). The check: every client's state hash at every
   confirmed frame equals the host's, and equals one offline run of the host's confirmed input log. Several games in
   one process work (one per copy of the library file, [weemitt.md](weemitt.md#a-separate-program-on-the-library)),
   or one process per client.
3. **Across systems.** The same session's input log replayed on Linux and on Windows (Wine in CI) gives the same
   hashes.
4. **Instances, late join and repair.** Players who move between levels, a client that enters a running level at
   frame F, and a client whose state is broken on purpose, all end equal to the host's copy of each instance.
5. **Fuzzing.** Every message parser, the handshake and the pack reader.
6. **Bots.** Scripted inputs that play each level for N players, for the frame-limit peaks and long runs.

## Risks and unknowns

| # | Risk or unknown | How to resolve it |
| --- | --- | --- |
| 1 | The multi-player refactor is larger than estimated (225 functions, about 140 identity tests, five boss functions, five bonus levels, cameos) | The swap spike in phase 1 finds the shared state early; then system by system with `regress.py`; measure the pace after the glove and ball, and re-plan |
| 2 | The frame limits for 16 gloves: object matrices (256, peak 158 with one idle glove), the display list's per-task check, vertices (800) | Measure with bots and 16 players in phase 3; raise them outside `port_ram` |
| 3 | The logic reads sound state (a Samtex enemy's sound handle; sounds that stop by camera distance) | List every read of sound state by the logic; split the logical part from the output in phase 2 |
| 4 | Determinism holds on the paths the tests did not reach (rope swinging, rare enemies, the bosses with many players) | The bots in phase 3 run every level on Linux and Windows; every session checks hashes, so a break shows at once with the frame |
| 5 | Wine is not Windows; a different CPU or a Windows update could change a math function | Run the cross-system test on real Windows before release; the game's math avoids the C library at whole frames, but check each libm call the logic makes |
| 6 | A level start is not fully set by a small start record. Entering a level that others play, late join and repair all assume it | The phase 1 experiment; fallback: a player joins a level only when it starts, and others wait for a new instance |
| 7 | Rollback corrections look bad for remote balls with many players | Smooth the drawing of remote players; tune the input delay; measure at 100 to 200 ms |
| 8 | Lua scripts break determinism in ways the sandbox misses | Hash the arena with the game; the harness runs every bundled script under rollback |
| 9 | Upstream changes slow down weemitt's own work | Keep each change inert at one player and small; the same owner decides the order |
| 10 | Legal: the published builds contain decompiled code (decided: builds are published); sharing edited shipped levels | Never ship ROM data; weemitt needs a license before a release; the level question is open ([open-questions.md](open-questions.md)) |
| 11 | Abuse: hostile players, crafted packets, crafted packs | Inputs-only model; strict parsers with fuzzing; prompts and hashes for packs; host moderation |
| 12 | The services cost money and time to run | Direct connection first; the lobby and relay in phase 4, or never if the owner prefers |
| 13 | 16 players' inputs mispredicted often enough that rollbacks get deep | Measure in the harness; an input delay of 1 to 2 frames; the host's deadline bounds the depth |
| 14 | Session events (a level done, a hub door) reach different instances at different frames, or differently on two machines | Events enter an instance only as confirmed inputs at a frame the host picks; the harness checks every instance's hashes |
| 15 | Players joining and leaving a running level is new game logic: a glove and ball appear or vanish at a frame | Upstream, as part of the per-player state; the harness joins and leaves players at random frames |
