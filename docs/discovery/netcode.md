# The network model, transport and services

This file covers questions 6 and 7. It compares three network models against Glover, gives the measurements, and
recommends one. Then it covers the transport, NAT traversal, relays, lobbies and the handshake.

**VERIFIED** means measured in this session or read in a source that is named. **INFERRED** means concluded from
them. **UNKNOWN** means not known yet; each unknown has a way to settle it in [roadmap.md](roadmap.md) or below.

## Summary

- **Recommendation: rollback with host-confirmed inputs, one simulation per occupied level** (an **instance**).
  Every machine in a level runs that level's instance on the same inputs. A machine shows its own player at once and
  predicts the others. When the real inputs arrive and differ, it loads a savestate and runs the frames again. The
  host decides the final input of every player in every instance for every frame, so a slow player never stalls the
  others.
- It rests on facts measured here: the logic is bit-exact across runs and across Linux and Windows builds at one
  whole frame per update; a savestate costs 0.02 ms (0.4 ms as a full copy); a game frame costs 0.03 to 0.05 ms.
- Only inputs cross the network: a few bytes per player per frame. No game data travels. A player can only cheat
  with inputs a controller could give.
- A player who enters a level that others are playing replays that instance's inputs from its start, which a machine
  runs at more than 20,000 frames a second. Late join is the same step.
- Transport: UDP with encryption; direct connection to the host, NAT traversal with ICE (libjuice), and a TURN relay
  as fallback. A small lobby server of the project's own.

## The measurements

