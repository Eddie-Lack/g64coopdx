# The repository

This file covers question 12 of the discovery ([README.md](README.md#the-documents) lists the 13 questions):
g64coopdx's layout, build, CI, license, the ROM requirement, what may be distributed, a draft CLAUDE.md and candidate
names. The owner's decisions on these points are in [open-questions.md](open-questions.md#decided). README.md's
[terms](README.md#terms) define the words that this file uses without explaining them, such as the owner, the
discovery, the brute forcer, the baseline, an instance, a game change and a phase. **VERIFIED** means read in a named
source or measured here, and **INFERRED** means concluded from that ([README.md](README.md#method)).

The layout, the build's targets and the draft CLAUDE.md below are what the discovery proposed. The repository's own
`CLAUDE.md` says where things are now ("Where things go") and which commands exist ("Everyday work").

## Layout

```
g64coopdx/
  CLAUDE.md                 rules for agents (draft below)
  README.md                 for players
  THIRD_PARTY_NOTICES.md    the licenses of vendored libraries
  Makefile                  the build (below)
  weemitt/                  g64coopdx's copy of all of weemitt's tree (git subtree); co-op changes its game code
    SOURCE                  the weemitt commit the copy was last taken from
  src/
    session/                players, slots, the start record, the handshake, settings
    rollback/               the frame clock, input queues, prediction, confirmation, desync checks; it calls the
                            savestate engine in the copy (bruteforce/engine)
    net/                    the transport behind a table of functions: direct UDP, ICE, relay
    lobby/                  the client side of the lobby and signaling server
    ui/                     host and join screens, the player list, chat, name tags (ImGui, as weemitt's menu)
    lua/                    the Lua runtime's arena, the API, the sandbox
    packs/                  level packs: reading, checking, the cache, downloads
    platform/               the window, audio output and input devices, built on the copy's platform layer
  server/                   the lobby and signaling server (a separate program, in Go)
  third_party/              ENet, libjuice, libsodium, Lua 5.4: vendored sources at pinned versions
  tests/
    toy/                    a small deterministic game with the same interface as the real one, for CI without a ROM
    net/                    loopback tests with simulated latency, loss and reordering
    fuzz/                   fuzz targets for every message parser and the pack reader
    rom/                    tests that need the player's ROM; they skip without it
  tools/                    the test harness (several headless clients in one run), log readers
  docs/
    discovery/              this report
    hosting.md              where the services run
    design.md, netcode.md, lua-api.md, packs.md   the living docs, once work starts
```

Rules that the layout encodes:

- **Game changes stay in `weemitt/`.** Co-op's changes to the game's C are made in the copy
  ([weemitt.md](weemitt.md#recommendation-b-as-a-vendored-copy)). Everything else lives in g64coopdx's own folders,
  and talks to the game through hooks and per-player state that the copy adds.
- The network layer sits behind a table of functions, as sm64coopdx's `struct NetworkSystem` does
  ([sm64coopdx.md](sm64coopdx.md)). The direct mode and the lobby mode then share all other code. In the direct mode
  the host shares an address and port, and a joiner types it. In the lobby mode players find a session through the
  lobby server ([netcode.md](netcode.md#services)).
- The toy game stands in for the real one wherever a test does not need Glover itself.

## The build

- GNU Make, as weemitt and the brute forcer. The Makefile reads weemitt's flags and source lists from
  the copy's `weemitt/Makefile` (the brute forcer does this with `make -pn`).
- The game's files get weemitt's game flags. g64coopdx's own files get ordinary host flags. Lua gets the game's float
  flags (`-ffp-contract=off`, SSE2, no fast math) and is linked into the game's object. Its arena, the fixed block of
  memory that Lua allocates from, is then part of the savestate ([mods.md](mods.md#scripts-in-a-rollback-game)).
- Targets: `make` (Linux), `make windows` (MinGW-w64, the first target that matters), `make headless` (no window,
  the test build), `make server` (the lobby server), `make test`.
- **64-bit from the start**, for Windows and Linux (decided). The 64-bit Linux and Windows builds gave equal memory
  hashes in this discovery's tests.
- The brute forcer's savestates work at 64 bits on Linux and Windows (game change 8, done in phase 0:
  `make savestates`, and `weemitt/bruteforce/docs/engine.md`).
- No 32-bit builds.
- SDL2, as weemitt. Dependencies are vendored as source, never as prebuilt binaries. sm64coopdx commits prebuilt
  libraries (`lib/`), which hides their code and their licenses ([sm64coopdx.md](sm64coopdx.md), section 7).

## CI

CI cannot have the ROM: it may not be committed, and a copy in CI is game data in a third party's hands. So CI tests
what needs no ROM, and the rest runs where the ROM is.

| Where | What |
| --- | --- |
| CI on every push (GitHub Actions, Linux) | Build Linux and Windows (MinGW). Unit tests. The toy game's rollback tests on loopback with simulated latency, loss and reordering, 2 to 16 clients. Fuzz targets for a fixed time. The lobby server's tests |
| CI, Windows | The Windows build's unit and toy tests under Wine, as weemitt's `libcheck.py --wine` does, or on a Windows runner |
| Locally, with the ROM (`tests/rom/`, skip without it) | weemitt's `regress.py check` on the copy's first commit, then g64coopdx's own baseline; the multi-player determinism test (the same inputs for N players give the same hashes on Linux and Windows); a headless session of N clients on loopback whose every frame's hash equals the host's and equals one offline run of the confirmed inputs; late join; packs |
| A self-hosted runner that holds the owner's ROM | Not for now (decided); it would run the ROM tests on every pull request |

Glovebox's CI works the same way: the tests that need the ROM skip themselves (`Glovebox/.github/workflows/app.yml`).

## The ROM requirement

- Every player supplies the Glover (USA) ROM, SHA-1 `270be17b3c8da9b88a7b99c2a545b0bce16837f4`. The program reads it
  at start-up, as weemitt does, and accepts the `.z64`, `.v64` and `.n64` byte orders.
- The handshake compares ROM hashes, never ROM bytes.
- Nothing from the ROM is ever committed, built into a release, or sent over the network: no banks, textures,
  sounds, levels, memory dumps, savestates or recordings that hold memory.

## What may be distributed

| Thing | May be distributed? |
| --- | --- |
| g64coopdx's own source | Yes, under its license |
| The copy of weemitt in `weemitt/` | It is decompiled game C, from the owner's own weemitt. It is under weemitt's own MIT license (`weemitt/LICENSE`) |
| Vendored libraries | Yes, under their licenses (MIT, ISC, MPL-2.0, zlib), with their notices |
| Release builds | **Decided: yes.** They contain the game's code compiled from the decompilation, but no ROM data, as sm64coopdx's and other decompilation ports' builds do. Players supply their own ROM |
| The lobby server | Yes; the project runs it |
| Level packs made by players | Yes, when they hold no game data ([mods.md](mods.md#game-data-and-the-authors-work)) |
| The ROM, anything made from it, savestates, memory dumps | Never |

## License

Facts (VERIFIED):

- weemitt, gloveworld and Glovebox have no license file. gloveworld's README says its license "is not chosen yet";
  Glovebox's Cargo.toml names `THIRD_PARTY_NOTICES.md` in its `license-file` field.
- sm64coopdx has no license file, so no right to its code exists. g64coopdx does not use its code in any case.
- CoopNet, sm64coopdx's lobby server, is AGPL-3.0. g64coopdx does not use it.
- In the repository: Monocypher (BSD 2-Clause or CC0) in `third_party/monocypher`, for the session's encryption.
- The libraries recommended in [netcode.md](netcode.md) and [mods.md](mods.md): ENet MIT, libsodium ISC, libjuice
  MPL-2.0, Lua MIT, SDL2 zlib, Dear ImGui MIT (already in weemitt's `third_party/imgui`), coturn BSD-3-Clause (a
  separate server, and only the fallback relay: [hosting.md](../hosting.md)).

**Decided: MIT** for g64coopdx's own code.

- MIT fits every library above. MPL-2.0 asks only that libjuice's own files keep their license; use them unchanged.
- The `LICENSE` file comes with the first code (phase 0). Its copyright holder is "The g64coopdx contributors" (decided).
- Keep `THIRD_PARTY_NOTICES.md` from the first commit, as gloveworld and Glovebox do.
- g64coopdx's `LICENSE` covers its own code. The copy in `weemitt/` keeps weemitt's own MIT license
  (`weemitt/LICENSE`), and `THIRD_PARTY_NOTICES.md` names it.

## Draft CLAUDE.md

```markdown
# Guide for agents

g64coopdx is an online co-op version of Glover (N64, USA, NGVE) on PC, with mods. It builds on weemitt, the native
PC port. `weemitt/` is g64coopdx's own copy of weemitt, and co-op changes the game code in it. Read this file, then
docs/ (start with docs/discovery/README.md).

## Rules

- **Never commit a ROM**, anything made from it (banks, textures, sounds, levels, memory dumps, savestates,
  recordings that hold memory), or a save file. The program reads the player's own ROM at start-up.
- **Never send game data over the network.** Only inputs, session messages, hashes, chat and level packs that hold
  no game data cross it.
- **Never use leaked original source code**, the N64 SDK's source (libultra, libaudio, libmus), or code from other
  ports, sm64coopdx and its CoopNet included. Study their design and docs only. Write the code from public
  documentation or your own design. The owner's own projects (weemitt, gloveworld, Glovebox) are not other ports:
  their code may be used.
- **This file's rules win over `weemitt/CLAUDE.md`.** That file is weemitt's own, and its rules for an exact copy of
  the original game (the memory layout rule, weemitt's baseline, the frame-rate and emulator checks) do not apply
  here. Its rules on the ROM and on leaked or other ports' code agree with this file.
- **Game changes go in `weemitt/`, and only there.** weemitt's own rules for an exact copy of the original game
  (its memory layout rule, its baseline) do not apply here. Keep co-op code in its own functions and files where you
  can; it makes weemitt's later fixes easier to take. Record each pull from weemitt in `weemitt/SOURCE`.
- **Pointers in the game's memory must point into `port_ram`.** On the 64-bit builds a stored pointer is an offset
  into it. Put new players' state on the game's heap. Growing a global means regenerating `gen/`.
- **g64coopdx keeps its own baseline.** `tests/rom/` records it. Record it again only when a change is meant to
  alter the game, and say so in the commit.
- **The online game is exact.** It runs whole frames only (`deltaTime` 1). Two machines with the same inputs must
  give the same state hash every frame, on Linux and Windows. A change that breaks this is a bug, even if it fixes
  something else.
- **One instance per occupied level.** State shared between levels (the save, the world, who is where) changes only
  through session events that the host puts into an instance's confirmed inputs at a frame.
- **Everything from the network is untrusted.** Check every length, count, index, frame number, string and path
  before use. Clamp inputs to what a controller can give. Every parser has a fuzz target.
- **Scripts are deterministic.** The Lua sandbox gives no clock, no address, no unseeded random number and no
  system call. Lua's state lives in the game's arena, inside the savestate.
- **The Windows program comes first.** Test it under Wine or on Windows.
- Write all prose (docs, comments, commits, pull requests) in plain language (ISO 24495-1): short sentences, one idea
  each, terms defined on first use. American English. No em dashes. No AI model names.
- Do not end sentences with a purpose clause that explains why the preceding detail matters. State the fact and
  stop.
- Comment code where the reason is not obvious, in plain sentences.
- The project's own code is MIT (`LICENSE`). Vendored code keeps its license; list it in `THIRD_PARTY_NOTICES.md`.

## Everyday work

These commands are planned; none exists yet.

    make -j4                       # build/linux/g64coopdx
    make windows -j4               # build/windows/g64coopdx.exe
    make headless -j4              # the test build
    make test                      # unit, toy-game, loopback and fuzz tests; no ROM needed
    make test-rom ROM=path         # the tests that need the ROM; they skip without it
    tools/session.py --clients 4 --latency 80 --loss 2 --rom ROM   # a headless session on loopback

## Where things go

| What | Where |
| --- | --- |
| A change to the game's logic | `weemitt/`, g64coopdx's copy |
| The network, rollback, sessions | src/net, src/rollback, src/session |
| The Lua API | src/lua, documented in docs/lua-api.md |
| A design decision | docs/design.md |
```

## Candidate names

`g64coopdx` names the project after sm64coopdx. That shows the idea, but it can read as part of the work of coop-deluxe,
the team that publishes sm64coopdx, and it is hard to say. "Glover" is the game's name and a trademark of its rights
holder, so a name without it is safer for a released program (INFERRED). weemitt already follows that pattern.

| Name | Note |
| --- | --- |
| g64coopdx | the working name; clear to people who know sm64coopdx |
| Handful | a handful of gloves; short; no trademark in it |
| Glovemates | plain meaning; contains "glove", a common word |
| Garib Party | the game's collectable; playful |
| weemitt together | ties it to weemitt |

The owner decides later; `g64coopdx` stays the working name ([open-questions.md](open-questions.md#decided)).
