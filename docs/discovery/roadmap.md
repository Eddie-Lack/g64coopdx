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

## Phase 1: two gloves offline

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
  settled first.

Playable: two people on one machine walk, roll and throw their balls in a hub level. Test: with one player the game
still matches g64coopdx's baseline, unless a change was meant to alter it; the same inputs for two players give the
same hashes on Linux and Windows (Wine), every frame.

## Phase 2: rollback on a LAN

- The rollback core moves from the toy to the game. Savestates by the static build's sections (the brute forcer's
  engine), full copies first.
- Direct UDP with ENet; the host confirms inputs.
- Game change 12, the sound split: the logical sound state apart from its output, so a rollback does not replay
  sounds ([netcode.md](netcode.md#sound-and-rollback)).
- Desync checks, with game change 5 (a cheaper state hash): hashes of confirmed frames on a worker thread; on a
  mismatch, a rebuild from the level's start and a report.
- Instances: one per occupied level; the host's reference copy of each in a worker process; entering a level that
  others play by replaying its input log; leaving a level; session events at confirmed frames; the one-world rule at
  the hub's portals ([netcode.md](netcode.md#instances)).
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

1. Enemies' targets: the nearest glove or ball; bullets; enemies holding a glove or a ball.
2. Puzzles: "any glove", "any ball", "any camera"; the records of which glove or ball just touched a puzzle's
   object, per player.
3. Garibs and pick-ups: the nearest player takes it; power-ups and spells per player.
4. Game change 11, the owner's rules: no lives; respawn at the player's own checkpoint without restarting the
   enemies; the ball's real owner, the host's pickup rule, a lost ball killing its owner.
5. Exits and the hub: each player exits alone, the ball left behind, world doors.
6. Bosses (six; five have their own code in `boss.c`) and bonus levels (six; five have their own code).
7. Cameos: which glove they move.
8. Pause and tips as local overlays.
9. Game change 4: frame limits raised for 16 gloves and balls (matrices, the display list, vertices, sphere hits).

Playable: the whole game, start to end, with 8 players. Test: a scripted run of each level by N bots in the harness;
the matrix and list peaks measured with 16 players.

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
- The name ([open-questions.md](open-questions.md)); weemitt's license, which a release also needs.

## Game changes by phase

| Phase | Game changes ([weemitt.md](weemitt.md#changes-to-the-game-code)) |
| --- | --- |
| 0 | 8 (64-bit savestates) |
| 1 | 1 (pads), 2 (per-player state, first part), 7 (PC additions off), 9 (a level from a record) |
| 2 | 3 (hooks, first part), 5 (cheap hash), 10 (players join and leave a level), 12 (the sound split) |
| 3 | 2 (the rest), 3 (the rest), 4 (frame limits), 11 (the owner's rules) |
| 5 | 6 (level packs) |
