# The network model, transport and services

This file answers questions 6 and 7 of the discovery: the network model, and the transport and services
([README.md](README.md#the-documents) lists the questions). It compares three network models against Glover, gives
the measurements, and recommends one. Then it covers the transport, NAT traversal, relays, lobbies and the
handshake. The terms are defined in [README.md](README.md#terms).

**VERIFIED** means measured by the discovery or by a test the text names, or read in a source that is named.
**INFERRED** means concluded from them. **UNKNOWN** means not known yet; each unknown has a way to settle it in
[roadmap.md](roadmap.md) or below.

## Summary

- **Recommendation: rollback with host-confirmed inputs, one simulation per occupied level** (an **instance**).
  Every machine in a level runs that level's instance on the same inputs. A machine shows its own player at once and
  predicts the others. When the real inputs arrive and differ, it loads a savestate and runs the frames again. The
  host decides the final input of every player in every instance for every frame, so a slow player never stalls the
  others.
- It rests on facts that the discovery measured: the logic is bit-exact across runs and across Linux and Windows builds
  at one whole frame per update; a savestate costs 0.02 ms (0.4 ms as a full copy); a game frame costs 0.03 to 0.05 ms.
- Only inputs cross the network: a few bytes per player per frame. No game data travels. A player can only cheat
  with inputs a controller could give.
- A player who enters a level that others are playing replays that instance's inputs from its start, which a machine
  runs at more than 20,000 frames a second. Late join is the same step.
- Transport, as the discovery recommends: UDP with encryption; direct connection to the host, NAT traversal with ICE
  (libjuice), and a TURN relay as fallback. A small lobby server of the project's own. Built so far: encrypted UDP, a
  direct connection to the host's address, and the lobby's own UDP relay, with rooms joined by code
  (`server/relay.go`). The game has no NAT traversal yet. Cloudflare's TURN service, or direct connections through
  libjuice, can replace the relay in phase 4 ([hosting.md](../hosting.md#the-decision)).

## The measurements

All from the discovery; scripts in [experiments/](experiments/); details in [weemitt.md](weemitt.md#experiments).

| Question | Result |
| --- | --- |
| Do two runs with the same inputs stay equal? | Yes. Each of four sets of 49 runs (48 levels and the attract mode, 3,000 or 6,000 frames each) ran its first build twice: 196 pairs, the same trace and memory hash at every check (every 10 frames, or 100 in the set that compared the sound players) |
| Across builds? | Linux 32-bit = Linux 64-bit (trace and hash). Linux 64-bit = Windows 64-bit under Wine (trace and hash, 49 runs of 3,000 frames). Linux 32-bit library = Windows 32-bit library (trace and hash, 49 runs). 147 pairs of different builds in all, no difference |
| With the real sound player in place of the silent stand-in? | The same trace in 49 pairs (64-bit Linux, 3,000 frames). The memory differs (the player keeps state there) |
| Size of a savestate | 4.67 to 4.99 MB: the game's variables 1.04 MB, memory below the heap 2.69 MB, the heap 0.93 to 1.25 MB |
| Cost of a save, a load | 0.014 to 0.019 ms and 0.017 to 0.025 ms after a frame with page tracking; about 0.4 ms each as a full copy |
| Cost of a game frame | 0.028 to 0.047 ms (headless, full draw); 0.023 to 0.039 ms with the brute forcer's trimmed draw |
| Cost of the full memory hash | about 8 ms |
| Memory changed in one frame | 6.7 to 9.5 KB in 43 to 48 pages of 4 KB; 12 KB compressed with xz |
| The game's whole memory (24 MB) compressed | 0.6 to 1.0 MB |
| Can a separate program run several games in one process? | Works: one game per copy of the library file, on Linux and Windows |

The discovery did not measure real networks, latency, packet loss or more than one glove. Those needed the
multi-player game core first ([roadmap.md](roadmap.md)). Later tests ran up to 16 gloves in one game, and a host
and three players in one process over a simulated lossy network.

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
- Cost (INFERRED from the measurements): a rollback of 8 frames (400 ms) is one load, 8 frames and 8 saves. That
  takes 0.4 to 0.6 ms with page tracking and about 4 ms with full copies. A frame lasts 50 ms. Resimulated frames
  can use the brute forcer's trimmed draw, which skips the work that only builds the picture; only the last frame
  needs the full draw.
- One simulation holds one level. Players in different levels are in different simulations, called instances
  ([below](#instances)).
- Needs: bit-exact logic everywhere (measured), cheap savestates (measured), sound that does not replay on each
  rollback ([below](#sound-and-rollback)), and deterministic scripts ([mods.md](mods.md#scripts-in-a-rollback-game)).

### C. State synchronization with an authority

sm64coopdx uses this model. Each machine runs its own copy of the level. Each player sends its own glove's and ball's state. Objects are owned
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

### Recommendation: B with host-confirmed inputs

In the common form of rollback (as in GGPO, a library in the table of candidates below), each machine may predict only a
fixed number of frames ahead of the last inputs it has. When a peer's inputs are later than that, every machine waits
for them. With many players that wait comes often, as in lockstep. The recommended variant avoids it:

1. Every client sends its input for frame t of its instance to the host, a few frames ahead of time, with the last
   few inputs repeated against loss.
2. The host collects each instance's inputs for frame t. At a deadline (a fixed number of frames behind the host's
   own frame count)
   it **confirms** frame t: for a player whose input has not arrived it repeats that player's last input. It sends the
   confirmed inputs to the players in that instance.
3. Every client predicts with the latest inputs it has, and rolls back to the confirmed inputs when they arrive.
4. A late player's own glove snaps back to what the host confirmed. Nobody else waits.
5. The host also runs every instance on its confirmed inputs alone, with no prediction and no rollback. That copy is
   the reference for hashes and the source of start records ([Instances](#instances)). A dedicated host is the same
   program without a window or local player.

Why B (INFERRED from the measurements and the comparison):

- The port's two unusual strengths, exact logic and cheap savestates, are what rollback needs. Model C would
  not use them.
- Model C's per-object sync code is the largest and least testable part of sm64coopdx. Model B avoids it.
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
| A client's inputs reach the host after their frame is confirmed | Each confirm message carries the host's clock. A client runs up to that clock, plus one frame each time the host confirmed a frame without its own input (at most 4, `rb_client_target`). The lead does not shrink: a client that runs ahead stays ahead until it moves to another instance |

Model A remains a fallback inside B: a session with a large input delay and a prediction window of 0 is lockstep.

## The details of the recommended model

### Frames and time

- The logic runs at one whole frame per update (`deltaTime` 1). The Smooth mode is off in a network game: below one
  frame weemitt's delta-time helpers use `pow` and `exp`, which differ between systems (`weemitt/docs/testing.md`).
- A level frame is 3 video retraces (20 a second); a menu frame is 2 (30 a second) (`weemitt/docs/library.md`,
  "Time"). The session's clock counts game frames and follows the game's own retrace count.
- The pause menu: in a shared game one player's pause cannot stop the others. Pause becomes a local overlay, and the
  game's own pause (which stops the logic) is off. The owner decided that the host may stop the game for everyone
  ([open-questions.md](open-questions.md#decided)). Done: each player has their own menu (src/client/netplay.c). A
  choice that acts in the game goes in that player's input for 3 frames: the button bit 0x0080, which no N64 controller
  sets, with the choice in the stick's x. The game acts on it at that confirmed frame (`G64PlayerAction`), for that
  player only.

### Messages and their size

| Message | Content | Size |
| --- | --- | --- |
| Input | frame number, buttons (16 bits), stick x and y (8 bits each), the last 3 inputs again | about 20 bytes |
| Confirmed inputs | frame number, one input per player | about 4 + 4 × players bytes |
| Hash | frame number, a 64-bit hash of the state | 12 bytes |
| Session events | join, leave, enter and leave a level, a level done, kick, chat | small, reliable |

At 20 frames a second with 16 players, a client receives about 70 bytes a frame, 1.4 KB a second, plus headers
(INFERRED). Any home connection carries that; the delay between machines is what limits the game.

### Desync detection and repair

- Every N confirmed frames each machine hashes its state of that frame and sends the hash to the host. In the
  rollback core N is 10 (`RB_HASH_EVERY` in src/rollback/rollback.h).
- The full memory hash costs about 8 ms. Hashing the savestate of the confirmed frame on a worker thread keeps it off
  the game's thread. A cheaper hash over the changed pages is game change 5 ([weemitt.md](weemitt.md#changes-to-the-game-code)).
- The host compares each client's hash with its own reference copy of the client's instance.
- On a mismatch the host tells the client, and the client rebuilds the level from the instance's start record and
  input log ([Instances](#instances)). A log of the first frame that differs, with the trace line, goes to a bug report.
- The hash must cover the scripts' state and the session's own state. The scripts' block of memory (the Lua arena)
  holds addresses, and its bytes differ between machines. The hash leaves the block out. With a script, the hash
  covers a number made from the scripts' state in the block's place (`G64LuaHash` in weemitt/src/port/g64lua.c;
  `game_hash` in src/rollback/engine_game.c).

### Instances

The owner's rules: players may be in different levels of the same world; a portal to another world opens when every
player is in the hub ([README.md](README.md#worlds-and-instances)).

**What an instance is.** One simulation of one level, run on the inputs of the players in it, from its **start
record**:

- the level and the frame the instance began;
- the session's settings (ball pickup rule, cheats, difficulty) and packs;
- the session's save at that frame (512 bytes; the EEPROM holds no ROM data, `weemitt/docs/testing.md`);
- each player's carried state: player slot, ball type, power-ups, the hub's left-behind ball;
- no random seed: every level start sets the same fixed seed (`SeedRandom` in `StartNewLevel`,
  `weemitt/src/game/game.c:1454`; `maths.c:31`).

**A machine runs one instance:** the one its player is in. The host runs a reference copy of every instance on
confirmed inputs. One copy of the game's code holds one game (the static build, or one library copy). The host's
copies take turns in one engine, in one process (src/rollback/engine_game.h). The copies cost little: a confirmed
frame costs 0.03 to 0.05 ms, so 16 instances cost under 1 ms per 50 ms frame (INFERRED from the measured frame cost).
Each switch between copies adds a save and a load.

**Entering a level.**

1. A player who leaves a level (by its exit, by a hub door) leaves its instance at a confirmed frame.
2. If the target level has no instance, the host makes a start record and the player starts a new instance.
3. If it has one, the player's machine builds the level from that instance's start record and its own ROM, runs the
   instance's input log from its start, and joins the live frames as a predicted player. At 20,000 frames a second,
   10 minutes of play (12,000 frames) take under a second, and an hour about 4 seconds (INFERRED from the measured
   frame cost). A late joiner to the session does the same.
4. The new player's glove and ball appear at a confirmed frame. Players who appear and vanish at a frame are new game
   logic: game change 10 ([weemitt.md](weemitt.md#changes-to-the-game-code)).
5. An instance ends when its last player leaves.

**Why not send a savestate.** A savestate holds the game's memory, and the memory holds data from the ROM (code,
tables, textures). Sending it would send game data, and its host pointers are only valid in one process.

**What is built (15 November 2026).** `src/client/instances.c` does the above for the online program:

- The host's part runs a reference copy of each instance (`rb_engine_game` games time-share one engine) and keeps each
  instance's log of confirmed inputs. Each such game is a savestate: the one engine saves the game it holds and loads
  the next one that steps (src/rollback/engine_game.h). A player who takes an exit leaves their instance at that frame,
  in the game itself on every machine (`g64players.c`). The host reads the exit from its reference copy, at a confirmed
  frame. It then puts the player in the instance of the target level (present from its next confirmed frame), or starts
  a new instance for them alone, and sends a move: the instance's start record and the join frame. A joiner gets the log
  up to that frame in chunks, builds the instance, runs the log and starts there (`rb_client_new_at`).
- The start record holds the level, its world, the players present at the start, and the session's progress (levels
  done, all-garib bits, the hub's progress, the worlds done). Each exit carries the progress as its level leaves it;
  the host merges it into the session's progress, and new instances start with it.
- `make instances` runs a host and three players in one process over a lossy network, with exits at set frames
  (`g64TestExits`): a new instance, a join into a running one, and a return to the running hub. Every hash check
  passes, and the Linux and Windows outputs are equal.

Not yet: a running instance does not see progress made elsewhere (a session event into its confirmed inputs); the
one-world rule at the hub's portals; per-player end-of-level screens. Progress into running instances and the
one-world rule came later ([Session events](#session-events); [roadmap.md](roadmap.md#phase-2-rollback-on-a-lan)).
The move also carries the session's world, every player's colors and the scripts' 16 values at the instance's
start (`g64_move` in src/client/imsg.h).

**How a level is built (VERIFIED, 15 November 2026).** A level reached by play differs from the same level started
directly. In Atlantis 2 reached from Atlantis 1, the random seed, the glove and the ball start the same, but the
camera keeps state from the level before. The camera differs from frame 1, and the glove's path differs from frame 94
under the same input (the stick moves the glove relative to the camera). So no machine uses a level reached by play.
Every machine builds every instance the same way: it loads a state saved right after power-on
(`port_game_boot`), then starts the level from the start record (`port_game_start_level`). This holds for the first
player in a level as well as for one who joins later. `make levelstart` checks it: each of the 43 levels a player can
be in gives the same memory hash when it is built again after another level was played, on Linux and under Wine.
The 43 are the hub sections, the cave, the assault course, the worlds and the way room (tests/rom/levelstart.c). The
game's other five levels, `PRESENT` to `OUTRO` in `enum Level`, are not in the test.

What a level carries from the one before, and so what the start record holds beyond the level: the save slot
(levels done, garib bits, `hubLevel`, `hubTarget`, lives), `gameInfo.world`, `prevLevel` and `prevWorld`,
`worldComplete[]`, and the hub's left-behind ball (`ballLeftBehind`, `ballEnd`). The glove's start position comes
from the level file (`TOKEN_HAND_START`), not from the level before. `prevLevel` only picks a cameo in the first hub
section (`cameo.c:191`).

### Session events

Some state belongs to the session, not to one instance: the save (levels done, all-garib bits), the world, who is
where. Changes to it come from one instance and may matter to another: a way room door that opens when a level is
done, or the hub's stage after a crystal goes in the fountain.

- An event comes out of an instance at a confirmed frame (a level done when a player exits it).
- The host applies it to the session's state, and sends it into every instance that it affects as part of the
  confirmed inputs of a frame it picks. Every machine in that instance then sees it at the same frame.
- Done for progress (src/client/instances.c, imsg.h). After a host frame's exits, the host sends the session's whole
  progress to every instance whose progress is older. It rides on the next confirmed frame as a session event
  (`rb_event`, `rb_host_set_event`). The confirm message and the instance's log carry it after their frames, so a
  player who joins later runs it at the same frame. Every machine gives it to the game right before that frame's
  step. The game joins it with its own progress, so progress that a level has made and not yet reported stays.
  `G64ApplyProgress` sets every bit of levels done, all garibs and worlds done that either side has. It takes the
  larger of each hub count, and the level's world stays.
- Done for a script's values (`G64_EVENT_SCRIPT`; docs/scripts.md, "Values that last from level to level"). The session
  has 16 such values, numbered 0 to 15, and a value's number is its key. A script's request to add to a value stays in
  its game's saved state for one frame. The host reads it from its reference game after that frame is confirmed, adds it
  to the session's value, and sends the new value into every instance that has an older one: one key on a frame, the
  keys in turn. A game that starts gets all 16 values with its start (the move message).
- Few things in a level read the progress while it runs. A way room door that new progress opens moves away at
  once. The rest (the way room's pedestals, the cave's crystals, which hub stage is loaded) shows at the level's next
  start.
- The hub is eight levels, HUB1 to HUB8, one per crystal in the fountain. A HUBn instance stays HUBn. An exit to the
  hub (a world exit or a hub exit) leads to the newest stage the host knows, whatever the level the player leaves
  says. The host learns of a crystal when a player leaves the cave.
- The world portal: the host opens a portal to another world only while every player is in the hub; entering it
  changes the session's world.

### Sound and rollback

- The game calls the sound player from its logic. On a rollback the resimulated frames would play their sounds
  again.
- The sound player's state is in the game's memory (`weemitt/docs/testing.md`). The traces of the stand-in and the real
  sound player agreed in the discovery's 49 runs, but the logic reads some sound handles: a Samtex enemy keeps updating
  while its sound plays (`weemitt/docs/library.md`, "Sound"; `src/game/enemy.c:1302`). So the sound's logical state must
  be identical on every machine and in the savestate.
- What the logic sees of the sound player: the handles that start functions return (only zero or not zero matters),
  four loops that wait for a song to start or stop, and the song number it keeps itself. The stand-in sound player
  (`weemitt/src/port/audio_stub.c`) answers all of them the same on every machine and after every rollback, so the
  online game keeps it.
- Done (game change 12). The stand-in records what the game asks for in a sound table
  (`weemitt/src/port/soundtable.h`), which savestates hold and a rollback restores, outside the hashed memory. The
  game's sound wrappers record sounds with a place before the distance cut, with their position and volume, and
  continuous sounds as sources by the address of their structure. A player's own update marks the sounds it makes as
  theirs.
- The reader (`src/client/sound.c`) compares the local game's table with what plays, once per window frame, and tells
  the real sound player what to start, change and stop: each sound once, also when a rollback runs its frame again; a
  sound that a wrong prediction started stops when the corrected frames no longer have it; a sound that a correction
  adds at most 10 game updates (half a second) late still plays. It mixes sounds with a place for the local
  player's camera.
- The real sound player runs in a second copy of weemitt's library, built with it and without a renderer
  (`make lib64-sound`, `lib-windows64-sound`), which the online program loads as `g64coopdx-sound.so` or `.dll`
  (`src/client/soundout.c`). Its game never runs; its memory is apart from the online game's. `make sound` checks the
  reader against an offline run with rollbacks, on Linux and under Wine.

### Settings that change the logic

`weemitt/docs/testing.md` names settings that change the logic: the easy ball controls and the camera's follow speed.
In a shared game such a setting is part of the session (the same for all) or part of one player's input. The menu's
Enhancements and Cheats are session settings that only the host sets; the cheat list in the pause menu is off
(game change 7).

## Transport and services

### What the transport must do

- UDP, with unreliable messages for inputs and hashes, and reliable ordered messages for session events, chat and
  pack downloads.
- Encryption and authentication of peers.
- Direct connection to a host with a public address or a forwarded port.
- NAT traversal for hosts behind home routers, and a relay when traversal fails.
- Windows first (MinGW cross-builds, as weemitt), Linux too.

### Encryption

Done in phase 4 (src/session/seal.h, src/session/session.h). The session seals its own messages, between the host
and each joiner. The relay here is the lobby's own UDP relay (`server/relay.go`). Every player sends to it, and it
passes each packet on inside a room, which players join by a code. In the direct mode a joiner connects to the
host's address instead (src/net/udp.h). The relay carries the sealed messages unopened, and the direct mode has the
same protection.

- **The exchange.** The joiner's hello carries a new X25519 public key, and the host's welcome carries one made for
  that joiner alone. Each end makes a shared secret, then two keys from it with BLAKE2b, one for each direction. The
  bytes of the hello and the welcome go into the keys: both ends get the same keys only if both saw the same two
  messages. The keys live for one session.
- **The messages.** Every message after the welcome is sealed with XChaCha20 and Poly1305: the start (with the
  names), pings, the game's messages (inputs, hashes, session events), chat and the kick. Each has a counter that
  is never used twice. A receiver drops a message that came before, one older than the last 64, and one in the
  clear. Messages may still overtake each other. A sealed datagram is 25 bytes longer than its message.
- **What stays in the clear.** The hello (the version, the ROM's hash, the colors, the name), the welcome, a
  refusal, and the relay's own envelope with the room code.
- **Random numbers** come from the operating system (`BCryptGenRandom` on Windows, `/dev/urandom` on Linux).
  Without them no key is made and no session starts. They never enter the game's state.
- **The library** is Monocypher 4.0.2, one C file (`third_party/monocypher`). It takes the place of libsodium,
  which the recommendation below names. Monocypher builds with gcc, MinGW and clang's fuzzer with no build system
  of its own.

What this protects against: someone who listens on the path, the relay included, cannot read names in the start,
chat or inputs. Someone who is not on the path cannot forge a message, change one or play one back; that covers a
forged kick, forged inputs, and a newcomer who gets a former player's place at the relay.

What it does not protect against: the players have no identities and the room code is no secret, so someone who
can change packets on the path during the hello could put their own keys in and sit in the middle. The relay is
in that place. A forged refusal or welcome from someone who can send from the host's address can still stop a
join. Proof of who a player is needs accounts or a shared secret, and is not planned.

The fuzz target reaches the parser behind the seal through `g64_session_test_inner`: a fuzzer cannot make a
datagram that opens.

### Candidate libraries (licenses read from each project's LICENSE file; dates from the release feeds)

| Library | License | What it gives | Fit |
| --- | --- | --- | --- |
| ENet | MIT | Reliable and unreliable UDP channels, in C | Good transport; no encryption or NAT traversal (the discovery could not fetch ENet's own docs to confirm this) |
| GameNetworkingSockets (Valve) | BSD-3-Clause | Messages, encryption (AES-GCM-256, Curve25519), ICE through WebRTC's code; v1.6.1, 3 October 2026 | All in one, but large dependencies; Valve's relay and signaling are Steam only; MinGW support unconfirmed |
| libjuice | MPL-2.0 | ICE with STUN and TURN, in C; v1.7.4, 26 September 2026 | Good for NAT traversal; sm64coopdx ships it too |
| libdatachannel | MPL-2.0 | WebRTC data channels (DTLS, SCTP), C++ | More than needed |
| yojimbo, netcode, reliable | BSD-3-Clause | Client and server, encrypted with libsodium, connect tokens | Client-server only, no NAT traversal |
| GGPO | MIT | Rollback input exchange, peer to peer | Its README describes a Windows build only, and it has no releases; peers wait for each other once the prediction window is used up (INFERRED from its model), which is what host-confirmed inputs avoid |
| SDL_net | zlib | Plain sockets | Too low level |
| libsodium | ISC | Encryption | Pairs with ENet |
| Monocypher | BSD-2-Clause or CC0 | Encryption, one file | Pairs with ENet |
| coturn | BSD-3-Clause (read from its LICENSE header) | A TURN and STUN server; 4.19.0, 14 November 2026 | The fallback relay, run as its own server |
| Steamworks networking | Steamworks SDK terms (not read) | Valve's relay, P2P | Needs the game on Steam; not an option for a ROM-based project (INFERRED) |
| Epic Online Services P2P | Epic's SDK terms (not read) | Relay, lobbies | A proprietary service and account; not first choice |
| Discord Game SDK | proprietary, archived | | Do not build on it |
| CoopNet (sm64coopdx's lobby) | AGPL-3.0 (VERIFIED: its LICENSE on branch `master`) | Lobby and signaling over libjuice | Another port's code; do not use |

**The discovery's recommendation (INFERRED):** ENet for the channels, libsodium for encryption (a key exchange at
connect, then every packet sealed), libjuice for ICE, and a TURN relay. The three libraries are small and written in
C. The relay is a managed service, with coturn as the fallback ([hosting.md](../hosting.md)). libjuice lists
Windows among its platforms; a MinGW build of each library was not tried, and phase 2 tries them, with
GameNetworkingSockets' MinGW build as the alternative. Their licenses (MIT, ISC, MPL-2.0) suit MIT, the
license chosen for g64coopdx. MPL-2.0 asks that changes to libjuice's own files stay MPL-2.0 and be shared; use it
unchanged. The encryption that was built uses Monocypher in place of libsodium ("Encryption" above).

### Services

| Service | What | Who runs it |
| --- | --- | --- |
| Direct connection | The host shares an address and port; a joiner types it | Nobody |
| Lobby and signaling server | Lists open sessions (name, version, player count, packs, password flag); passes ICE candidates between host and joiner; hands out short-lived TURN credentials | The project: a small program of its own, written from scratch, in a container on Fly.io ([hosting.md](../hosting.md#the-lobby-server)) |
| TURN relay | Carries the traffic when traversal fails | Cloudflare's managed TURN service, with coturn on a server of the project's own as the fallback ([hosting.md](../hosting.md#the-relay)) |
| STUN | Tells a machine its public address | Cloudflare's public STUN server; the lobby names the servers, so they can change |

The table gives the plan. Built so far are the direct connection and the lobby. The lobby runs its own UDP relay,
which needs no credentials (`server/relay.go`). The relay keeps the list of rooms that hosts chose to show. The lobby
does not pass ICE candidates or hand out TURN credentials yet ([server/README.md](../../server/README.md);
[hosting.md](../hosting.md#what-is-set-up-today)).

The lobby server never sees game data: only session descriptions, ICE candidates and the handshake. sm64coopdx's
lobby (CoopNet) does the same job. g64coopdx follows its design: a lobby that only introduces players, the version in
the lobby list, the game name as a filter. It does not use CoopNet's code.

### The handshake

On connect, before anything else, the host and the joiner compare:

| Item | Rule |
| --- | --- |
| Protocol version | Equal |
| Build id | Equal: by default, the commit of g64coopdx that the program was built from (`G64_BUILD` in weemitt/studio.mk). weemitt's copy and Lua (`weemitt/third_party/lua`) are part of that commit. The compiler and its flags are not part of the id. The joiner's hello carries a 32-bit hash of the id (`build_hash` in src/client/netplay.c). sm64coopdx compares a 128-byte version string at three points ([sm64coopdx.md](sm64coopdx.md), 3.9) |
| ROM | Equal: the joiner's hello carries a 32-bit hash of its ROM (`g64_rom_hash` in src/session/session.c). The host refuses a hash that differs from its own (`G64_REFUSE_ROM`). The ROM never travels |
| Packs | The host's list of pack hashes; the joiner downloads what it lacks, after the player agrees ([mods.md](mods.md#how-mods-reach-other-players)) |
| Session settings | From the host |
| Player | name, colors, the glove and ball models chosen; checked for length and characters |

g64coopdx ships only 64-bit builds. One session still requires one build id (INFERRED: builds of two commits may run
different logic, also in ways not yet measured). The id names the commit only, unless a release passes its own
(weemitt/studio.mk). Two builds of one commit have the same id, even when one has uncommitted changes or was built
with another compiler. The Linux and Windows builds of one commit have the same id too. They give equal states and
hashes (`make crossbuild`).

### Input from the network

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
