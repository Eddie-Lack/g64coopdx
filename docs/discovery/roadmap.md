# Roadmap

Each phase ends in something playable or testable. Sizes are rough: weeks of focused work for one developer who
knows the code. Phase 3 is the least certain; its risk is named in its section. "Game change N" is row N of the list
of changes g64coopdx makes to its copy of weemitt ([weemitt.md](weemitt.md#changes-to-the-game-code)). Each phase ends
with a look through weemitt's log for fixes worth taking into the copy.

| Phase | Ends in | Size | Depends on |
| --- | --- | --- | --- |
| 0. The skeleton | The copy of weemitt in place; a 64-bit build with savestates; CI that runs the toy game's rollback tests | 2 to 4 weeks | game change 8 |
| 1. Two gloves offline | Two players, two controllers, one hub level, on one machine; a level started from a record | 4 to 6 weeks | 0; game changes 1, 2 (first part), 7 and 9 |
| 2. Rollback on a LAN | 2 to 4 players over direct UDP, in different levels of one world, with desync checks | 5 to 8 weeks | 1; game changes 3 (first part), 5, 10 and 12 |
| 3. The whole game in co-op | Every level, boss and bonus level, 8 players, the owner's rules | 8 to 14 weeks | 2; game changes 2 to 4 and 11 |
| 4. Online services | Lobby, NAT traversal, relay, encryption, chat, moderation, 16 players | 4 to 6 weeks | 2 (can run beside 3) |
| 5. Mods | Lua scripts and level packs from Glovebox, downloaded on join | 6 to 9 weeks | 2; game change 6; Glovebox's pack export |
| 6. Release | A Windows release, documentation, crash reports | 2 to 4 weeks | 3, 4, 5 |

In all: about 31 to 51 weeks with every phase done one after another, so roughly 7 to 12 months for one person
(INFERRED). Phase 4 can run beside phase 3, which shortens the calendar time but not the work. g64coopdx does not have
to prove that each change leaves the one-player game exactly as the original, which saves work in phases 1 and 3; keeping its own baseline and
taking weemitt's fixes by hand costs a little in every phase.

## Phase 0: the skeleton

- The repository layout of [repository.md](repository.md).
- The copy of weemitt, added with `git subtree` at a pinned commit, with `weemitt/SOURCE` naming it. Its first commit
  is weemitt unchanged, and weemitt's `tools/regress.py check` passes on it, with weemitt's own headless build. This
  checks the copy only; g64coopdx's own builds are 64-bit.
- g64coopdx's own baseline, recorded from that commit with weemitt's tools.
- The Makefile that builds the copy's game with its own flags and links it with g64coopdx's code.
- The toy game: a few hundred lines of deterministic C (bodies, inputs, a random generator) behind the same
  interface as the real game: step, save, load, hash.
- The rollback core against the toy: frame clock, input queues, prediction, savestates, confirmation by the host,
  hash checks. Loopback tests with simulated latency, loss and reordering, 2 to 16 clients.
- CI on Linux and Windows (MinGW, Wine).
- The 64-bit static build of the game, on Windows and Linux.
- Game change 8: the brute forcer's savestates at 64 bits. Windows first: build and test the engine there. Then Linux:
  accept the position-independent build's `.data.rel` sections. Full-copy savestates stand in until each works
  ([weemitt.md](weemitt.md#recommendation-b-as-a-vendored-copy)).
- A check that the Linux build's memory hashes equal the Windows build's for the same inputs.
- The MIT `LICENSE` and `THIRD_PARTY_NOTICES.md`.

Test: the toy's every client equals one offline run of the host's confirmed inputs, frame by frame, under every
simulated network.

Status (15 November 2026):

- Done: the copy and its `SOURCE`; g64coopdx's baseline (it equals weemitt's, 49 runs); the 64-bit builds and
  `make crossbuild` (Linux equals Windows under Wine in all 49 runs); the toy game, the rollback core and its loopback
  tests, also under Wine, with a fuzz target; game change 8 (all engine tests pass at 64 bits on Linux and on
  Windows under Wine).
- Done beyond the list: the real game behind the rollback core (`src/rollback/engine_game.c`). One host and three
  clients play level 13 over a lossy simulated network; every hash check passes and every client equals the offline
  run (`make rollback-game`, Linux and Wine). The hash is weemitt's memory hash, and the Linux and Windows programs
  give the same value for the same state.
- Left: the window programs as static builds (they link SDL2 from the system today). The folders `src/net`,
  `src/session` and the others come with the phases that fill them.

## Phase 1: two gloves offline

Status (15 November 2026): the swap spike is in (`weemitt/src/game/g64players.c`), and kept for now: it is the
fastest way to two gloves for the first online tests. Each extra player's glove, ball, camera, pad and their control
blocks live in a slot on the game's heap and are swapped into the globals for that player's part of the frame. The
other players' gloves and balls are drawn as proxy actors. Four gloves play the first hub level for 3,000 frames with
the same output and hashes on Linux and Windows (`make twoplayers`); with one player the game still equals the
baseline. Each player's view is drawn with that player swapped in, and the gloves after the first have colors of
their own. Known gap: enemies, collectables and puzzles see only player 1.

- Game change 1: a pad per player.
- Game change 7: an option that turns off every PC addition of weemitt's that the controller can reach, such as the
  cheat list in the pause menu.
- A spike first: two players by swapping the player's state in and out ([state-inventory.md](state-inventory.md#two-ways-to-make-the-game-multi-player)),
  in one hub level. It finds the shared state that breaks. It is thrown away.
- Game change 2, first part: per-player glove, ball and camera, indexed, with the second player's state on the game's
  heap. The glove's state machine, the ball and which glove holds it, the camera per player, and the collision code's
  tests of whether an actor is the glove (`== &hand`).
- g64coopdx draws every player's glove and ball from the local player's camera, and gives the second glove its own
  color.
- Game change 9: start a level from a record, and compare its memory with a level reached by play
  ([netcode.md](netcode.md#instances)). Every instance, every late join and every repair depends on it, so it is
  settled first. Done in part: a level reached by play differs (the camera carries over), so every machine builds
  every level from the power-on state (`port_game_boot`, `port_game_start_level`; `make levelstart`). The start
  record's carried state is still to come.

Playable: two people on one machine walk, roll and throw their balls in a hub level. Test: with one player the game
still matches g64coopdx's baseline, unless a change was meant to alter it; the same inputs for two players give the
same hashes on Linux and Windows (Wine), every frame.

## Phase 2: rollback on a LAN

- The rollback core moves from the toy to the game. Savestates by the static build's sections (the brute forcer's
  engine), full copies first.
- Direct UDP with ENet; the host confirms inputs.
- Game change 12, the sound split: the logical sound state apart from its output, so a rollback does not replay
  sounds ([netcode.md](netcode.md#sound-and-rollback)). Done: the sound table, the reader and the real sound player
  in a second copy of the library.
- Desync checks, with game change 5 (a cheaper state hash): hashes of confirmed frames on a worker thread; on a
  mismatch, a rebuild from the level's start and a report.
- Instances: one per occupied level; the host's reference copy of each in a worker process; entering a level that
  others play by replaying its input log; leaving a level; session events at confirmed frames; the one-world rule at
  the hub's portals ([netcode.md](netcode.md#instances)). Done in part (src/client/instances.c): instances, the
  host's reference copies (in one process), joining by the input log, leaving by an exit, and progress into running
  instances as session events. Still to come: the one-world rule.
- Game change 10: players who join and leave a running level at a frame.
- Game change 3, first part: the hooks that instances need (level start and end, a player leaving a level, session
  events).
- The headless harness: N clients and a host in one run on loopback, with recorded inputs per player and a simulated
  network.

Playable: 2 to 4 people on a LAN or by IP, together or apart, in the hub level of phase 1 and the other hub sections
that work with two gloves by then. Test: the harness, at 0 to 200 ms latency and 0 to 5% loss, with players moving
between levels at random; every client's hash equals the host's copy of its instance.

## Phase 3: the whole game in co-op

Game changes 2 (per-player state) and 3 (hooks for the session layer), system by system, each step checked by the
harness at N players and by the determinism tests:

1. Enemies' targets: the nearest glove or ball; bullets; enemies holding a glove or a ball. Done: before an enemy's
   or a bullet's update, the player it looks at is swapped into the globals (weemitt/src/game/g64players.c,
   tests/rom/enemies.c). An enemy updates when any player's camera is near it. Left: bosses (item 6).
2. Puzzles: "any glove", "any ball", "any camera"; the records of which glove or ball just touched a puzzle's
   object, per player.
   Done in part: a puzzle's condition on the player is tested for each player, and "on" or "in" holds for any player
   (weemitt/src/game/puzzle.c, PuzzleEvaluateCondition; tests/rom/puzzles.c). Platforms that wait for a ball or a
   glove, or face one, look at every player. Vents need no change: they fire on timers, and their bullets look for
   players. A camera zone (a puzzle whose actions only move the camera) is each player's own: each player runs it
   with their own copy of its state. Winds, fans, water, spikes and buzzers act on every player's glove and ball
   (g64players.c, "World hazards"; tests/rom/hazards.c). Left: buzzers and fans have no test of their own.
3. Garibs and pick-ups: the nearest player takes it; power-ups and spells per player.
   Done: the player whose glove or ball reaches a garib from the nearest takes it, and the players of a level share
   one count of garibs. The score, the health a garib gives and a power-up are the taker's own. The first player
   whose glove touches a pick-up takes it. A power-up's orbiters are drawn as proxies
   (weemitt/src/game/g64players.c, "Garibs and pick-ups"; tests/rom/collect.c). Left: the ball's and the glove's
   textures are shared, so a power-up that changes the ball's look changes every ball. The combo of garibs taken in
   quick succession is one for all players.
4. Game change 11, the owner's rules: no lives; respawn at the player's own checkpoint without restarting the
   enemies; the ball's real owner, the host's pickup rule, a lost ball killing its owner.
   Done in part: online, each player's checkpoint is the gate their own ball crossed, and each respawns there with
   their own countdown; a respawn after a death restarts no enemies; a death takes no life
   (weemitt/src/game/g64players.c, "Respawns"; tests/rom/respawn.c). Each player's shadow altitude is measured where
   they are, so a fall into nothing kills every player. The pickup rule "own ball only" holds as the game is built:
   a glove touches only its own ball, so a lost ball kills its owner. Left: the host's other choice, "any ball",
   which needs a glove to hold another player's ball.
5. Exits and the hub: each player exits alone, the ball left behind, world doors. Done in part: each player exits
   alone, and the cave's crystal scene is each player's own (weemitt/src/game/g64players.c, tests/rom/cave.c).
6. Bosses (six; five have their own code in `boss.c`) and bonus levels (six; five have their own code).
   Done in part: the count-in of a timed level holds every player. The Pirates boss's wall holds every glove and
   ball. In the Prehistoric boss's level each player has the camera's stand, the entry area's camera and the strip
   where the glove lets go of the ball, and the boss deals with the player whose ball hit it or the nearest
   (weemitt/src/game/boss.c; tests/rom/bosses.c). In the Carnival boss's level each player has the turntable's
   push and the circling camera, the pie goes for the player the boss looks at, any player's ball sets the traps
   off, and the bomb's blast reaches every glove and ball. The other players are made once a level's opening cameo
   has ended: a copy taken during a cameo kept the cameo's camera. In the Fortress boss's level each player has the
   camera over the arena's middle and can stun the boss with a slam; the boss goes for the platform of the glove
   nearest to it; every glove hurts on a charged platform's edge and near the charged boss, and the stomp throws
   every glove and ball. Who has the controls (the camera, the glove or the ball) is each player's own. In the
   Fortress, Prehistoric and Atlantis bonus levels each player has the level's per-player part, and online a player
   wins or loses a bonus level alone: the others play on (weemitt/src/game/bonus.c; tests/rom/bonus.c). The bonus
   clock's end still takes everyone. An enemy's slam ring and a quake reach every player, and a player's own slam
   ring hurts no other player (footprint.c, quake.c). Every player has the Atlantis boss's camera and the first
   space boss's circling camera, with their own camera height; the robot's foot squashes every glove and bursts
   every ball under it; any player can carry their ball to the robot's platform, and online that exit takes the
   players at once (nmeprocess2.c; these have no test of their own). In the last space boss's level each player has a turret of their own: aim, camera, shield
   and shots (boss.c; tests/rom/bosses.c). Every glove is held at one place there, so the boss takes the living
   players in turn, 64 frames each. Left: that level's game over, which is one player's; the shield's glow and
   the crosshair's fade, which follow the first player on every screen; a cameo during the fight, which moves the
   first player's camera only. In the Carnival bonus level every player sits on the one cannon with its camera:
   the stick pushed the farthest turns it and any player's A fires it (bonus.c; tests/rom/bonus.c). The space bonus
   level has no code of its own for the player: the changes above cover it. The Carnival
   boss's pie and bomb, the Fortress boss's slam, pain and stomp and the slam rings have no test.
7. Cameos: which glove they move.
   Done: a cameo plays for the player who set it off, when a puzzle's condition on a player started it. Three
   cameos in the game start that way: one in the Atlantis hub and two in level 15. Every other cameo plays when a
   level starts, for the first player. The cameo's player is in the globals while it starts and steps, so the
   script moves their glove and camera. The other players are held and cannot be hurt while it plays, and they
   see its camera (cameo.c, puzzle.c, g64players.c, "Cameos"; tests/rom/cameos.c). The other players are made only
   when no cameo plays. In the control room, whose scene plays from a recording of the first player, the other
   players have no glove and watch through the first player's camera. Left: hiding the other gloves during a
   cameo. A cameo whose player leaves goes on with the first player's glove.
8. Pause and tips as local overlays. Pause is done: each player has their own menu. The look-around mode is each
   player's own too: their view turns, and it goes back to where it looked (tests/rom/lookaround.c).
   Tips are done: online a tip does not stop the level. Any player opens one with B at the tip, and the two tips
   that the game opens by itself open for every player. The reader's glove gets no input and nothing hurts them;
   their A turns the page or closes the tip, and they stay held until they let go of it. What each player reads is part of the game's state, by player number,
   so a rollback keeps it. The window draws the reader's box from the local game's memory: the text never crosses
   the network (weemitt/src/game/g64players.c, "Tips"; tip.c; src/client/netplay.c; tests/rom/tips.c). The game's
   own box, with its font, is not drawn online: the window's box has the text in the window's font, and it names
   the buttons in words where the game's font has a sign for them.
9. Game change 4: frame limits raised for 16 gloves and balls (matrices, the display list, vertices, sphere hits).
   Done: with 16 players every level runs 1500 frames. The peaks then are 596 object matrices (the Atlantis hub;
   one player needs up to 212), 1489 heap blocks (one player: 1146) and a display list task of 31320 bytes (one
   player: about 14000). The N64 game's limits were 256 matrices, 1280 blocks and 20000 bytes. From about 6 players
   the matrices ran past their table into the display list, and at about 13 the Atlantis hub ran out of blocks.
   Now a frame has room for 1024 matrices (objectMtxMore, weemitt/include/display.h), the dynamic heap blocks move
   to a table of 2048 when they meet the static ones (weemitt/src/game/jalloc.c), and a frame's tasks may fill the
   whole display list buffer of 20000 commands, of which 16 players use 4196 (main.c). With one player nothing
   moves: the tables in the game's memory hold what they held before. The vertices (peak 606 of 800) do not
   depend on the number of players. The sphere hit table matters only for part frames, which the online game
   does not run (tests/rom/limits.c). The picture was looked at with 16 players in one level of the window
   program, a host and 15 joiners on one machine: the renderer read memory only up to the heap's end and drew the
   objects past the 256th with a stale matrix, which put a black shape over the screen. It now reads up to the end
   of port_win (PORT_PTR_END, weemitt/src/port/ptr32.h). That session ran 2000 frames with 3200 hash checks and no
   mismatch.

Playable: the whole game, start to end, with 8 players. Test: a scripted run of each level by N bots in the harness;
the matrix and list peaks measured with 16 players.

The test is `make bots` (tests/rom/bots.c): each of the 48 levels runs 1500 frames with 8 and with 16 players whose
sticks and buttons come from a seeded generator, on Linux and under Wine, and the two must print the same hashes. Its
first runs found two faults, both fixed. A player who died with a snowball came back with the snowball still
counted and a ball of size 0, and the ball's code divided by that size (platform.c). A player's thrown ball could be
freed while another player was in the globals, and its owner kept the pointer (debris.c, G64FakeballGone). The peaks
are in item 9.

Risk: the size. 225 functions and about 140 identity tests ([state-inventory.md](state-inventory.md)). The range is
wide because each boss and each bonus level has its own code.

## Phase 4: online services

- The lobby and signaling server (`server/`), written from scratch: sessions, ICE candidates, TURN credentials,
  version filtering.
- libjuice for ICE; libsodium for encryption.
- The relay: Cloudflare's managed TURN service, tested with libjuice first; coturn on a server as the fallback
  ([hosting.md](../hosting.md)).
- The lobby deployed where [hosting.md](../hosting.md) says; its placeholder is already in `server/`.
- An HTTPS client in the game for the lobby: the system's own on Windows (WinHTTP), libcurl on Linux (INFERRED;
  chosen in this phase).
- The handshake: protocol, build id, ROM hash, packs, settings ([netcode.md](netcode.md#the-handshake)).
- Host and join screens, the lobby browser, the player list, chat, kick and ban with a confirm step, moderators,
  names and colors.
- Fuzz targets for every parser.

Playable: up to 16 people from home networks (the design limit; the promise stays 8 until phase 3's 16-glove
measurement passes). Test: the harness through the relay; fuzzing in CI; a test with real
machines on two networks.

## Phase 5: mods

- Lua 5.4 vendored, its arena in the savestate, the sandbox, the deterministic math, the API v1 with generated docs
  ([mods.md](mods.md#the-lua-api)).
- Game change 6: levels and banks from buffers, more bank slots, rotation and scale on every object type.
- Glovebox: Export level pack; imported models in banks of their own.
- Packs by hash, a prompt before download, path checks, a cache.

Playable: a session on a custom Glovebox level with a script, joined by players who did not have the pack. Test: the
pack reader's fuzz target; a script's state equal on every client after rollbacks; a level built from scratch loads
and plays.

## Phase 6: release

- A 64-bit Windows release, with SDL2 and the notices.
- Crash reports that hold no game memory.
- Player docs in the owner's style.
- The name ([open-questions.md](open-questions.md)).

## Game changes by phase

| Phase | Game changes ([weemitt.md](weemitt.md#changes-to-the-game-code)) |
| --- | --- |
| 0 | 8 (64-bit savestates) |
| 1 | 1 (pads), 2 (per-player state, first part), 7 (PC additions off), 9 (a level from a record) |
| 2 | 3 (hooks, first part), 5 (cheap hash), 10 (players join and leave a level), 12 (the sound split) |
| 3 | 2 (the rest), 3 (the rest), 4 (frame limits), 11 (the owner's rules) |
| 5 | 6 (level packs) |
