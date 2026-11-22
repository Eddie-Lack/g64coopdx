# Guide for agents

g64coopdx is an online co-op version of Glover (N64, USA, NGVE) on PC, with mods. It builds on weemitt, the native
PC port. Read this file, then docs/ (start with docs/discovery/README.md).

The repository holds the design, a placeholder for the lobby server in `server/`, and `weemitt/`, g64coopdx's own
copy of weemitt (a `git subtree`; `weemitt/SOURCE` names the weemitt commit). Co-op changes the game code in the
copy. The work follows docs/discovery/roadmap.md, phase 0 now.

## Rules

- **Never commit a ROM**, anything made from it (banks, textures, sounds, levels, memory dumps, savestates,
  recordings that hold memory), or a save file. The program reads the player's own ROM at start-up.
- **Never send game data over the network.** Only inputs, session messages, hashes, chat and level packs that hold
  no game data cross it.
- **Never use leaked original source code**, the N64 SDK's source (libultra, libaudio, libmus), or code from other
  ports, sm64coopdx and its CoopNet included. Study their design and docs only. Write the code from public
  documentation or your own design. The owner's own projects (weemitt, gloveworld, Glovebox) are not other ports:
  their code may be used.
- **Never commit a secret.** Tokens and keys live in the hosting provider's secrets and in GitHub's repository
  secrets (docs/hosting.md).
- **This file's rules win over `weemitt/CLAUDE.md`**. That file is weemitt's own, and its rules for an exact copy of
  the original game (the memory layout rule, weemitt's baseline, the frame-rate and emulator checks) do not apply
  here. Its rules on the ROM and on leaked or other ports' code agree with this file.
- **Game changes go in `weemitt/`, and only there.** Keep co-op code in its own functions and files where you can;
  it makes weemitt's later fixes easier to take. Record each pull from weemitt in `weemitt/SOURCE`.
- **Pointers in the game's memory must point into `port_ram`.** On the 64-bit builds a stored pointer is an offset
  into it. Put new players' state on the game's heap. Growing a global means regenerating `gen/`.
- **g64coopdx keeps its own baseline.** It is the commit named by `BASELINE_REF` in `weemitt/tools/regress.py`.
  Today that is the commit that added the copy, so the logic is still weemitt's at one player. Move it only when a
  change is meant to alter the game, and say so in the commit. Recording the baseline needs that commit: in a shallow
  clone, fetch it first (`git fetch origin c592b7efa902907779c1f477ef7b5e236d37ff0b`).
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

## Pull requests

- Merge your own pull requests once they are ready. No one else reviews them.
- A pull request is ready when CI passes and nothing is left to fix.
- After a merge, restart your branch from the latest `main` before the next change.

## Everyday work

    make client        # the online program: weemitt/build/studio64/weemitt-studio (docs/playing-online.md)
    make headless      # the game without a window: weemitt/build/headless64/weemitt
    make game          # the game for Linux: weemitt/build/linux64/weemitt (needs SDL2)
    make windows       # the game for Windows: weemitt/build/windows64/weemitt.exe (needs MinGW-w64 and SDL2)
    make crossbuild    # the Linux and Windows builds give equal states and hashes (needs the ROM and Wine)
    make savestates    # the brute forcer's engine and savestates at 64 bits, for Linux and Windows
    make savestates-test   # its tests, the Windows program under Wine (needs the ROM)
    make rollback-game # the rollback core with the real game, on Linux and under Wine (needs the ROM)
    make sound         # the online game's sound through rollbacks, on Linux and under Wine (needs the ROM)
    make twoplayers    # four gloves in one game; enemies, puzzles, respawns, hazards, garibs, pick-ups, bosses, bonus levels, the look-around mode, tips, cameos, each player's ball texture and the any-ball rule with two players; the frame's limits with 16; Linux and Wine must print the same (needs the ROM)
    make levelstart    # every level built from the power-on state gives the same hashes, on Linux and Wine (needs the ROM)
    make bots          # every level with 8 and with 16 players who play at random, on Linux and Wine (needs the ROM)
    make instances     # players moving between instances of the real game, on Linux and Wine (needs the ROM)
    make regress       # the game's logic against g64coopdx's baseline (needs the ROM; records it on the first run)
    make server        # the lobby server: build/lobby (needs Go)
    make test          # the server's checks, the rollback core's loopback tests and the UDP tests; no ROM needed
    make test-windows  # the loopback tests as a Windows program under Wine; its hash must equal Linux's
    make fuzz          # the message parsers under libFuzzer (needs clang)
    make image         # the lobby server's container image (needs Docker)

CI (`.github/workflows/ci.yml`) runs the tests, also under Wine, and fuzzes the parsers. It builds the server and the
game's headless and Windows library builds on every pull request, and publishes the image from `main`. The ROM checks
run locally: set `GLOVER_ROM`.

## Where things go

| What | Where |
| --- | --- |
| The lobby server | `server/` |
| Where the services run, and how to deploy them | docs/hosting.md |
| The design and the roadmap | docs/discovery/ |
| A change to the game's logic (from phase 0) | `weemitt/`, g64coopdx's copy |
| The rollback core, and the real game behind it | src/rollback (`rollback.h`, `engine_game.h`) |
| The toy game, the loopback tests, fuzz targets | tests/toy, tests/net, tests/fuzz |
| Tests that need the ROM (they skip without it) | tests/rom |
| More than one player in the game (the swap prototype) | weemitt/src/game/g64players.c |
| The UDP transport (direct mode) | src/net (`udp.h`) |
| The direct-mode session (hello, welcome, start, game messages) | src/session (`session.h`) |
| The session's encryption, and the library it uses | src/session (`seal.h`), `third_party/monocypher` |
| The online mode of the window program | src/client (`netplay.h`), built into weemitt's studio (`make client`) |
| Instances: one game per occupied level, and moving players between them | src/client (`instances.h`, `imsg.h`) |
| The online game's sound: the reader and the real sound player | src/client (`sound.h`, `soundout.h`); the table in weemitt/src/port/soundtable.h |
| How players host and join | docs/playing-online.md |
