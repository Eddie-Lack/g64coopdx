# Roadmap

Each phase ends in something playable or testable. Sizes are rough: weeks of focused work for one developer who
knows the code, and they carry the uncertainty named in each phase. "Upstream" means a change in weemitt, behind a
player count or a hook, with `regress.py check` saying "same as the baseline" at one player.

| Phase | Ends in | Size | Depends on |
| --- | --- | --- | --- |
| 0. The skeleton | A build and CI that run the toy game's rollback tests | 1 to 2 weeks | the copyright holder for the MIT license |
| 1. Two gloves offline | Two players, two controllers, one hub level, on one machine; a level started from a record | 5 to 7 weeks | 0; upstream items 1 to 3 and 11 |
| 2. Rollback on a LAN | 2 to 4 players over direct UDP, in different levels of one world, with desync checks | 5 to 8 weeks | 1; upstream item 12 |
| 3. The whole game in co-op | Every level, boss and bonus level, 8 players, the owner's rules | 9 to 16 weeks | 2; upstream items 3 to 5 and 13 |
| 4. Online services | Lobby, NAT traversal, relay, encryption, chat, moderation, 16 players | 4 to 6 weeks | 2 (can run beside 3) |
| 5. Mods | Lua scripts and level packs from Glovebox, downloaded on join | 6 to 9 weeks | 2; upstream item 8; Glovebox's pack export |
| 6. Release | A Windows release, documentation, crash reports | 2 to 4 weeks | 3, 4, 5 |

In all: about 32 to 52 weeks, so roughly 8 to 12 months for one person (INFERRED; the largest unknown is phase 3).

## Phase 0: the skeleton

- The repository layout of [repository.md](repository.md), weemitt as a submodule, the Makefile that builds weemitt's
  game with weemitt's flags.
- The toy game: a few hundred lines of deterministic C (bodies, inputs, a random generator) behind the same
  interface as the real game: step, save, load, hash.
- The rollback core against the toy: frame clock, input queues, prediction, savestates, confirmation by the host,
  hash checks. Loopback tests with simulated latency, loss and reordering, 2 to 16 clients.
