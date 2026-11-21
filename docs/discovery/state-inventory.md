# The game's single-player assumptions

This file covers question 4: every global and structure that exists once per player or assumes one player, where it
lives, who reads and writes it, and what a second (and sixteenth) player needs. It also gives the frame's hard
limits, and, in the details, a measure of the refactor.

Paths start with the repository (`weemitt/`, `gloveworld/`). N64 addresses come from `gloveworld/symbol_addrs.txt`.
**V** (verified) means read in the code or measured by running it. **I** (inferred) means a reading not yet tested.
Nothing in weemitt or gloveworld was changed. The scripts are in [experiments/inventory/](experiments/inventory/).

## Summary

| Fact | Value | Status |
| --- | --- | --- |
| What the game has | 1 glove (`hand`), 1 ball (`ball`), 1 camera, 1 pad read per frame (`pad[ActiveController]`) | V |
| Functions in `weemitt/src/game` | 1,234 in 58 files | V |
| Functions that touch the glove or the ball (or their control blocks) | 225 (18 percent): 193 the glove side, 146 the ball side, 114 both | V |
| Functions that write them | 97 the glove side, 75 the ball side, 45 both | V |
| Identity tests `== &hand`, `== &ball` | 98 tests of the glove in 42 functions; 43 tests of the ball in 17 functions | V |
| Places that store `&hand` or `&ball` (or their positions) in another structure | 56 | V |
| Lines that read the pad through `ActiveController` | 308 | V |
| Puzzle condition types about the glove, the ball or the camera | 16 of 36 | V |
| The random generator | one, `rseed`, about 440 call sites | V |
| Object matrices per frame | 256; measured peak 158 with one idle glove; a glove costs up to 14 | V |

The code is written around two named globals tied together by pointers (`ball.link == &hand` means "held";
`cameraInfo.focus` points at `hand.pos` or `ball.pos`; `bullet->target`). A second glove needs a change in
about 225 functions and about 140 identity tests, each of which needs a decision: "any player", "the
owner of this ball", or "the nearest player".

## Rollback and the inventory

The recommended model is rollback ([netcode.md](netcode.md)): every machine simulates every player. Three facts follow
that the single-player code alone does not show.

1. **Every player's camera is game state.** The glove's stick direction is relative to `camera.rot.y`
   (`weemitt/src/game/hand.c:547-574`, `controller.c:361`). Enemies update only within the far distance of the camera
   plus 200 units (`weemitt/src/game/enemy.c:1300-1301`, V). Puzzles test the camera (`CONDITION_CAM_IN_BOX`,
   `CAM_IN_RANGE`, `puzzle.c:964-966`, V). Sounds fade and stop by their distance from the camera (`sound.c:400-507`, V),
   and a Samtex enemy keeps updating while its sound plays (`enemy.c:1302`, V). So each player's camera runs inside the
   shared simulation, from that player's input (the C buttons are input), and every rule that uses the camera needs a
   rule for N cameras: "within range of any player's camera" for enemy updates, "any camera" for puzzles, and the
   local player's camera only for what is heard and drawn.
2. **The draw path changes the state.** `GraphicsStep` moves animations, places collision spheres and platforms that
   follow objects, calls `Random`, steps the fade (`weemitt/bruteforce/docs/engine.md`, "The trimmed draw", V). It runs
   for every machine in every frame, so it must draw the same for every machine: every glove, every ball, from a
   view that does not depend on who looks. What differs per machine (the camera that draws, the HUD, the names) must
   come after it, outside the logic.
3. **Pause, tips and game over stop the whole game today.** `MainGameLoop` skips almost everything while
   `gamePaused` is set (`weemitt/src/game/main.c:969-1130`, V). In a shared game these become per-player states or
   local overlays.

## The owner's co-op rules

The owner's decisions ([open-questions.md](open-questions.md#decided)) add work that the single-player code does not
have at all.