All from this session; scripts in [experiments/](experiments/); details in [weemitt.md](weemitt.md#experiments).

| Question | Result |
| --- | --- |
| Do two runs with the same inputs stay equal? | Yes. 48 levels at 6,000 frames and the attract mode: the same trace and memory hash every 10 frames |
| Across builds? | Linux 32-bit = Linux 64-bit (trace and hash). Linux 64-bit = Windows 64-bit under Wine (trace and hash, 49 runs of 3,000 frames). Linux 32-bit library = Windows 32-bit library (trace and hash, 49 runs). Each set also ran its first build twice. 147 comparisons of trace and hash in all, no difference |
| With the real sound player in place of the silent one? | The same trace in 49 runs. The memory differs (the player keeps state there) |
| Size of a savestate | 4.67 to 4.99 MB: the game's variables 1.04 MB, memory below the heap 2.69 MB, the heap 0.93 to 1.25 MB |
| Cost of a save, a load | 0.014 to 0.019 ms and 0.017 to 0.025 ms after a frame with page tracking; about 0.4 ms each as a full copy |
| Cost of a game frame | 0.028 to 0.047 ms (headless, full draw); 0.023 to 0.039 ms with the brute forcer's trimmed draw |
| Cost of the full memory hash | about 8 ms |
| Memory changed in one frame | 6.7 to 9.5 KB in 43 to 48 pages of 4 KB; 12 KB compressed with xz |
| All 24 MB compressed | 0.6 to 1.0 MB |
| A separate program, several games in one process | Works: one game per copy of the library file, on Linux and Windows |

Not measured: real networks, latency, packet loss, more than one glove. Those need the multi-player game core
first ([roadmap.md](roadmap.md)).

## Three models

### A. Deterministic lockstep with input delay

Every machine waits until it has every player's input for a frame, then runs it. Each local input is delayed by a few
frames to hide the network's delay.

- Simple: no savestates, no prediction.
- At 20 frames a second a frame is 50 ms. A delay of 2 frames hides a round trip of about 100 ms. Glover's controls
  are slow enough that 2 frames may feel acceptable; 4 or more would not (INFERRED; test it).
- The slowest player sets everyone's pace. One player's lost packet stalls all players. With 8 to 16 players on the
  internet, stalls happen often (INFERRED).

### B. Rollback on the port's savestates

Every machine runs every frame at once with its own input and a prediction of the others' (their last input). It
keeps a savestate of each frame since the last frame whose inputs are all known (the confirmed frame). When a real
input arrives and differs from the prediction, it loads the state of that frame and runs forward again.

- The local glove responds at once. Remote gloves and their balls jump when a prediction was wrong.
- Cost (INFERRED from the measurements): a rollback of 8 frames (400 ms) is one load and 8 frames: under 0.5 ms with
  page tracking, about 4 ms with full copies. A frame lasts 50 ms. Resimulated frames can use the trimmed draw; only
  the last frame needs the full draw for the picture.
- One simulation holds one level. Players in different levels are in different simulations, called instances
  ([below](#instances)).
- Needs: bit-exact logic everywhere (measured), cheap savestates (measured), sound that does not replay on each
  rollback ([below](#sound-and-rollback)), and deterministic scripts ([mods.md](mods.md#scripts-in-a-rollback-game)).

### C. State synchronization with an authority, as sm64coopdx does

Each machine runs its own copy of the level. Each player sends its own glove's and ball's state. Objects are owned
by the nearest player or by the host, and the owner sends their state. Events (a garib taken, an enemy killed, a
switch pressed) go as messages ([sm64coopdx.md](sm64coopdx.md), section 3).

- Any latency is tolerated, for any number of players. Players can be in different levels.
- Determinism is not needed, so the Smooth mode would be allowed.
- But every kind of object needs code to send and apply its state: Glover's enemies run instruction scripts,
  platforms follow paths, puzzles test positions, and the ball pushes switches and enemies. sm64coopdx needed a
  sync system with ids, ownership, per-object fields, event packets and delayed packets for this, and still only logs
  desyncs. The amount of game code to touch is much larger than for B (INFERRED from
  [state-inventory.md](state-inventory.md)).
- A client sends its own state, so it can teleport or fly. sm64coopdx checks only for its debug-fly action.

### Judged against Glover

| | A. Lockstep | B. Rollback | C. State sync |
| --- | --- | --- | --- |
| The ball's physics | Exact | Exact; remote balls jump after a wrong prediction | Remote balls are copies, late by the latency; a remote ball pressing a switch needs an event |
| Latency | Felt as input delay by everyone | Hidden for the local player; seen as corrections of remote players | Hidden; remote players shown in the past |
| Late join | Replay the level's inputs | Replay the level's inputs | Ask a peer for the level's state; per-object code |
| Player count 8 to 16 | Poor: the slowest player stalls all | Good with host-confirmed inputs (below) | Good |
| Desync detection | Hash compare: exact | Hash compare: exact | No reference; drift is normal |
| Desync repair | Replay from the level's start | Replay from the level's start | Partial, per object |
| Game code to change | The multi-player core | The multi-player core | The multi-player core plus sync code for every object kind |
| Cheating | Inputs only | Inputs only | Any state the client owns |
| Players in different levels | One simulation per level | One instance per occupied level; the host confirms the inputs of all of them | Natural |
| Scripts | Deterministic, no sync code | Deterministic, in the savestate | Sync tables, as sm64coopdx |

### Recommendation: B, with host-confirmed inputs

GGPO-style rollback lets every peer wait for every other peer's input once its prediction window is used up. With
many players that becomes lockstep's stall. The recommended variant avoids it:

1. Every client sends its input for frame t of its instance to the host, a few frames ahead of time, with the last
   few inputs repeated against loss.
2. The host collects each instance's inputs for frame t. At a deadline (a fixed number of frames behind its own time)
   it **confirms** frame t: for a player whose input has not arrived it repeats that player's last input. It sends the
   confirmed inputs to the players in that instance.
3. Every client predicts with the latest inputs it has, and rolls back to the confirmed inputs when they arrive.
4. A late player's own glove snaps back to what the host confirmed. Nobody else waits.
5. The host also runs every instance on its confirmed inputs alone, with no prediction and no rollback. That copy is
   the reference for hashes and the source of start records ([Instances](#instances)). A dedicated host is the same
   program without a window or local player.

Why B (INFERRED from the measurements and the comparison):

- The port's two unusual strengths, exact logic and cheap savestates, are exactly what rollback needs. Model C would
  not use them.
- Model C's per-object sync code is the largest and least testable part of sm64coopdx. Glover avoids it.
- Only inputs travel. No ROM-derived data goes over the network, and a cheater cannot set state.
- Every frame of every machine can be checked against the host's by hash. weemitt's test tools already compare runs
  frame by frame.

The costs of B, and how to meet them:

| Cost | Answer |
| --- | --- |
| Players in different levels need different simulations | One instance per occupied level; the host runs a reference copy of each ([Instances](#instances)) |
| Corrections of remote gloves are visible | Show remote gloves smoothed toward the corrected position over a few drawn frames; never the local glove |
| Logic runs at 20 frames a second only | The Original mode already draws smooth pictures from whole frames (`weemitt/docs/interpolation.md`) |
| Sound must not replay | [Sound and rollback](#sound-and-rollback) |
| Scripts must be deterministic and in the state | [mods.md](mods.md#scripts-in-a-rollback-game) |
| A slow machine falls behind | The host's deadline also bounds how far a client may be behind; a client that falls too far behind is told to catch up (it runs frames faster than real time) or is dropped |

Model A remains a fallback inside B: a session with a large input delay and a prediction window of 0 is lockstep.

## The details of the recommended model

### Frames and time

- The logic runs at one whole frame per update (`deltaTime` 1). The Smooth mode is off in a network game: below one
  frame the helpers use `pow` and `exp`, which differ between systems (`weemitt/docs/testing.md`).
- A level frame is 3 video retraces (20 a second); a menu frame is 2 (30 a second) (`weemitt/docs/library.md`,
  "Time"). The session's clock counts game frames and follows the game's own retrace count.
- The pause menu: in a shared game one player's pause cannot stop the others. Pause becomes a local overlay; the
  game's own pause (which stops the logic) is off, or needs a vote ([open-questions.md](open-questions.md)).

### Messages and their size

| Message | Content | Size |
| --- | --- | --- |
| Input | frame number, buttons (16 bits), stick x and y (8 bits each), the last 3 inputs again | about 20 bytes |
| Confirmed inputs | frame number, one input per player | about 4 + 4 × players bytes |
| Hash | frame number, a 64-bit hash of the state | 12 bytes |
| Session events | join, leave, enter and leave a level, a level done, kick, chat | small, reliable |

At 20 frames a second with 16 players, a client receives about 70 bytes a frame, 1.4 KB a second, plus headers
(INFERRED). The cost of the network is latency, not bandwidth.

### Desync detection and repair

- Every N confirmed frames each machine hashes its state of that frame and sends the hash to the host.
- The full memory hash costs about 8 ms. Hashing the savestate of the confirmed frame on a worker thread keeps it off
  the game's thread. A cheaper hash over the changed pages is an upstream item ([weemitt.md](weemitt.md#upstream-changes-weemitt-needs)).
- The host compares each client's hash with its own reference copy of the client's instance.
- On a mismatch the host tells the client, and the client rebuilds the level from the instance's start record and
  input log ([Instances](#instances)). A log of the first frame that differs, with the trace line, goes to a bug report.
- The hash must cover the scripts' state (the Lua arena) and the session's own state.

### Instances

The owner's rules: players may be in different levels of the same world; a portal to another world opens when every
player is in the hub ([README.md](README.md#worlds-levels-and-instances)).

**What an instance is.** One simulation of one level, run on the inputs of the players in it, from its **start
record**:

- the level and the frame the instance began;
- the session's settings (ball pickup rule, cheats, difficulty) and packs;
- the session's save at that frame (512 bytes; the EEPROM holds no ROM data, `weemitt/docs/testing.md`);
- each player's carried state: slot, ball type, power-ups, the hub's left-behind ball;
- the random seed (`LoadLevel` reseeds it, `weemitt/src/game/game.c:1454`).

**A machine runs one instance:** the one its player is in. The host runs a reference copy of every instance on
confirmed inputs. With one game per process (the static build, or one library copy), the host runs each extra
instance in a worker process. Its cost is small: a confirmed frame costs 0.03 to 0.05 ms, so 16 instances cost under
1 ms per 50 ms frame (INFERRED from the measured frame cost).

**Entering a level.**

1. A player who leaves a level (by its exit, by a hub door) leaves its instance at a confirmed frame.
2. If the target level has no instance, the host makes a start record and the player starts a new instance.
3. If it has one, the player's machine builds the level from that instance's start record and its own ROM, runs the
   instance's input log from its start, and joins the live frames as a predicted player. At 20,000 frames a second,
   10 minutes of play (12,000 frames) take under a second, and an hour about 4 seconds (INFERRED from the measured
   frame cost). A late joiner to the session does the same.
4. The new player's glove and ball appear at a confirmed frame. Players who appear and vanish at a frame are new game
   logic, upstream ([weemitt.md](weemitt.md#upstream-changes-weemitt-needs)).
5. An instance ends when its last player leaves.

**Why not send a savestate.** A savestate holds the game's memory, and the memory holds data from the ROM (code,
tables, textures). Sending it would send game data, and its host pointers are only valid in one process.

UNKNOWN: whether a level start is fully set by such a record, or whether more of the game's state carries over from
the previous level. Settle it in phase 1: start a level two ways (after playing, and from the record) and compare the
memory. The fallback: a player may join a running instance only if it can be rebuilt; otherwise they wait for a new
instance of that level.

### Session events

Some state belongs to the session, not to one instance: the save (levels done, all-garib bits), the world, who is
where. Changes to it come from one instance and may matter to another: a hub door that opens when a level is done.

- An event comes out of an instance at a confirmed frame (a level done when a player exits it).
- The host applies it to the session's state, and sends it into every instance that it affects as part of the
  confirmed inputs of a frame it picks. Every machine in that instance then sees it at the same frame.
- The world portal: the host opens a portal to another world only while every player is in the hub; entering it
  changes the session's world.

### Sound and rollback

- The game calls the sound player from its logic. On a rollback the resimulated frames would play their sounds
  again.
- The sound player's state is in the game's memory (`weemitt/docs/testing.md`). The traces of the silent and the real
  sound player agreed in 49 runs here, but the logic reads some sound handles: a Samtex enemy keeps updating while its
  sound plays (`weemitt/docs/library.md`, "Sound"; `src/game/enemy.c:1302`). So the sound's logical state must be
  identical on every machine and in the savestate.
- Plan (INFERRED): split the sound into its logical part (handles, which sound plays, its timers), run in every
  frame and saved, and its output (the synthesizer and the samples), driven only by the frames the player sees.
  Sounds started in a resimulated frame that the display already played are not started again. This is an upstream
  change. UNKNOWN: how much of libmus's state the logic can see. Settle it by listing the logic's reads of sound
  state.

### Settings that change the logic

`weemitt/docs/testing.md` names settings that change the logic: the easy ball controls and the camera's follow speed.
In a shared game such a setting is part of the session (the same for all) or part of one player's input. The menu's
Enhancements and Cheats are session settings that only the host sets; the cheat list in the pause menu is off
(upstream item 9).

## Transport and services

### What the transport must do

- UDP, with unreliable messages for inputs and hashes, and reliable ordered messages for session events, chat and
  pack downloads.
- Encryption and authentication of peers.
- Direct connection to a host with a public address or a forwarded port.
- NAT traversal for hosts behind home routers, and a relay when traversal fails.
- Windows first (MinGW cross-builds, as weemitt), Linux too.

### Candidate libraries (licenses read from each project's LICENSE file; dates from the release feeds)

| Library | License | What it gives | Fit |
| --- | --- | --- | --- |
| ENet | MIT | Reliable and unreliable UDP channels, in C | Good transport; no encryption or NAT traversal (not confirmed from ENet's own docs, which could not be fetched) |
| GameNetworkingSockets (Valve) | BSD-3-Clause | Messages, encryption (AES-GCM-256, Curve25519), ICE through WebRTC's code; v1.6.1, 3 October 2026 | All in one, but large dependencies; Valve's relay and signaling are Steam only; MinGW support unconfirmed |
| libjuice | MPL-2.0 | ICE with STUN and TURN, in C; v1.7.4, 26 September 2026 | Good for NAT traversal; sm64coopdx ships it too |
| libdatachannel | MPL-2.0 | WebRTC data channels (DTLS, SCTP), C++ | More than needed |
| yojimbo, netcode, reliable | BSD-3-Clause | Client and server, encrypted with libsodium, connect tokens | Client-server only, no NAT traversal |
| GGPO | MIT | Rollback input exchange, peer to peer | Its README describes a Windows build only, and it has no releases; peers wait for each other once the prediction window is used up (INFERRED from its model), which is what host-confirmed inputs avoid |
| SDL_net | zlib | Plain sockets | Too low level |
| libsodium | ISC | Encryption | Pairs with ENet |
| Monocypher | BSD-2-Clause or CC0 | Encryption, one file | Pairs with ENet |
| coturn | BSD-3-Clause (read from its LICENSE header) | A TURN and STUN server; 4.19.0, 14 November 2026 | The relay, run as its own server |
| Steamworks networking | Steamworks SDK terms (not read) | Valve's relay, P2P | Needs the game on Steam; not an option for a ROM-based project (INFERRED) |
| Epic Online Services P2P | Epic's SDK terms (not read) | Relay, lobbies | A proprietary service and account; not first choice |
| Discord Game SDK | proprietary, archived | | Do not build on it |
| CoopNet (sm64coopdx's lobby) | AGPL-3.0 (VERIFIED: its LICENSE on branch `master`) | Lobby and signaling over libjuice | Another port's code; do not use |

**Recommendation (INFERRED):** ENet for the channels, libsodium for encryption (a key exchange at connect, then
every packet sealed), libjuice for ICE, coturn for the relay. All are C, small, and MinGW-friendly; the licenses (MIT,
ISC, MPL-2.0, BSD-3-Clause) suit any license g64coopdx chooses. MPL-2.0 asks that changes to libjuice's own files
stay MPL-2.0 and be shared; use it unchanged. Phase 2 of the roadmap tries GameNetworkingSockets' MinGW build as the
alternative.

### Services

| Service | What | Who runs it |
| --- | --- | --- |
| Direct connection | The host shares an address and port; a joiner types it | Nobody |
| Lobby and signaling server | Lists open sessions (name, version, player count, packs, password flag); passes ICE candidates between host and joiner; hands out short-lived TURN credentials | The project: a small program of its own, written from scratch |
| TURN relay | Carries the traffic when traversal fails | The project, with coturn |
| STUN | Tells a machine its public address | The project's coturn, or public STUN servers |

The lobby server never sees game data: only session descriptions, ICE candidates and the handshake. sm64coopdx's
lobby (CoopNet) does the same job; its design (a lobby that only introduces players, the version in the lobby list,
the game name as a filter) is a model, its code is not.

### The handshake

On connect, before anything else, the host and the joiner compare:

| Item | Rule |
| --- | --- |
| Protocol version | Equal |
| Build id | Equal: g64coopdx's commit, weemitt's commit, the compiler and its flags, the Lua version. sm64coopdx compares a 128-byte version string at three points ([sm64coopdx.md](sm64coopdx.md), 3.9) |
| ROM | Each machine sends the SHA-1 of its own ROM (`270be17b...` for Glover USA); equal. The ROM never travels |
| Packs | The host's list of pack hashes; the joiner downloads what it lacks, after the player agrees ([mods.md](mods.md#how-mods-reach-other-players)) |
| Session settings | From the host |
| Player | name, colors, the glove and ball models chosen; checked for length and characters |

The 64-bit and the 32-bit builds give the same results here, but one session should still require one build id
(INFERRED: the cheapest rule that catches every difference).

### Everything from the network is untrusted

- Every message has a type, a length and fields with fixed limits; a parser checks all of them before use and
  drops the message otherwise.
- Inputs are clamped: buttons to the bits a controller has, sticks to -80 to 80 (the port's range,
  `weemitt/docs/library.md`).
- Frame numbers are checked against a window; a client cannot confirm frames, only the host can.
- Names and chat: a length limit, no control characters, rate limits.
- Pack files: hashes, sizes, path rules ([mods.md](mods.md#how-mods-reach-other-players)).
- No pointers, offsets or memory addresses in any message.
- Rate limits on every message type; a host drops and bans peers that exceed them.
- The lobby server checks the same, and never forwards data it has not parsed.
- Fuzz tests for every parser in CI ([README.md](README.md#testing)).