- CI on Linux and Windows (MinGW, Wine).
- The 32-bit static build of weemitt's game; a check that a Linux build's memory hashes equal the Windows build's,
  and the offset pointer representation if they do not
  ([weemitt.md](weemitt.md#recommendation-c-with-the-brute-forcers-way-of-building)).
- The MIT `LICENSE` and `THIRD_PARTY_NOTICES.md`.

Test: the toy's every client equals one offline run of the host's confirmed inputs, frame by frame, under every
simulated network.

## Phase 1: two gloves offline

- Upstream 1: a documented build entry for outside programs.
- Upstream 2: a pad per player.
- A spike first: two players by swapping the player's state in and out ([state-inventory.md](state-inventory.md#two-ways-to-make-the-game-multi-player)),
  in one hub level. It finds the shared state that breaks. It is thrown away.
- Upstream 3, first part: per-player glove, ball and camera, indexed, outside `port_ram`. The glove's state machine,
  the ball and its pairing by owner, the camera per player, the collision identity tests.
- g64coopdx draws every player's glove and ball, the local camera's view, and a second glove's color.
- Upstream 11: start a level from a record, and compare its memory with a level reached by play
  ([netcode.md](netcode.md#instances)). Every instance, every late join and every repair depends on it, so it is
  settled first.

Playable: two people on one machine walk, roll and throw their balls in a hub level. Test: `regress.py` at one
player; the same inputs for two players give the same hashes on Linux and Windows (Wine), every frame.

## Phase 2: rollback on a LAN

- The rollback core moves from the toy to the game. Savestates by the static build's sections (the brute forcer's
  engine), full copies first.
- Direct UDP with ENet; the host confirms inputs.
- Sound: the split of logical sound state from its output, so a rollback does not replay sounds
  ([netcode.md](netcode.md#sound-and-rollback)). Upstream.
- Desync checks: hashes of confirmed frames on a worker thread; on a mismatch, a rebuild from the level's start and
  a report.
- Instances: one per occupied level; the host's reference copy of each in a worker process; entering a level that
  others play by replaying its input log; leaving a level; session events at confirmed frames; the one-world rule at
  the hub's portals ([netcode.md](netcode.md#instances)).
- Upstream 12: players who join and leave a running level at a frame.
- The headless harness: N clients and a host in one run on loopback, with recorded inputs per player and a simulated
  network.

Playable: 2 to 4 people on a LAN or by IP in the hub and the levels that phase 1 made work, together or apart. Test:
the harness, at 0 to 200 ms latency and 0 to 5% loss, with players moving between levels at random; every client's
hash equals the host's copy of its instance.

## Phase 3: the whole game in co-op

Upstream 3 and 4, system by system, each step proven by `regress.py` at one player and by the harness at N players:

1. Enemies' targets: the nearest glove or ball; bullets; enemies holding a glove or a ball.
2. Puzzles: "any glove", "any ball", "any camera"; edge records per player.
3. Garibs and pick-ups: the nearest player takes it; power-ups and spells per player.
4. Upstream 13, the owner's rules: no lives; respawn at the player's own checkpoint without restarting the enemies;
   the ball's real owner, the host's pickup rule, a lost ball killing its owner.
5. Exits and the hub: each player exits alone, the ball left behind, world doors.
6. Bosses (5 with code, `boss.c`) and bonus levels (5).
7. Cameos: which glove they move.
8. Pause and tips as local overlays.
9. Upstream 5: frame limits raised for 16 gloves and balls (matrices, the display list, vertices, sphere hits).

Playable: the whole game, start to end, with 8 players. Test: a scripted run of each level by N bots in the harness;
the matrix and list peaks measured with 16 players.

Risk: the size. 225 functions and about 140 identity tests ([state-inventory.md](state-inventory.md)). The range is
wide because each boss and each bonus level has its own code.

## Phase 4: online services

- The lobby and signaling server (`server/`), written from scratch: sessions, ICE candidates, TURN credentials,
  version filtering.
- libjuice for ICE; coturn as the relay; libsodium for encryption.
- The handshake: protocol, build id, ROM hash, packs, settings ([netcode.md](netcode.md#the-handshake)).
- Host and join screens, the lobby browser, the player list, chat, kick and ban with a confirm step, moderators,
  names and colors.
- Fuzz targets for every parser.

Playable: 16 people from home networks. Test: the harness through the relay; fuzzing in CI; a test with real
machines on two networks.

## Phase 5: mods

- Lua 5.4 vendored, its arena in the savestate, the sandbox, the deterministic math, the API v1 with generated docs
  ([mods.md](mods.md#the-lua-api)).
- Upstream 8: levels and banks from buffers, extra bank lists, rotation and scale on every object type.
- Glovebox: Export level pack; imported models in banks of their own.
- Packs by hash, a prompt before download, path checks, a cache.

Playable: a session on a custom Glovebox level with a script, joined by players who did not have the pack. Test: the
pack reader's fuzz target; a script's state equal on every client after rollbacks; a level built from scratch loads
and plays.

## Phase 6: release

- A Windows release (32-bit, or 64-bit once upstream item 10 is done), with SDL2 and the notices.
- Crash reports that hold no game memory.
- Player docs in the owner's style.
- The name and the license's copyright holder ([open-questions.md](open-questions.md)); weemitt's license, which a
  release also needs.

## Upstream items by phase

| Phase | weemitt items ([weemitt.md](weemitt.md#upstream-changes-weemitt-needs)) |
| --- | --- |
| 1 | 1 (build entry), 2 (pads), 3 (per-player state, first part), 9 (PC additions off), 11 (a level from a record) |
| 2 | 6 (cheap hash), 12 (players join and leave a level), the sound split |
| 3 | 3 (the rest), 4 (hooks), 5 (frame limits), 13 (the owner's rules) |
| 5 | 8 (level packs) |
| later | 7 (savestates in the library), 10 (64-bit savestates) |