| Rule | What the game's code needs | Where today | Status |
| --- | --- | --- | --- |
| Players may be in different levels of one world | No change to how one level runs: each level is its own instance ([netcode.md](netcode.md#instances)). The game must start a level from a start record and accept players who arrive later | `LoadLevel` (`weemitt/src/game/game.c:808`) resets one glove, ball and camera | V (code) / I (plan) |
| Players join and leave a running level | A glove, ball and camera that appear or vanish at a frame, set up as `ResetHand`, `ResetBall`, `ResetCamera` do for the level's start, without touching the other players. Every per-player list and table must allow a gap | `ResetHand` (`hand.c:4080-4140`), `ResetBall` (`ball.c:93-190`) | I |
| A ball's real owner never changes | A new per-ball field, apart from `ball.link`, which today means "held by" (`ball.link == &hand`, written in 14 functions). Pickup checks the host's rule: own ball only, or any ball | `IsHandHoldingBall` (`actor.c:77`) and the 130 lines with `ball.link` or `hand.link` | V (code) / I (plan) |
| A lost ball kills its real owner | `BurstBall` (`ball.c:570`), a ball's fall out of the world and a drowned ball must kill the owner, not "the glove" | `ball.c`, `collision.c:1369-1380` | V (code) / I (plan) |
| No lives | `LoseLife` never takes a life and never starts the game over; a death in a bonus level sends only that player out. The game's own cheat bit 8 already skips the loss (`hand.c:3804`) | `LoseLife` (`hand.c:3798-3821`) | V |
| Respawn at the player's own checkpoint | `handControl.checkpoint` is per glove already; the gate test must use the ball of each player (`platform.c:1519-1522` tests the one `ball`). `RestartPlayer` must restart one player and no enemies | `CheckRestartGate`, `RestartPlayer` (`platform.c:1513-1700`) | V |
| Each player exits alone | `CheckForExits` and `ExitLevel` act for one player and remove only that player from the instance; the exit timers become per player | `game.c:2198-2680`, `game.c:3716` | V |
| One world at a time | A session rule in g64coopdx, applied at the hub's world portals | `gameInfo.world` | I |

## What each system needs

Short form of the tables in [Details](#details). "Per player" means one copy for each player, indexed by the
session's player number. "Shared" means one copy for the session, or for the instance where marked.

| System | Per player | Shared | Needs a rule |
| --- | --- | --- | --- |
| Glove | `hand`, `handControl`, the glove's loose globals (`handInertia`, `tongue*`, `catchPull*`, `handQAim`, `tickOnGround` and others), its light, shadow, footprints | | |
| Ball | `ball`, `ballControl`, `extraBall[3]`, `ballStatic`, `ballLeftBehind`, the ball's textures and bits, and a new real owner | | The host's pickup rule; a lost ball kills its owner |
| Camera | `camera`, `cameraInfo`, `cameraUpVect` | Boss and cameo camera scripts | Enemy update range, `CAM_IN_*` puzzles: any camera |
| Input | a pad per player (`pad[4]` covers 4; more need new storage) | | Menus use the local pad |
| Player stats | `health`, `hurtTimer`, power-ups, spells, `controlMethod`, score | Cheats (a session setting) | No lives in co-op |
| Garibs | the combo counters `garibCount`, `garibTimer` | each garib's taken flag, `numGaribs` | The nearest of all gloves and balls takes it |
| Checkpoints | `handControl.checkpoint`: the last gate the player's own ball passed | the gates' look | A respawn must not restart every enemy (`RestartPlayer`, `platform.c:1550`) |
| Death | `KillHand`, `HurtHand`, `deathTimer` | | No lives and no game over |
| Level and exits | the exit timers | `gameInfo` (per instance) | Each player exits alone |
| Save | | the host's `saveSlot` | Clients never write their own save from the session |
| Random numbers | | `rseed` | None: rollback runs every call on every machine in the same order |
| Cutscenes and cameos | | the cameo scripts | Which glove a cameo moves (`cameo.c` uses `&hand`) |
| Pause and tips | the local overlay | | The game's pause stops nothing |
| Bosses | the boss's per-target pain state (`handPain`, `ballPain`) | the boss | Which player a boss targets |
| Bonus levels | `controlMethod` | `bonusTimer` | Who plays; a death sends only that player out |
| Hub | | `hubLevel`, `hubTarget`, the worlds' progress | The ball left behind in the hub, per player |
| HUD | the local player's | | Others shown as name tags |
| Enemies | `hand.link`, `ballControl.enemy`, `handControl.attachedTo` per player | the enemies | Targets: the nearest glove or ball |
| Puzzles | the edge records (`handEdges`, `ballEdges`) per player | the puzzles | "Any glove", "any ball" |
| Collision | | | Identity tests by actor type, not by address |

## Two ways to make the game multi-player

| | Swap the player in and out | Index every use |
| --- | --- | --- |
| How | Keep `hand`, `ball` and the others where they are. Before each player's part of the frame, copy that player's state into them; after, copy it back out | Turn `hand` into `players[i].hand` (or a pointer) at every use; the first player keeps the N64 addresses |
| Speed of a first prototype | Fast: a few hundred lines around `MoveHand`, `MoveBall`, `MoveCam` | Slow: about 225 functions |
| Problems | Pointers stored elsewhere (`ball.link`, `cameraInfo.focus`, `bullet->target`, the actor and collision lists) point at the one global, which holds a different player at different times. Puzzles still see only the player swapped in; enemies and bullets swap in the player they look at (roadmap, phase 3) | None of these; the work is large |
| One player | The swap is a no-op | Each rewrite can change the game; g64coopdx's own baseline shows where it did |
| Use | A spike in phase 1 to find the problems early; not a design to keep | The real design |

Recommendation: start with the swap for two players in one hub level, which shows where the shared state breaks; then
index system by system, each step checked by the determinism tests. The order: input and the glove's state
machine, the ball and its pairing, the camera, collision identity tests, enemies' targets, puzzles, garibs and
pick-ups, checkpoints and death, exits, bosses, cameos.

## The frame's hard limits

| Limit | Value | Measured with one idle glove (29 levels, 600 frames each) | For 16 players |
| --- | --- | --- | --- |
| Object matrices | 256 per frame (`Mtx objectMtx[256]`, `weemitt/include/display.h:9`); no bounds check at the write sites | peak 158 (level 0) | 158 + 15 × (14 + 1) = about 383: must be raised (I) |
| Display list | `Gfx glist[20000]` (160,000 bytes) per buffer; the game drops a task of 20,000 bytes or more (`main.c:578-582`) | peak 11,704 bytes in one task, 14,224 in a frame | likely over the per-task check: must be raised (I) |
| Vertices | 800 per frame (`objectsVtx[2][800]`) | peak 606 (level 37) | depends on what the gloves draw from it: measure |
| Tasks | 10 per buffer | 5 | fine |
| Heap | PC: 22,344,432 bytes (`weemitt/src/port/port_fixups.h:44`); N64: 1,356,528 | 1.31 MB static at most (level 37) | fine on the PC |
| Heap block table | 1,280 blocks | up to 750 static and 95 dynamic | each player adds blocks: measure |
| Enemies | 30 (`enemies[30]`, no bounds check at `enemy.c:152`) | | shared, unchanged |
| Collision sphere hits | 64 a frame (`NUM_SPHERE_HITS`, `collision.c:94`) | | more gloves, more hits: measure |
| CPU matrix stack | 20 | | fine |

These buffers are in `port_ram`. Larger ones can go on the game's heap, with the game's code pointed at them, or the
globals can grow, which means regenerating `gen/` ([weemitt.md](weemitt.md#changes-to-the-game-code)). The measured
peaks are a floor: the glove stood still.

## Details

The tables below are the inventory's full form, with a citation for every row.

### 2. Inventory by system

Columns: symbol and type; where it is defined; N64 address; who touches it (functions R/W from the scan in section 4);
what a second, third, ... player needs. "Index" means turn the single object into `X[player]`. "Duplicate" means a
second instance of the same data. "Shared" means one copy for everybody.

#### 2.1 The glove

| Item | Defined | N64 address | Used by | What N players need | Status |
| --- | --- | --- | --- | --- | --- |
| `hand`: `Actor` (0xE4 bytes: pos, vel, rot, scale, flags, links, pointers to `phys`, `state`, `obj`, `anim`, `shadow`) | `weemitt/include/actor.h:359`; struct `actor.h:231-269` | 0x802902D8 (`gloveworld/symbol_addrs.txt:1749`) | 166 functions touch it (62 write it, 68 counting `&hand.pos` given to writers); 30 files | Index. Each glove needs its own `Actor` and its own `ActorPhysics`, `ActorState`, `ActorObject`, `AnimState`, `ActorShadow` blocks (allocated by `InitActorStructures`, `weemitt/src/game/object.c:124`, called from `ResetHand`) | V |
| The glove's state machine: `hand.state->mode` (`ActorState.mode`, 45 `HAND_*` values) and `prevMode` | `weemitt/include/actor.h:7-27`, enum `weemitt/include/enums.h:68-` | in the heap (pointer in `hand`) | `ApplyAction` (`weemitt/src/game/hand.c:679-1343`), `MaintainAction` (`hand.c:1356-3011`, 1,656 lines), `ConditionsMet` (`hand.c:176`), `MoveHand` (`hand.c:3013`); also read in many other files | Index. Per-glove mode. All the code reads it as `hand.state->mode` | V |
| `handControl`: `HandControl` (0x5B4 bytes) holds action locks and timers, gait, fall time, checkpoint, `attachedTo`, `fakeball`, spell timers, `samples[2]`, and five embedded `Actor`s (`edgeProbe`, `frontProbe`, `orbiter`, `orbiter2`, `orbiter3`) | `weemitt/include/hand.h:10-84` (def), `hand.h:101` (extern) | 0x8028FBD0 (`symbol_addrs.txt:1746`) | 114 functions (66 write), 27 files | Index (one per glove). It is a large struct and `ResetHand` clears it with `memclear(&handControl.actionLock, sizeof(HandControl) - 8)` (`hand.c:4123`) | V |
| `player`: `Player` (0x6C bytes): `score`, `garibs`, `lives`, `health`, `levelFrames`, `powerups[15]`, `hurtTimer`, `spell`, `castSpell`, `castSpellType` | `weemitt/include/hand.h:86-99`, extern `hand.h:102` | 0x80290180 (`symbol_addrs.txt:1747`) | 61 functions (34 write), 18 files | Per player for health, hurtTimer, spells, power-ups and score; lives are not used; garibs are shared in the instance (all decided) | V |
| Loose glove state outside the structs: `handInertia` (0x8028FBCC), `levelHandScale` (0x801EEC40), `tongueTime` (0x801EEC04), `tongueBullet`, `tongueStuck`, `tongueOffsetPos` (0x8029F398), `handQAim`, `catchPullBall`, `catchPullHand`, `locateRandomAim`, `blur1`, `blur2`, `cartwheelAccel`, `camPos`, `camPos2`, `ballWalkDismountFrame`, `prewhackCancelled`, `throwAimCancelled`, `swishSpeed` | declared as `extern` or `static` near the top of `hand.c` (`hand.c:32-150`) | see `symbol_addrs.txt` | written by `hand.c` functions (list from the scan: `tongueOffsetPos` 9 writes, `catchPull*` 6 each, `vertQ` 6, `tongueTime` 5) | Index or move into `HandControl`. They are hidden per-glove state: if left global, glove 2 overwrites glove 1 mid-frame | V (names) / I (that each is per-glove) |
| Port-side glove state: `handFallLifted` (`weemitt/include/actor.h:395`), `tickOnGround` (static, `weemitt/src/game/hand.c:145`) | `actor.h:395` | outside `port_ram` | 2 functions | Index. These are the frame-rate rewrite's extras | V |
| Glove light, footprints, shadow: `handLightList` (`weemitt/include/light.h:48`, 0x802988C8); `handControl.footprint`; `hand.shadow` | light.h:48 | 0x802988C8 | `light.c` (2 functions write), `footprint.c`, `shadow.c` | Index (a light per glove) or share one light | V |
| Models: the glove model is found by name (`FindObject`) and its `ObjectInstance` is held in `hand.inst` | `weemitt/src/game/hand.c:4080-4140` | n/a | `ResetHand` | Duplicate the instance (animation state lives in `Actor.anim`, which is per actor) | I |

#### 2.2 The ball

| Item | Defined | N64 address | Used by | What N players need | Status |
| --- | --- | --- | --- | --- | --- |
| `ball`: `Actor` | `weemitt/include/actor.h:360` | 0x8029F978 (`symbol_addrs.txt:1813`) | 128 functions touch it (61 write), 29 files | Index (one ball per player) | V |
| `ballControl`: `BallControl` (0x164 bytes): `type`, `state`, `loseTimer`, `enemy` (held by an enemy), `releaseTimer`, `cancerTimer`, `hitFlags`, `samples[2]`, `snow`, `transformState`, `numPlasters`, `orbiter` | `weemitt/include/ball.h:6-33`, extern `ball.h:34` | 0x8029F468 (`symbol_addrs.txt:1806`) | 74 functions (31 write), 19 files | Index | V |
| Ball forms: `ballControl.type` takes the `BallMode` values 0 normal (rubber), 1 bowling, 2 power, 3 bearing (ball-bearing), 4 beach, 5 snow, 6 crystal | `weemitt/include/enums.h:242-251` | n/a | `ChangeBallInto` (`weemitt/src/game/ball.c:1275`); `ActivateCollectable(nextBallState[...])` (`ball.c:1300`); `MoveBall` (`ball.c:230`) | Per ball. Ball textures and bits are loose globals: `ballTextureList`, `ballTextureData`, `ballTexturePalette`, `ballBitTexture*`, `snowBallBit`, `plaster[]` (21 writes in `ball.c`), `crystalBall`, `crystalBallBit` | V |
| Ball state: `ballControl.state` 0 normal, 1 held, 2 burst, 5 held special | `enums.h:253-258` | n/a | 8 functions write it | Per ball | V |
| The glove and ball are paired by pointer: `ball.link == &hand` means held. `IsHandHoldingBall()` (`weemitt/src/game/actor.c:77`) is called from 28 places; `ball.link` or `hand.link` appear on 130 lines | actor.c:77, actor.h:371 | n/a | puzzle, exit, water, enemy, collision, camera | Replace `&hand` by "this ball's owner" (`link` points at the glove that holds it). Every `== &hand` test needs the glove that holds this ball, not the global | V |
| Ball-only globals: `ballWithHand` (0x801EFCF8, 6 fns), `ballStatic` (0x801EFCFC, 19 fns, 10 write), `ballLeftBehind` (0x801EFD00, 9 fns) | declared as `extern` in `ball.c:65`, `game.c:210,348,376` | see left | `game.c:2381-2485` (hub: the ball is left behind or stored when you change level), `ball.c`, `collect.c`, `cheat.c`, `bullet.c:1475` | Per player for `ballStatic` and `ballLeftBehind` (they are hub carry-over for one ball) | V |
| `extraBall[3]`: 3 extra `Actor`s drawn as the lumps of the "cancer" ball (`GiveBallCancer`) | `weemitt/src/game/ball.c:42`, `ball.c:185-195` | 0x80297940 | `ball.c`, `object.c:216` | Index (3 per ball) | V |
| Ball model and `ResetBall` | `weemitt/src/game/ball.c:93-190` | n/a | `LoadLevel` (`game.c:448-450`) | Call once per player | V |

#### 2.3 Camera

| Item | Defined | N64 address | Used by | What N players need | Status |
| --- | --- | --- | --- | --- | --- |
| `camera`: `Actor` | `weemitt/include/camera.h:63` | 0x8028F8E0 | 69 functions (26 write), 26 files | One camera per player, simulated in the instance: enemy updates, puzzles and sounds read the camera (see [Rollback and the inventory](#rollback-and-the-inventory)). Each machine draws from its own player's camera | V / I |
| `cameraInfo`: `CameraInfo` (0x1D4 bytes) with `focus` and `focusVel` pointers (set to `&hand.pos` or `&ball.pos`), orbit, override timers and flags | `weemitt/include/camera.h:7-61`, extern line 64 | 0x8028F9C8 | 70 functions (63 write), 22 files | Per player, in the instance. Each `focus` points to that player's glove or ball | V |
| Camera follows glove or ball: `UpdateActorPositionDependencies` switches `cameraInfo.focus` between `&hand.pos` and `&ball.pos` (`weemitt/src/game/actor.c:1272`, 1430-1440), `TrackBall()` decides | actor.c:1272 | n/a | | The decision is per player | V |
| The glove's stick direction is relative to the camera: `camera.rot.y` is read in `hand.c:547-574`, `hand.c:2733`, `controller.c:361-362`, `controller.c:440-441` | | | | Every machine simulates every player's camera from that player's input (the C buttons are input), so the yaw is known everywhere and need not be sent | I |
| Scripted camera: `SetCameraOverride` (`weemitt/src/game/camera.c:144`), used by cameos, bosses, bonus levels | camera.c:144 | n/a | `cameo.c`, `boss.c`, `bonus.c` | Per player; a boss's or a cameo's scripted camera applies to every player in the instance | V |
| Camera upvector: `cameraUpVect`, `cameraUpVectAim` | `camera.h:65-66` | | `game.c`, `main.c`, `quake.c`, `water.c` | Per client | V |
| Camera is updated once per frame: `UpdateActor(&camera)` then `MoveCam()`, `PlayerSpinCamera()` | `weemitt/src/game/main.c:1057-1091` | | | Once per player per frame, in the instance | V |

#### 2.4 Input

| Item | Defined | N64 address | Used by | What N players need | Status |
| --- | --- | --- | --- | --- | --- |
| `pad[4]`: `Pad` (0x190 bytes each: stick, magnitude, `button[14]`, `prevButton`, `pressed`, `latch`, `holdTime`, `sincePress`) | `weemitt/include/controller.h:6-27` | 0x801FAA90 (`symbol_addrs.txt:1643`); room for 5 (the 5th is the debug pad) | 50 functions touch `pad` (339 reads, 114 writes), 21 files | The array already has 4 slots. Add a player index to the input path | V |
| `ActiveController` (`u8`): the one pad index that is processed | `weemitt/include/controller.h:30` | 0x80202218 | read on 308 lines of `src/game`; set only in `RunGameInit` (`weemitt/src/game/main.c:610-611`) | Replace by "the pad of this player". Menu code can keep `ActiveController` for the local pad | V |
| `controllerdata[]` (`OSContPad`), `controllerPresent`, `autoPlayback`, `recordKeys`, `playbackButtons` | `controller.h:29`, `controller.c` | 0x801FB280 | `controller.c` | Per player for `controllerdata`. The demo playback (`ReadControllerFromData`) already shows a way to feed `pad[]` from stored data | V |
| What reads the pad each frame: `ControllerStep` calls `osContStartReadData`, `osContGetReadData(controllerdata)` (all 4 pads) and then `ReadController(ActiveController)` only | `weemitt/src/game/main.c:780-794` | | | Call `ReadController(i)` for each player. In the PC port `osContGetReadData` fills only pad 0 (`weemitt/src/port/os.c:198-229`) | V |
| `initControllers` marks `pad[i].present` for each plugged pad but returns the lowest index and sets `controllerPresent` FALSE if that is not pad 0 | `weemitt/src/game/controller.c:78-107` | | | Keep | V |
| Gameplay readers (the part a co-op change must touch): `ProcessInput` (`controller.c:378`), `MoveHand`, `MaintainAction`, `ApplyAction`, `ConditionsMet`, `DismountBall`, `DismountEnemy` (all `hand.c`), `MoveBall`, `MoveHandToBall` (`ball.c`), `MoveCam`, `PlayerSpinCamera` (`camera.c`), `UpdateActorCollisionAndGravity`, `UpdateActorPositionDependencies` (`actor.c`), `DeactivateCollectable`, `DoCarnivalBonus`, `DoSpaceBoss3`, `NMEProcessHubChicken`, `UpdateTip`, `UpdatePause`, `CheckForExits`, cheat menu (4), ballworld (6) | | | | 25 gameplay functions. Another 25 are menu or screen functions (title, level select, save slots, end screens) that can stay on the local pad | V |
| Port: `deltaTime` ticks the pad in `delta.c` (`DeltaHoldInput`, `DeltaKeepInput`, `DeltaTakePress`, `DeltaTickPressed`) with `tickPresses[4][...]` already sized 4 | `weemitt/src/game/delta.c:220-255` | | | Fine for 4 pads. More than 4 players needs a larger array | V |
| Input recorder: `weemitt/src/port/record.h` records "the controller values of every update" for replay | | | | A base for sending inputs: it records `(dt, buttons, stick x, stick y)` per update. Deterministic replay is an existing feature of the headless build | V |

#### 2.5 Lives, energy, garibs, score and power-ups

| Item | Defined | Used by | What N players need | Status |
| --- | --- | --- | --- | --- |
| `player.lives` | `weemitt/include/hand.h:90` | writes: `collect.c:855` (`GainExtraLife`), `hand.c:3806` (`LoseLife`), `game.c:1334,1361,3269`, `eeprom.c:222` (save); read for the HUD `overlay.c:998` | Not used: no lives in co-op (decided). `LoseLife` decides game over with `player.lives == -1` (`hand.c:3807-3810`) and sets `gamePaused` | V |
| `player.health` (3 hearts) | `hand.h:91` | 14 writes, 6 reads: `HurtHand` (`hand.c:3716`), `collect.c`, `game.c`, `ballworld.c` | Per player | V |
| `player.garibs` (count in this level), `numGaribs` (total in the level, 0x80299398) | `hand.h:89`, `collect.c:26` | `GainGarib` (`collect.c:860`), `UpdateGaribs` (`collect.c:157`), HUD `overlay.c:1041-1048`; `player.garibs` is zeroed in `LoadLevel` (`game.c:815`) | Shared by the players in the instance (decided). Collection is first come: the garib's `active` flag lives in the level's data (`Garib`, `weemitt/include/collect.h`) | V |
| Garib pick-up logic: for each garib it tests the ball (if `ballControl.loseTimer == 0`) and the glove (if `hand.state->mode != HAND_JOINED`) and takes the nearer (`weemitt/src/game/collect.c:239-265`). The combo counters `garibCount`, `garibTimer` are single globals (0x801EFDB6, 0x801EFDB4) | collect.c:157-330 | | Loop over players, pick the nearest. Make the combo per player | V |
| `player.score`, extra life every `difficultyStats[...].lifeScore` | `hand.h:88`; `AddToScore` `collect.c:885` | | Per player | V |
| `player.powerups[15]`, `player.spell`, `castSpell`, `castSpellType`, `player.hurtTimer`; glove side: `handControl.spellTimer[3]`, orbiters, `handControl.frog` | `hand.h` | `ActivateCollectable` (`collect.c:487`), `DeactivateCollectable` (`collect.c:651`), `UpdatePickups` (`collect.c:346`), `spell.c`, HUD `overlay.c:1055-1101` | Per player. Pickups have 15 types (`weemitt/include/enums.h:260-276`): stun, death, frog, suction, hercules, speedup, rotor blades, bowling, power, ball bearing, beach, snow, crystal, boomerang, vanish | V |
| `memclear(&player.score, 100)` clears the player block at new game and continue | `game.c:1333,1360` | | Needs a per player version | V |
| Cheats: `cheatActivated` (bit mask, 0x80297BEC) 26 functions read it, 8 write it; bits seen: 8 no life loss (`hand.c:3804`), 0x40 no damage (`hand.c:3738`), 0x20 | `weemitt/src/game/cheat.c` | | Shared (a room setting) | V |

#### 2.6 Checkpoints, respawn, death

| Item | Defined | What N players need | Status |
| --- | --- | --- | --- |
| `handControl.checkpoint`: `Platform *` of the restart gate last touched | `weemitt/include/hand.h` (offset 0x510); set in `CheckRestartGate` (`weemitt/src/game/platform.c:1513-1536`) | Per player pointer. The gate itself (`PlatformRestartGate`: `untouched`, `targetScale`, `restartTimer`, `cameraPos`) is shared level data | V |
| The gate fires when the ball (not the glove) falls through its ring: `DistanceBetweenPoints2D(&ball.pos, ...)` | `platform.c:1519-1522` | Loop over balls | V |
| `RestartPlayer` runs the whole respawn: gate timer 65, resets glove, ball, camera, spells, and restarts every enemy with `RestartEnemy` (except bosses) | `platform.c:1550-1700` | Respawn of one player must not restart all enemies for everyone. Needs a per player version | V |
| `LoseLife`: bonus level exits (`ExitLevel(BONUS_DONE_EXIT)`), else `player.lives--`, `gameOverCount = 1; gamePaused = TRUE` when lives run out, `player.health = 3`, gate `restartTimer = 65` | `weemitt/src/game/hand.c:3798-3821` | Per player. Game over for one player must not pause everyone | V |
| `HurtHand` (`hand.c:3716`) and `KillHand` (`hand.c:3648`): 37 lines call or declare `HurtHand`; they set `handControl.deathTimer = 80` and the hurt animation | | Per player | V |
| Death sources: fall (`fallTime >= 17`, or `shadow.altitude == -1` for too long, `hand.c:3055-3067`); hurt platform flag 0x4000 (`collision.c:1369-1380`); ball burst (`BurstBall`, `ball.c:570`); enemies and bullets; water | | Per player, already tied to the actor identity tests | V |
| `numRestartGates` (`parse.c:58`) | | Shared | V |

#### 2.7 Level, hub, exits, save data

| Item | Defined | N64 address | What N players need | Status |
| --- | --- | --- | --- | --- |
| `gameInfo`: `GameInfo` (mode, level, prevLevel, world, `worldComplete[7]`, fade, `exitType`, `cameoActive`, `exitPlatform`, language) | `weemitt/include/game.h:6-24`, extern line 41 | 0x801E7530 | Shared. 144 functions read it (42 write). The host decides the level | V |
| Levels: 48 (`HUB1`..`OUTRO`, `NUM_LEVELS`), 7 worlds (hub, Atlantis, Carnival, Pirates, Prehistoric, Fear, Space), 8 hub levels, 6 boss levels (Atlantis, Carnival, Pirates, Prehistoric, Fear, Space 3), 6 bonus levels | `weemitt/include/enums.h:286-347` | | Shared | V |
| Exit triggers: `CheckForExits` (`weemitt/src/game/game.c:2198`) picks the glove position for exit types 6-12, 21 and a few levels (`pos = &hand.pos`, `game.c:2576-2582`) and the ball position for the others (`pos = &ball.pos`). An exit needs the glove to hold the ball (`IsHandHoldingBall() \|\| ball.link == &hand`, `game.c:2620-2623`). Timers `levelExitTimer` (0x801E7460), `worldExitTimer` (0x801E7620), `newLevExitTimer` (0x801E7466) are single globals | game.c:2198-2680 | | Each player exits alone (decided). The "pull onto exit" code moves `ball.pos` and `hand.pos` (`game.c:2194-2211`, `PullOntoExit`). The exit leaves the ball "left behind" in the hub (`ballStatic = 2`, `game.c:2653-2660`) | V |
| `ExitLevel(type, pause, complete)` | `game.c:3716` | | Shared; once per room | V |
| `LoadLevel(num)` runs `ResetHand(1)`, `ResetBall()`, `ResetCamera()`, `SeedRandom()`, `player.garibs = 0`; level start comes from `TOKEN_HAND_START`, `TOKEN_BALL_START`, `TOKEN_CAMERA_START` in the level file (one start per level) | `game.c:808-1460`; parse `weemitt/src/game/parse.c:570-595` | | Per player start positions around the one start point | V |
| Save: `saveSlot[6]` (`SaveSlot`, 0x1C bytes: name, `levels` bit mask, `allGaribs` bit mask, `score`, `lives`, `difficultyLevel`, `hubTarget`, `hubLevel`, `visited`, volumes), `currentSaveSlot` | `weemitt/include/save.h:6-21` | 0x801EAA44, 0x801EAB24 | One save for the room (the host's). 36 functions read `saveSlot` (14 write). Clients should not write their own save from the host's progress | V |
| EEPROM layout (4 Kbit, 512 bytes): block 0 id, blocks 1-6 high scores (0x30 bytes), blocks 7-0x1E best times (0xC0), blocks 0x1F-0x33 save slots (0xA8), blocks 0x34-0x37 CRCs (0x20). Blocks 0x38-0x3F are free (64 bytes) | `weemitt/src/game/eeprom.c:111-206` | | Shared or not touched | V |
| Progress: `UpdateSaveSlot` writes `score = player.score + player.garibs * 10`, `lives`; `levels` and `allGaribs` bits set in `weemitt/src/game/game.c:3461-3479` (`all garibs` only when `player.garibs == numGaribs`) | | | The "all garibs" bit is set when one instance's players have them all (decided) | V |
| Hub carry: `saveSlot.hubLevel`, `hubTarget`, `ballLeftBehind`, `worldComplete[]` | `game.c:2381-2485` | | Shared | V |
| Time trial: `bestTimes[6][5]` (`BestTime`, 6 bytes), `player.levelFrames` | `save.h:28-31`; `game.c:2259` | | Shared | V |

#### 2.8 Random numbers

| Item | Defined | N64 address | What N players need | Status |
| --- | --- | --- | --- | --- |
| `rseed` (`u32`): one xorshift-style generator, `Random(lim)` (`weemitt/src/game/maths.c:18-29`), `SeedRandom()` sets 0x6151F (`maths.c:31`), called in `LoadLevel` (`game.c:1454`) | `maths.c:13` | 0x801ED3F0 | Keep one shared generator. All clients must run the same number of `Random()` calls in the same order. About 440 call sites, many for effects only (debris, weather, sound pitch). In a lockstep design these stay in step because every client runs everything. In a state-sync design the cosmetic ones can use a second generator. Port: `frame.c:350-353` restores the seed outside ticks | V / I |
| `deathRandom` (`u8`) | `enemy.c:54` | | Constant | V |

#### 2.9 Cutscenes, cameos, pause

| Item | Defined | What N players need | Status |
| --- | --- | --- | --- |
| Cameos (scripted sequences): `cameoList` (0x801F9628), `StartCameo` (`weemitt/src/game/cameo.c:187`), `UpdateCameos` (`cameo.c:210`), `gameInfo.cameoActive` read in 11 files. Cameo items address the glove: `actor = &hand` in `cameo.c:161,288,330,412`, `pos = &hand.pos` in `cameo.c:397,430` | | Shared script; each cameo needs a "which glove" choice. While `cameoActive` and `handControl.actionLock`, `ApplyAction` returns early (`hand.c:698-700`) | V |
| `StartCameo` is skipped in hub 1 coming back from level 8 or 9 (`cameo.c:191`) | | | V |
| Pause: `gamePaused` (`u8`, 0x801EC7D2) read by 26 functions, `pauseMode` (0x801EC7D3), `disablePause`. Start on `pad[ActiveController]` opens the pause menu (`controller.c:388-391`). `MainGameLoop` skips almost everything when `gamePaused` (`main.c:969-1130`) | | Online cannot freeze the world for one player. Pause must become a local overlay, and `gamePaused` set by the game over / tip / exit code needs another meaning | V / I |
| Tips: `readingTip` pauses the game while a sign is read (`UpdateTip`, `tip.c`) | | Same issue | V |
| Intro, outro, fly-through and presentation levels (`INTRO`, `OUTRO`, `PRESENT`, `FLYTHRU`, `FLYTHRU2`) hide the glove and ball (`hand.alpha = 0`, `main.c:811-818`) | | Skip online | V |

#### 2.10 Bosses, bonus levels, hub

| Item | Defined | What N players need | Status |
| --- | --- | --- | --- |
| `DoBoss` (`weemitt/src/game/boss.c:176`) switches on the level to `DoCarnivalBoss` (`boss.c:386`), `DoPiratesBoss` (336), `DoFortressBoss` (739, the Fear boss), `DoPrehistBoss` (1185), `DoSpaceBoss3` (1373); Atlantis boss has no function (script only) | | Boss code reads `&hand`, `&ball` directly: 57 word matches of `hand`, 42 of `ball` in `boss.c`. Boss state is in file statics (`handPain`, `ballPain`, `bossFocalPoint`, `frankieDead`, ...). Needs "which player" for each target, and the Space boss 3 level makes the glove a robot (`HAND_ROBOT`, `hand.c:4131-4140`) | V |
| Bonus levels: `DoCarnivalBonus`, `DoFortressBonus`, `DoPrehistoricBonus`, `DoAtlantisBonus`, `DoPiratesBonus` (`boss.c` / `bonus.c`), time limit `bonusTimer` (0x801FA754), `LoseLife` in a bonus level exits (`hand.c:3799-3803`). `SetControlToBall()`/`SetControlToHand()` (`bonus.c:320,333`) | | Per player controlMethod. `controlMethod` (0x801EC740, 80 reads in 25 functions) is a global: 0 camera, 1 hand, 2 ball (`enums.h:236-239`) | V |
| Ballworld mode (a port feature, "every level is played as the ball alone"): `weemitt/src/game/ballworld.c`, 700 lines | | A working precedent for steering the ball from `pad[ActiveController]` and keeping `player.health` aside, but it still has one ball | V |
| Hub levels (8) and cave (`CAVE`, `DoCaveStuff`, `game.c:2822`), `WAYROOM` | | Shared | V |

#### 2.11 Spells and power-ups on the glove

| Item | Where | What N players need | Status |
| --- | --- | --- | --- |
| Spells are collected into `player.spell/castSpell`; cast state in `handControl.castSpellTime`, `spellTimer[3]`, `handControl.orbiter*` (`HandleSpellOrbiters`, `hand.h`) and `player.powerups[]` | `spell.c`, `collect.c:487-815` | Per player | V |
| Enemy spells hit the glove or the ball: `ActivateCollectable(bullet->spell, ...)` after a hit (`bullet.c:1476`) | | Needs the hit player | V |

#### 2.12 HUD

| Item | Where | What N players need | Status |
| --- | --- | --- | --- |
| `stats[MAX_STATS=9]` overlay slots (garibs, score, health, lives and power-up bars); `numLivesOL`, `garibsGotOL` | `weemitt/include/overlay.h:160`, `enums.h:454-459`; `PrintStats` `weemitt/src/game/overlay.c:975-1105` reads `player.*` directly | One HUD for the local player. Drawing other players' hearts needs a second set of overlays or a compact name tag | V |

#### 2.13 Enemies and puzzles (who they look at)

| Item | Where | Finding | Status |
| --- | --- | --- | --- |
| Enemy target choice reads the globals directly. `NMEConditionsMet` (`weemitt/src/game/nmeprocess.c:465`) computes `ballDist` and `handDist` from `ball.pos`, `hand.pos` and takes the nearer: `closestActor = (ballDist < handDist) ? &ball : &hand` (`nmeprocess.c:489-491`). Conditions `IF_HAND_IN_RANGE`, `IF_BALL_IN_RANGE` and 2D versions use them (`nmeprocess.c:505-530`) | | Two candidates, hard coded. Needs a loop over all gloves and balls (nearest by distance), keeping the `deathTimer` / `loseTimer` / `attachedTo` / `ballControl.enemy` checks per player | V |
| `NMEActionFacePlayer`, `NMEActionFollowPlayer`, `NMEActionFleePlayer`, `NMEActionCharge` pick `&hand.pos` or `&ball.pos`, with the nearer of the two (`weemitt/src/game/enemy.c:937-1113`) | | Same | V |
| Enemy holds the glove or ball: `hand.link = enemy`, `ball.link = enemy`, `ballControl.enemy`, `handControl.attachedTo` (`enemy.c:1236-1290`, `nmeprocess.c:316-330`) | | Per player | V |
| Bullets: `bullet->target = &hand` / `&ball` / `&enemies[x].actor` (`bullet.c:1090, 1493-1496, 1509`; `nmeprocess2.c:689, 2640`) and `BulletHitTarget` (`bullet.c:1346-1362`) | | `target` is a pointer to an `Actor`, so it already works with any actor. The places that choose it use the globals | V |
| Enemy script data (in the level file) names conditions by `hand` or `ball` (`IF_HAND_IN_RANGE`, ...); `NME_FOLLOWPLAYER` and `NME_HOLDPLAYER` flags 0x40 (glove) and 0x80 (ball) (`nmeprocess.c:483-500`) | `weemitt/include/enums.h:619-` | Data in the 48 levels. Semantics "the glove" must become "any glove" | V |
| Puzzle and switch conditions: 36 `CONDITION_*` types, 16 of them about the glove, ball or camera (`HAND_ON`, `BALL_ON`, `HAND_TOUCHING`, `BALL_TOUCHING`, the JUST_ versions, `HAND_CROSSES_LINE`, `HAND_IN_BOX`, `HAND_IN_RANGE`, `BALL_IN_BOX`, `BALL_IN_RANGE`, `HAND_IN_CYLINDER`, `CAM_IN_BOX`, `CAM_IN_RANGE`) (`weemitt/include/enums.h:691-728`). Evaluated in `weemitt/src/game/puzzle.c:949-1102`. Edges (just on, just touched) come from `handEdges` and `ballEdges` (`puzzle.c:226-227`) | | "Any glove or ball" for the shared world. The edge records are per actor and need an array. `CAM_IN_*` tests the camera, which differs per client | V |
| Collision treats the glove and ball as special actors: 98 identity tests with `&hand` (23 in `collision.c`, 19 in `actor.c`, 13 in `game.c`, 12 in `hand.c`) and 43 with `&ball` | | Test "is a player glove" by actor type (`HAND`, `BALL` types exist: `hand.type = HAND`, `hand.c:4085`, `ball.type = BALL`, `ball.c:~147`) instead of by address | V |
| Water: `UpdateWater` reads `aPtr == &hand`, `aPtr == &ball`, `hand.state->waterState` (`weemitt/src/game/water.c:132-216, 501`) | | Same identity tests | V |
| Death plane equivalents: hurt platforms (`platform->flags & 0x4000`) call `HurtHand(HURT_GO_TO_SAFE)` only when `actor == &hand`, and `BurstBall()` when `actor == &ball` (`collision.c:1369-1380`). Falling out of the world: `fallTime >= 17` and no floor below (`hand.c:3064-3067`) | | Same | V |
| Shadows, footprints, light, buzzers, spikes, vents, earthquakes: each reads `hand`/`ball` (shadow.c: 23 refs to `hand`) | | | V |

### 3. Frame limits and hard-coded sizes

| Limit | Value | Where | Measured use | Status |
| --- | --- | --- | --- | --- |
| Object matrices per frame | 256 (`Mtx objectMtx[256]` inside `Dynamic`); `objectMatrix` index has no bounds check at the four write sites | `weemitt/include/display.h:9`; sites `object.c:687,694`, `shadow.c:195`, `footprint.c:226`; reset `object.c:309` | peak 158 (hub level 0), 138 (level 21), 128 (level 35); glove draw costs up to 14 matrices (sampled in level 10), ball 1, other actor types 0 to 20 | V (peak), I (the glove cost depends on its pose) |
| Display list buffer | `Gfx glist[20000]` = 160,000 bytes in `Dynamic`; two buffers (`dynamic[2]`) | `display.h:15`; `main.c:151` | peak 14,224 bytes in one frame (sum of the tasks) | V |
| Per-task list check | `data_size >= 20000` bytes prints "Overran the graphics list" and drops the task (the `Crash` after it is unreachable) | `weemitt/src/game/main.c:578-582` | peak 11,704 bytes in one task (59 percent) | V |
| Tasks per frame | `tlist[2][10]` (10 per buffer) | `main.c:164, 919-931` | 5 per frame observed | V |
| Vertices per frame | 800 per draw buffer (`objectsVtx[2][800]`), no bounds check seen at `vtxPtr` increments | `weemitt/src/game/object.c:74, 312` | peak 606 (level 37), 414 (level 27) | V |
| CPU matrix stack | `Matrix stack[20]` | `weemitt/include/maths.h:10-14` | | V |
| Heap | N64: `JallocInit(jallocHeapStart, 0x80400000 - jallocHeapStart)` = 0x14F2F0 = 1,356,528 bytes (`gloveworld/src/main.c:917`; `jallocHeapStart` 0x802B0D10). PC port: runs to 0x1800000 past the base (`weemitt/src/port/port_fixups.h:44`), 22,344,432 bytes | `main.c:918` | static part at frame 400, idle: level 0 1,301,584; level 10 988,624; level 12 1,204,496; level 13 965,120; level 27 1,198,208; level 37 1,312,048; level 39 973,984. Dynamic part 800 to 6,600 | V (measured on the PC build; the used sizes are the game's own allocation sizes) |
| Heap block table | `JALLOC_NUM_BLOCKS 0x500` = 1,280 blocks of 0x14 bytes; statics grow up, dynamics grow down | `weemitt/include/jalloc.h:7-20`, `jalloc.c:327-340` | up to 750 static blocks (level 12) and 95 dynamic blocks (level 0) in use at frame 400 | V |
| Enemies | `enemies[30]` (0x802996E8 to 0x8029F0F0 holds 30 x 0x300), "The game has room for 30 enemies" | `weemitt/src/game/puzzle.c:66-67`; `enemy.c:152-160` allocates `&enemies[numberOfEnemies++]` with no bound check | | V (no check) / I (size from addresses) |
| Debris | `debris[120]` | `weemitt/src/game/debris.c:29, 89` | | V |
| Bullets | list in the heap; `MAX_BULLET_TRACKS 256` | `weemitt/src/game/bullet.c:103` | | V |
| Collision sphere hits per frame | 64 (`NUM_SPHERE_HITS`) | `weemitt/src/game/collision.c:94` | | V |
| Collision spheres per actor | 8 (`ActorObject.spheres[8]`) | `weemitt/include/actor.h:51` | | V |
| Puzzle variables | `puzzleVal[100]` | `weemitt/include/puzzle.h:67` | | V |
| Path arrivals, water event stamps, edge slots | 64, 16, (30 enemies, 4 platforms, 16 confined) | `platform.c:70`, `water.c:68`, `puzzle.c:66-70` | | V |
| HUD stat slots | 9 | `weemitt/include/enums.h:459` | | V |
| Cameos per script | 20 items (`items[0x14]`) | `weemitt/include/cameo.h:25` | | V |
| Console memory | Heap sizes above are for the 4 MB console. Glovebox's builder has `--expansion`, a mod that may use the Expansion Pak (the emulator always has it) | Glovebox `CLAUDE.md` | | I (not checked in Glovebox code) |

How the measurements were made: `gdb` on `weemitt/build/headless/weemitt` with the player's ROM from the upload folder
(not copied, not committed). A breakpoint at `main.c:578` (`CleanupAndSendDisplayList`) printed `objectMatrix`,
`actualGFXListLength[currentTask]`, `vtxPtr - objectsVtx[...]` and `jallocControl`. Each of 29 levels ran 600 original
frames with no input, so the glove stood still and the numbers are a floor, not a worst case. The levels were 0, 8-18,
20-23, 25-28, 30-33, 35-39. Table below.

#### 3.1 Measured peaks per level (idle glove, 600 frames each)

| level | peak object matrices (of 256) | peak vertices (of 800) | peak bytes in one task (check 20000) | peak bytes in a frame | frames |
| --- | --- | --- | --- | --- | --- |
| 0 | 158 | 174 | 8888 | 10896 | 598 |
| 8 | 105 | 222 | 11704 | 13640 | 598 |
| 9 | 93 | 186 | 9904 | 11840 | 598 |
| 10 | 66 | 222 | 9200 | 11232 | 598 |
| 11 | 73 | 234 | 8224 | 10336 | 598 |
| 12 | 118 | 369 | 11008 | 13120 | 598 |
| 13 | 76 | 278 | 6976 | 9336 | 598 |
| 14 | 95 | 180 | 7520 | 9656 | 599 |
| 15 | 100 | 180 | 8536 | 10648 | 598 |
| 16 | 49 | 174 | 8256 | 10368 | 598 |
| 17 | 86 | 174 | 7576 | 9816 | 598 |
| 18 | 84 | 174 | 6728 | 8896 | 598 |
| 20 | 50 | 234 | 7088 | 9192 | 598 |
| 21 | 138 | 180 | 10312 | 12416 | 598 |
| 22 | 40 | 174 | 5712 | 7816 | 598 |
| 23 | 118 | 201 | 7856 | 10096 | 598 |
| 25 | 55 | 174 | 5912 | 11360 | 598 |
| 26 | 115 | 252 | 9792 | 11904 | 598 |
| 27 | 93 | 414 | 10944 | 12976 | 598 |
| 28 | 57 | 174 | 6984 | 12704 | 598 |
| 30 | 40 | 286 | 8568 | 14224 | 598 |
| 31 | 71 | 174 | 7784 | 9888 | 598 |
| 32 | 38 | 174 | 6096 | 11848 | 598 |
| 33 | 81 | 218 | 7448 | 9576 | 598 |
| 35 | 128 | 366 | 9464 | 11576 | 598 |
| 36 | 61 | 174 | 6752 | 8864 | 598 |
| 37 | 90 | 606 | 10936 | 13040 | 598 |
| 38 | 75 | 228 | 4768 | 6784 | 598 |
| 39 | 80 | 174 | 7152 | 8720 | 598 |
| max | 158 | 606 | 11704 | 14224 | |

### 4. Measuring the refactor: every use of the glove and ball globals

#### 4.1 How the script works

`inv/access.py` runs clang on every `weemitt/src/game/*.c` with the game's own compile flags (the Makefile's
`GAME_CXXFLAGS`: C++14, `-m32`, `-malign-double`, `-DPORT`, `-include src/port/port_pre.h`) and dumps the syntax tree
(`-Xclang -ast-dump`). It parses the text dump as a tree and, for each use of a chosen global, climbs the chain of
member accesses and array indexes (`hand.pos.x`, `pad[0].button[3]`, `(&hand.pos)->x` from the vector macros) to the
whole expression, then looks at what the parent does with it:

| Parent | Recorded as |
| --- | --- |
| `=`, `+=`, `-=`, ... on the left side; `++` / `--`; a C++ `operator=` (struct copy, `Ptr32` assignment) | write (compound forms and `++` also count as a read) |
| `&` of the chain, or an array member that decays to a pointer | address taken, with the consumer: `call:Name#argument`, `cmp` (identity test), `store-ptr` (`x->field = &hand`), `assign` (local pointer), `return` |
| anything else | read |

The macros `COPY_VECTOR(&hand.pos, ...)` and `ZERO_VECTOR(&ball.vel)` expand to `*(&x) = ...` and `(&x)->x = ...`. The
script treats `&` followed by `*` or `->` as the same object, so these count as writes.

An address that goes to a function counts as a write when the function and argument position are on a list of
functions that modify what they receive (`WRITER` in `report.py`: `AnimateActor#0`, `AccelerateActor2D#0`,
`SetVector2D#0`, `SubVector2D#0`, `AddToOneScaledVector#0`, `QuatSlerp#0`, `ActorHandleRotaim#0`, `SquashActor#0`,
`ActorStartRoll#0`, `memclear#0`, and about 40 more). The list was chosen from the prototypes (`include/maths.h`,
`include/actor.h`: first argument is the result). It is a judgment, not a proof. 253 address uses of the glove side and 64
of the ball side hit that list.

Validation: the scan's event count for `hand` and `ball` per file was compared with `grep -ow` word counts. For
`hand.c` the scan finds 1,644 `hand` events against 1,468 word matches (macros expand and are counted per use), for
`actor.c` 163 against 144, `game.c` 56 against 57, `camera.c` 38 against 26, `puzzle.c` 30 against 31. The small
differences are macro expansion and a few words in comments or strings.

Limits (the numbers are a floor, not a bound):

- A write through a copy of the address (`Actor *a = &hand; a->vel.x = 1`) is seen only as an address use at the `&`.
  There are 26 glove-side and 17 ball-side `assign` uses and 153 identity tests (104 and 49, counting `&hand.pos` style forms); the functions that receive `&hand` as a parameter
  (`AnimateActor`, `AccelerateActor2D`, `UpdateActor`, `DestroyAndFreeActor`, ...) write `hand` through the
  parameter. These are the generic actor functions and they work for any actor, so they are not a per-player problem
  by themselves.
- A write through a pointer stored in the actor (`hand.state->mode = x`, `hand.obj->...`, `hand.phys->...`) counts as a
  **read** of `hand` (it reads the pointer). `hand.state->mode` alone is used on hundreds of lines. These blocks are
  separate allocations, so a per-player refactor has to allocate one set of them per player but does not have to
  change those lines if the global becomes a lookup.
- Code that walks `actorList`, `collisionList` or `distList` reaches `hand` and `ball` through the list without naming
  them (`UpdateActor`, `DrawActor`, collision loops). Not counted. They must work for any number of actors already,
  because the lists hold enemies and platforms.
- Static variables with the same name in two files are merged in the `all/` scan (the hand and ball scan is exact
  for `hand`, `ball`, `handControl`, `ballControl`, `player`).
- Only `src/game/*.c` is scanned. `weemitt/src/port` also refers to the glove and ball (`hostworld.c` 70 lines,
  `frame.c` 26, `devview.c` 10, `platform.c` 9, `spacedemo.c` 8). `weemitt/bruteforce` has its own copy of the engine
  and is not counted.
- "Functions" are definitions in the .c files of `src/game` (1,234). Functions that only pass the address along to
  another function are counted once per function.

#### 4.2 Rerun

```sh
cd experiments/inventory
sh run_all.sh                       # scan of hand, ball and friends, scan of every global, tables.md
python3 access.py --targets hand,ball,handControl,ballControl,player,camera,cameraInfo   # just the scan (about 6 s)
python3 report.py events.json > tables.md                    # the tables below
python3 report_globals.py NAME1,NAME2 > globals.md           # readers and writers of any global (needs all/events.json)
ROM=/path/to/rom.z64 sh run_all.sh   # also the gdb probes (frame limits); needs gdb and weemitt/build/headless/weemitt
```

`events.json` has one record per use: file, line, function, global, first-level field, kind and consumer. Example query:
`python3 -c "import json; print([e for e in json.load(open('events.json')) if e['func']=='MoveBall' and e['kind']=='write'])"`.

#### 4.3 Results

##### Per global (all of src/game, 1234 functions defined in 58 files)

| Global | reads | writes | addr taken | of which to a writer callee | functions touching | functions writing |
| --- | --- | --- | --- | --- | --- | --- |
| `hand` | 1826 | 483 | 649 | 215 | 166 | 68 |
| `handControl` | 650 | 383 | 67 | 38 | 114 | 67 |
| `ball` | 978 | 360 | 256 | 52 | 128 | 66 |
| `ballControl` | 292 | 116 | 19 | 12 | 74 | 31 |
| `player` | 145 | 64 | 2 | 2 | 61 | 34 |
| `camera` | 268 | 80 | 59 | 13 | 69 | 30 |
| `cameraInfo` | 247 | 278 | 16 | 9 | 70 | 64 |
| `gameInfo` | 568 | 186 | 4 | 0 | 144 | 42 |
| `pad` | 339 | 114 | 2 | 0 | 50 | 16 |
| `saveSlot` | 93 | 31 | 11 | 5 | 36 | 14 |

##### Distinct functions

| Set | functions |
| --- | --- |
| touch `hand` or `handControl` | 193 |
| touch `ball` or `ballControl` | 146 |
| touch both sides | 114 |
| touch either side | 225 |
| touch the `hand` Actor itself | 166 |
| touch the `ball` Actor itself | 128 |
| touch `hand` and `ball` Actors | 99 |
| write hand side (incl. address to a writer callee) | 97 |
| write ball side | 75 |
| write both sides | 45 |
| any of the 10 scanned globals | 347 |
| functions defined in src/game | 1234 |

##### By file

| File | System | hand side fns | ball side fns | both | hand R | hand W | hand &(writer) | hand & other | ball R | ball W | ball &(writer) | ball & other |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| src/game/hand.c | glove | 34 | 12 | 12 | 1580 | 515 | 206 | 187 | 282 | 92 | 13 | 27 |
| src/game/ball.c | ball | 14 | 24 | 11 | 92 | 22 | 2 | 7 | 346 | 122 | 18 | 32 |
| src/game/actor.c | actors | 7 | 7 | 6 | 150 | 42 | 4 | 29 | 117 | 43 | 4 | 23 |
| src/game/controller.c | input | 4 | 1 | 1 | 113 | 24 | 7 | 5 | 56 | 12 | 6 | 2 |
| src/game/nmeprocess2.c | enemies | 20 | 14 | 13 | 64 | 39 | 2 | 32 | 81 | 17 | 9 | 14 |
| src/game/collect.c | pick-ups | 6 | 5 | 5 | 47 | 32 | 6 | 22 | 50 | 29 | 2 | 5 |
| src/game/game.c | game flow | 11 | 6 | 6 | 23 | 23 | 8 | 29 | 54 | 33 | 2 | 10 |
| src/game/platform.c | actors | 6 | 4 | 4 | 34 | 44 | 1 | 5 | 17 | 28 | 1 | 6 |
| src/game/boss.c | bosses and bonus | 5 | 4 | 4 | 59 | 10 | 2 | 13 | 38 | 11 | 2 | 9 |
| src/game/bullet.c | enemies | 10 | 5 | 5 | 45 | 24 | 4 | 23 | 13 | 7 | 1 | 3 |
| src/game/ballworld.c | ball | 12 | 16 | 8 | 18 | 8 | 0 | 0 | 41 | 19 | 0 | 8 |
| src/game/enemy.c | enemies | 9 | 12 | 8 | 21 | 16 | 0 | 17 | 18 | 13 | 0 | 14 |
| src/game/bonus.c | bosses and bonus | 5 | 4 | 2 | 29 | 32 | 5 | 14 | 5 | 0 | 0 | 3 |
| src/game/collision.c | actors | 4 | 4 | 4 | 14 | 2 | 0 | 26 | 48 | 2 | 0 | 26 |
| src/game/nmeprocess.c | enemies | 4 | 3 | 2 | 20 | 3 | 0 | 9 | 32 | 3 | 2 | 8 |
| src/game/shadow.c | effects | 3 | 2 | 2 | 45 | 0 | 0 | 4 | 1 | 0 | 0 | 2 |
| src/game/puzzle.c | puzzles | 3 | 3 | 3 | 29 | 0 | 0 | 4 | 14 | 1 | 0 | 2 |
| src/game/water.c | level features | 4 | 1 | 1 | 9 | 0 | 0 | 2 | 19 | 9 | 0 | 4 |
| src/game/camera.c | camera | 6 | 2 | 2 | 28 | 3 | 0 | 7 | 1 | 3 | 0 | 2 |
| src/game/quake.c | level features | 2 | 2 | 2 | 14 | 7 | 3 | 3 | 7 | 1 | 3 | 2 |
| src/game/cheat.c | pick-ups | 3 | 2 | 2 | 4 | 2 | 0 | 0 | 12 | 8 | 0 | 2 |
| src/game/personal.c | debug | 1 | 1 | 1 | 14 | 0 | 0 | 5 | 5 | 7 | 0 | 2 |
| src/game/main.c | game flow | 2 | 2 | 2 | 4 | 5 | 1 | 0 | 4 | 4 | 1 | 0 |
| src/game/present.c | game flow | 1 | 1 | 1 | 8 | 0 | 1 | 1 | 3 | 4 | 0 | 1 |
| src/game/parse.c | level parser | 3 | 2 | 2 | 1 | 3 | 0 | 0 | 0 | 7 | 0 | 0 |
| src/game/spike.c | level features | 1 | 1 | 1 | 6 | 0 | 0 | 1 | 2 | 0 | 0 | 1 |
| src/game/spell.c | enemies | 3 | 2 | 2 | 2 | 3 | 0 | 2 | 1 | 1 | 0 | 1 |
| src/game/cameo.c | cameos | 4 | 0 | 0 | 0 | 5 | 0 | 14 | 0 | 0 | 0 | 0 |
| src/game/footprint.c | effects | 2 | 0 | 0 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| src/game/debris.c | effects | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| src/game/overlay.c | HUD and overlays | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 |
| src/game/object.c | actors | 2 | 2 | 1 | 0 | 0 | 1 | 1 | 1 | 0 | 0 | 1 |
| src/game/buzzer.c | level features | 1 | 1 | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 |
| **total** | | 193 | 146 | 114 | 2476 | 866 | 253 | 463 | 1270 | 476 | 64 | 211 |

##### By system (files outside hand.c and ball.c show who else reaches in)

| System | hand side fns | ball side fns | hand R+W+& | ball R+W+& |
| --- | --- | --- | --- | --- |
| glove | 34 | 12 | 2335 | 387 |
| ball | 26 | 40 | 140 | 548 |
| actors | 19 | 17 | 337 | 292 |
| enemies | 46 | 36 | 307 | 224 |
| bosses and bonus | 10 | 8 | 157 | 64 |
| input | 4 | 1 | 149 | 65 |
| pick-ups | 9 | 7 | 107 | 102 |
| game flow | 14 | 9 | 98 | 106 |
| level features | 8 | 5 | 45 | 41 |
| effects | 6 | 2 | 54 | 3 |
| puzzles | 3 | 3 | 33 | 16 |
| camera | 6 | 2 | 38 | 6 |
| debug | 1 | 1 | 19 | 13 |
| cameos | 4 | 0 | 19 | 0 |
| level parser | 3 | 2 | 4 | 7 |
| HUD and overlays | 0 | 1 | 0 | 2 |

##### Address uses (how the `&hand` / `&ball` / `&hand.pos` forms are used)

| Use | hand side | ball side |
| --- | --- | --- |
| ArraySubscriptExpr | 1 | 1 |
| ConditionalOperator | 1 | 1 |
| assign | 26 | 17 |
| call (other) | 279 | 136 |
| call (writer) | 253 | 64 |
| cmp | 104 | 49 |
| return | 3 | 0 |
| store-ptr | 49 | 7 |

Notes: `cmp` is an identity test such as `actor == &hand`. `store-ptr` stores the address in a field (for example `cameraInfo.focus = &hand.pos`, `ball.link = &hand`). `assign` copies the address to a local or global pointer.

##### Most written fields

| Global.field | writes (incl. ++, +=) | functions |
| --- | --- | --- |
| `hand.vel` | 163 | 32 |
| `ball.vel` | 136 | 37 |
| `ball.pos` | 76 | 24 |
| `hand.scale` | 64 | 9 |
| `hand.rotAim` | 63 | 19 |
| `hand.pos` | 58 | 18 |
| `handControl.actionLock` | 56 | 18 |
| `hand.flags` | 53 | 28 |
| `ball.flags` | 47 | 26 |
| `ball.scale` | 30 | 6 |
| `hand.friction` | 28 | 13 |
| `handControl.releaseTimer` | 27 | 15 |
| `ball.link` | 26 | 14 |
| `hand.rot` | 25 | 10 |
| `handControl.frontProbe` | 18 | 5 |
| `handControl.inputAccel` | 18 | 2 |
| `handControl.orbiter3` | 16 | 5 |
| `ballControl.releaseTimer` | 14 | 9 |
| `ballControl.orbiter` | 12 | 2 |
| `handControl.gait` | 12 | 4 |
| `hand.alpha` | 11 | 9 |
| `ballControl.state` | 9 | 8 |
| `hand.link` | 9 | 9 |
| `handControl.samples` | 9 | 3 |
| `handControl.orbiter` | 9 | 2 |
| `handControl.idleTime` | 9 | 7 |
| `ballControl.transformState` | 8 | 8 |
| `ballControl.samples` | 8 | 1 |
| `ballControl.returnTime` | 8 | 5 |
| `ballControl.loseTimer` | 8 | 6 |

##### Functions that write both hand side and ball side

| Function | File |
| --- | --- |
| CheckActorForTeleport | src/game/actor.c |
| UpdateActor | src/game/actor.c |
| UpdateActorCollisionAndGravity | src/game/actor.c |
| UpdateActorPositionDependencies | src/game/actor.c |
| UpdateActorTeleport | src/game/actor.c |
| BurstBall | src/game/ball.c |
| LoseBall | src/game/ball.c |
| MoveBall | src/game/ball.c |
| MoveBallToHand | src/game/ball.c |
| ReleaseBall | src/game/ball.c |
| ShatterBall | src/game/ball.c |
| DoFortressBoss | src/game/boss.c |
| DoPiratesBoss | src/game/boss.c |
| MaintainBulletBomb | src/game/bullet.c |
| MaintainBulletSnowball | src/game/bullet.c |
| PlayerSpinCamera | src/game/camera.c |
| ActivateCheat | src/game/cheat.c |
| ActivateCollectable | src/game/collect.c |
| DeactivateCollectable | src/game/collect.c |
| CheckActorCollisions | src/game/collision.c |
| ProcessInput | src/game/controller.c |
| NMEActionHoldPlayer | src/game/enemy.c |
| UpdateEnemy | src/game/enemy.c |
| CheckForExit | src/game/game.c |
| CheckForExits | src/game/game.c |
| DoCaveStuff | src/game/game.c |
| FreeLevel2 | src/game/game.c |
| ApplyAction | src/game/hand.c |
| CheckHandCatapult | src/game/hand.c |
| HurtHand | src/game/hand.c |
| KillHand | src/game/hand.c |
| MaintainAction | src/game/hand.c |
| MoveHand | src/game/hand.c |
| UpdateBlastCircle | src/game/hand.c |
| GraphicsStep | src/game/main.c |
| MainGameLoop | src/game/main.c |
| NMEProcessInstruction | src/game/nmeprocess.c |
| NMEProcessRobot | src/game/nmeprocess2.c |
| UpdateEnemyBlastring | src/game/nmeprocess2.c |
| ParseLandscapeFile | src/game/parse.c |
| ParseTokenGeneric | src/game/parse.c |
| RestartPlayer | src/game/platform.c |
| DoPresentScreen | src/game/present.c |
| DoExplosion | src/game/quake.c |
| ActivateSpellHand | src/game/spell.c |

##### Functions outside hand.c and ball.c that write hand side or ball side

| Function | File | writes |
| --- | --- | --- |
| CheckActorForTeleport | src/game/actor.c | ball.link, hand.alpha, hand.flags, handControl.joinTimer |
| SetTeleportScale | src/game/actor.c | ball.scale |
| UpdateActor | src/game/actor.c | ball.vel, hand.friction, hand.vel, handControl.lastGroundPlatform, handControl.lastGroundPos |
| UpdateActorCollisionAndGravity | src/game/actor.c | ball.pos, hand.vel |
| UpdateActorPositionDependencies | src/game/actor.c | ball., ball.flags, ball.pos, ball.rotQuat, ball.vel, ballControl.fallTime, ballControl.state, hand.pos, hand.rotAim, hand.vel, handControl.actionLock, handControl.clingPlanes, handControl.edgeProbe, handControl.frontProbe |
| UpdateActorTeleport | src/game/actor.c | ball.pos, ball.scale, hand.pos, hand.scale |
| BallworldEndFrame | src/game/ballworld.c | hand.alpha |
| BallworldThrownBegin | src/game/ballworld.c | handControl.releaseTimer |
| BallworldThrownEnd | src/game/ballworld.c | handControl.releaseTimer |
| BlastCircle | src/game/ballworld.c | handControl.blastPos, handControl.blastSize, handControl.footprint |
| Float | src/game/ballworld.c | ball.vel |
| Mend | src/game/ballworld.c | ballControl.numPlasters |
| PachinkoFrame | src/game/ballworld.c | ball.pos, ball.vel |
| PachinkoInput | src/game/ballworld.c | ball.vel |
| PachinkoPuzzles | src/game/ballworld.c | handControl.actionTime |
| Pound | src/game/ballworld.c | handControl.actionTime |
| PoundFrame | src/game/ballworld.c | ball.flags, ball.vel |
| Stick | src/game/ballworld.c | ball.vel |
| TakeBall | src/game/ballworld.c | ball.flags, ball.pos |
| Transform | src/game/ballworld.c | ballControl.transformState |
| DoAtlantisBonus | src/game/bonus.c | hand.vel, handControl.spellTimer |
| DoCarnivalBonus | src/game/bonus.c | hand., hand.flags, hand.link, hand.pos, hand.rotQuat, hand.vel, handControl.samples |
| DoControlRoom | src/game/bonus.c | hand., hand.flags, hand.pos, hand.rotAim, hand.vel |
| SetControlToBall | src/game/bonus.c | hand.alpha, hand.scale |
| SetControlToHand | src/game/bonus.c | hand.alpha, hand.scale |
| DoCarnivalBoss | src/game/boss.c | ball.vel |
| DoFortressBoss | src/game/boss.c | ball.vel, hand.vel |
| DoPiratesBoss | src/game/boss.c | ball.pos, hand.pos |
| DoPrehistBoss | src/game/boss.c | ball.pos, ball.vel |
| DoSpaceBoss3 | src/game/boss.c | hand.alpha, hand.pos, hand.rotQuat, hand.vel |
| DestroyBulletSnowball | src/game/bullet.c | hand.flags |
| MaintainBulletBomb | src/game/bullet.c | ball.vel, hand.flags, hand.friction, hand.pos, hand.vel |
| MaintainBulletBubble | src/game/bullet.c | hand.vel, handControl.flags, handControl.joinTimer |
| MaintainBulletSnowball | src/game/bullet.c | ball.prevVel, ball.vel, hand., hand.flags, hand.link, hand.rotQuat, handControl.attachOffset, handControl.attachTag, handControl.attachedTo, handControl.dropTimer, handControl.releaseTimer, handControl.status |
| UpdateBullets | src/game/bullet.c | handControl.smokeTimer |
| ProcessCameoInstruction | src/game/cameo.c | hand.link, handControl.actionLock, handControl.dropTimer, handControl.orbiter3 |
| PlayerSpinCamera | src/game/camera.c | ball.vel, hand.vel |
| ActivateCheat | src/game/cheat.c | ball.flags, ball.pos, ball.scale, handControl.spellTimer |
| ActivateCollectable | src/game/collect.c | ball.flags, ball.friction, ball.phys, ballControl.returnTime, ballControl.snow, hand.flags, handControl.actionLock, handControl.morphReverse, handControl.morphStep, handControl.orbiter3, handControl.rotorSpeed, handControl.suctionTime |
| DeactivateCollectable | src/game/collect.c | ball.flags, ball.friction, ball.link, ball.phys, ball.platPrevPos, ball.pos, ball.prevPos, ball.prevVel, ball.vel, ballControl.orbiter, ballControl.releaseTimer, ballControl.returnTime, ballControl.snow, hand.flags, hand.friction, hand.rot, hand.rotAim, hand.rotQuat, handControl.actionLock, handControl.morphReverse, handControl.morphStep, handControl.orbiter, handControl.orbiter2, handControl.orbiter3, handControl.releaseTimer, handControl.suctionPlane |
| GainGarib | src/game/collect.c | hand., hand.rotAim, hand.vel, handControl.actionLock |
| UpdatePickups | src/game/collect.c | handControl.castSpellTime |
| CheckActorCollisions | src/game/collision.c | ballControl.hitFlags, handControl.touchedActor |
| MeasureInputAccel | src/game/controller.c | handControl.inputAccel |
| ProcessInput | src/game/controller.c | ball., ball.rotAim, ball.vel, hand., hand.friction, hand.rotAim, hand.vel, handControl.inputAccel |
| KillDebris | src/game/debris.c | handControl.fakeball |
| KillEnemy | src/game/enemy.c | ball.flags |
| NMEActionHoldPlayer | src/game/enemy.c | ball.flags, ball.link, ball.pos, ballControl.enemy, ballControl.enemyOffset, ballControl.enemyRot, ballControl.enemySphere, ballControl.releaseTimer, ballControl.state, hand.flags, hand.link, handControl.actionLock, handControl.attachOffset, handControl.attachTag, handControl.attachedTo, handControl.dropTimer, handControl.releaseTimer, handControl.status |
| UpdateEnemy | src/game/enemy.c | ballControl.enemyRot, hand.link, handControl.dropTimer, handControl.releaseTimer |
| RemoveFootprint | src/game/footprint.c | handControl.footprint |
| CheckForExit | src/game/game.c | ball.flags, ball.pos, ball.vel, ballControl.transformState, hand.rotAim, handControl.actionLock |
| CheckForExits | src/game/game.c | ball.alpha, ball.flags, ball.pos, ball.vel, hand., hand.scale |
| ContinueGame | src/game/game.c | handControl.idleTime |
| DoCaveStuff | src/game/game.c | ball.flags, ball.link, ball.pos, ball.vel, ballControl.releaseTimer, hand., hand.rot, hand.rotAim, hand.rotQuat, handControl.actionLock, handControl.noControlTimer, handControl.releaseTimer |
| FreeLevel2 | src/game/game.c | ball., ballControl.numPlasters, ballControl.samples, hand., handControl.edgeProbe, handControl.frontProbe, handControl.noControlTimer, handControl.samples |
| GameOverScreen | src/game/game.c | hand.alpha, hand.pos |
| InitPlayerForLevel | src/game/game.c | handControl.idleTime |
| StartNewGame | src/game/game.c | handControl.idleTime |
| StartNewLevel | src/game/game.c | handControl.idleTime |
| UpdatePause | src/game/game.c | handControl.checkpoint |
| GraphicsStep | src/game/main.c | ball.alpha, hand.alpha |
| MainGameLoop | src/game/main.c | ball., ball.flags, ball.prevVel, ballControl.hitFlags, hand., hand.flags, handControl.flags, handControl.prevRot, handControl.touchedActor |
| NMEProcessBugle | src/game/nmeprocess.c | ball., ball.vel |
| NMEProcessInstruction | src/game/nmeprocess.c | ball.vel, hand.flags, hand.rotAim, hand.vel |
| NMEProcessBabySpank | src/game/nmeprocess2.c | ball.link, ball.vel, ballControl.enemy |
| NMEProcessBovva | src/game/nmeprocess2.c | ball.vel |
| NMEProcessChuck | src/game/nmeprocess2.c | ball.flags, ball.vel, ballControl.enemyOffset |
| NMEProcessEvilGlove | src/game/nmeprocess2.c | hand.alpha, handControl.orbiter3, handControl.rotorSpeed |
| NMEProcessFumble | src/game/nmeprocess2.c | ball.vel |
| NMEProcessRobot | src/game/nmeprocess2.c | ball.flags, ball.pos, hand.flags, hand.pos, hand.rotAim, hand.vel, handControl.actionLock |
| NMEProcessSelwynCrab | src/game/nmeprocess2.c | hand.flags, hand.friction, hand.vel |
| NMEProcessSelwynWhale | src/game/nmeprocess2.c | ball.pos, ball.vel |
| NMEProcessSpank | src/game/nmeprocess2.c | hand.flags, hand.friction, hand.vel |
| NMEProcessSucker | src/game/nmeprocess2.c | ball., ball.flags, ball.pos, ball.vel |
| NMEProcessSwish | src/game/nmeprocess2.c | hand.flags, hand.link, handControl.attachTag, handControl.attachedTo, handControl.dropTimer, handControl.releaseTimer, handControl.status |
| UpdateEnemyBlastring | src/game/nmeprocess2.c | ball.vel, hand.flags, hand.vel |
| HandSwish | src/game/object.c | hand.rotQuat |
| ParseLandscapeFile | src/game/parse.c | ball.link, ball.pos, handControl.fakeball |
| ParseTokenGeneric | src/game/parse.c | ball.link, ball.pos, hand.pos |
| ParseTokenPlatformFeatures | src/game/parse.c | handControl.checkpoint |
| ProcessInputPersonal | src/game/personal.c | ball.flags, ball.pos, ball.prevPos, ball.vel |
| CheckRestartGate | src/game/platform.c | handControl.checkpoint |
| RestartPlayer | src/game/platform.c | ball., ball.pos, ball.radius, ball.scale, ball.spinSpeed, ball.vel, ballControl.cancerTimer, ballControl.fallTime, ballControl.hitFlags, ballControl.loseTimer, ballControl.releaseTimer, ballControl.state, hand.flags, hand.link, hand.pos, hand.rot, hand.rotAim, hand.rotVel, hand.scale, hand.vel, handControl.actionLock, handControl.deathTimer, handControl.fallTime, handControl.morphStep, handControl.noControlTimer, handControl.releaseTimer, handControl.samples, handControl.spellTimer |
| DoPresentScreen | src/game/present.c | ball.flags, ball.pos, hand.pos |
| PuzzleExecuteAction | src/game/puzzle.c | ball.flags |
| DoExplosion | src/game/quake.c | ball., ball.vel, hand., hand.flags, hand.rotAim, hand.vel |
| ActivateSpellHand | src/game/spell.c | ball.link, handControl.morphStep, handControl.releaseTimer, handControl.spellTimer |
| WaterDoFloating | src/game/water.c | ball.vel |

#### 4.4 Other globals that assume one player (readers and writers)

From `inv/all/events.json` (every global in `src/game`). "functions" counts distinct functions touching the symbol.

| Global | reads | writes | & | functions | writing fns | files |
| --- | --- | --- | --- | --- | --- | --- |
| `hand` | 1826 | 483 | 649 | 166 | 62 | 30 |
| `ball` | 978 | 360 | 256 | 128 | 61 | 29 |
| `handControl` | 650 | 383 | 67 | 114 | 66 | 27 |
| `ballControl` | 292 | 116 | 19 | 74 | 31 | 19 |
| `player` | 145 | 64 | 2 | 61 | 34 | 18 |
| `camera` | 268 | 80 | 59 | 69 | 26 | 26 |
| `cameraInfo` | 247 | 278 | 16 | 70 | 63 | 22 |
| `gameInfo` | 568 | 186 | 4 | 144 | 42 | 42 |
| `pad` | 339 | 114 | 2 | 50 | 16 | 21 |
| `ActiveController` | 339 | 2 | 0 | 56 | 1 | 22 |
| `controlMethod` | 80 | 4 | 0 | 25 | 4 | 13 |
| `ballWithHand` | 10 | 8 | 0 | 6 | 4 | 5 |
| `ballStatic` | 25 | 14 | 0 | 19 | 10 | 11 |
| `ballLeftBehind` | 16 | 30 | 0 | 9 | 7 | 3 |
| `saveSlot` | 93 | 31 | 11 | 36 | 14 | 12 |
| `currentSaveSlot` | 123 | 16 | 0 | 35 | 9 | 13 |
| `numGaribs` | 11 | 7 | 0 | 10 | 7 | 5 |
| `rseed` | 6 | 4 | 0 | 2 | 2 | 1 |
| `gamePaused` | 28 | 16 | 0 | 26 | 10 | 13 |
| `pauseMode` | 4 | 3 | 0 | 4 | 2 | 3 |
| `disablePause` | 1 | 4 | 0 | 5 | 4 | 4 |
| `cameoList` | 23 | 4 | 6 | 13 | 2 | 8 |
| `cheatActivated` | 45 | 14 | 0 | 26 | 8 | 14 |
| `levelExitTimer` | 14 | 4 | 0 | 9 | 4 | 4 |
| `worldExitTimer` | 8 | 4 | 0 | 6 | 4 | 4 |
| `newLevExitTimer` | 4 | 2 | 0 | 3 | 2 | 2 |
| `bonusTimer` | 1 | 0 | 0 | 1 | 0 | 1 |
| `enemies` | 219 | 75 | 115 | 55 | 23 | 21 |
| `numberOfEnemies` | 35 | 2 | 0 | 18 | 2 | 9 |
| `stats` | 22 | 17 | 1 | 10 | 9 | 6 |
| `handLightList` | 3 | 4 | 1 | 2 | 2 | 1 |
| `extraBall` | 1 | 10 | 3 | 4 | 2 | 2 |
| `levelHandScale` | 34 | 2 | 0 | 9 | 1 | 4 |
| `handInertia` | 14 | 3 | 0 | 11 | 2 | 6 |
| `garibCount` | 6 | 3 | 0 | 1 | 1 | 1 |
| `garibTimer` | 2 | 2 | 0 | 1 | 1 | 1 |
| `gameOverCount` | 14 | 5 | 0 | 5 | 2 | 4 |
| `platformList` | 44 | 6 | 62 | 37 | 3 | 12 |
| `puzzleVal` | 25 | 7 | 0 | 7 | 2 | 2 |
| `bossFocalPoint` | 5 | 9 | 2 | 2 | 2 | 1 |
| `earthQuake` | 20 | 8 | 0 | 7 | 2 | 5 |
| `bestTimes` | 21 | 9 | 30 | 12 | 4 | 5 |

#### 4.5 Summary of the counts

- The refactor reach is 225 of 1,234 functions (18 percent). 114 of them touch both a glove and a ball, so most are
  gameplay functions that handle the pair.
- `hand.c` is the core: 34 glove-side functions, 1,580 reads, 515 writes and 393 address uses of the glove. `ball.c` is 24 ball-side
  functions. These two files are where a per-player object has to be added first.
- Outside `hand.c` and `ball.c` the heaviest users are `actor.c` (7 glove, 7 ball functions), `controller.c` (4/1),
  `nmeprocess2.c` (20/14, enemy behaviors), `collect.c` (6/5), `game.c` (11/6), `platform.c` (6/4), `boss.c` (5/4),
  `bullet.c` (10/5), `ballworld.c` (12/16, a port module that already steers the ball), `enemy.c` (9/12) and
  `bonus.c` (5/4).
- The most written fields are `hand.vel` (163 writes in 32 functions), `ball.vel` (136 in 37), `ball.pos` (76 in 24),
  `hand.scale` (64), `hand.rotAim` (63), `hand.pos` (58 in 18), `handControl.actionLock` (56 in 18), `hand.flags` and
  `ball.flags` (53 and 47). `ball.link` is written in 14 functions (26 writes): that is the glove-to-ball
  pairing.
- 98 + 43 identity tests, and 56 places that store the address of the glove or ball in another structure, are the
  hardest part. They need a decision per site: "any player" (a loop), "the owner of this ball" (use `link`), or
  "the nearest player".
- The generic actor functions (`AnimateActor`, `UpdateActor`, `AccelerateActor2D`, ...) take an `Actor *` and need no
  change. 121 calls of `AnimateActor(&hand, ...)` would become `AnimateActor(&p->hand, ...)`.

