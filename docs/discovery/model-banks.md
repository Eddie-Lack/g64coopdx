# Models in level packs

This page is the plan for custom models in a level pack ([level-pack.md](level-pack.md)). A model is an object
the game draws, such as a platform or a piece of scenery. The game reads models from banks. A bank is one block of
the ROM: a texture bank holds pictures, and an object bank holds objects that use those pictures. The work is in
steps. Each step is one pull request. The table at the end says which steps are done.

## What a pack may hold

The owner decided these points.

- A pack may carry the author's own models: models imported from glTF files and Glovebox's kit models. They come
  in one object bank and one texture bank of the pack's own. Both are stored uncompressed.
- A pack may not carry a reskin or a copy of a retail model. Both hold the game's own data.
- A pack may also name retail banks by number. A retail bank is one of the ROM's own banks. Each player's game
  loads it from their own ROM. Only the number crosses the network. The session already checks that every player
  has the same ROM.
- The models in a pack are at most 4 MB.
- Custom models may be platforms, if a test shows that they give the same game on Linux and Windows. The test
  passed ("Platforms" below).

## The file

A pack gains one optional file, `models.bin`. The pack tool finds it by its presence, as it finds `script.lua`.
`pack.txt` keeps `format=1`. A pack without `models.bin` keeps the id it has today.

All words are big-endian, as in the game's banks.

| Offset | Size | What |
| --- | --- | --- |
| 0 | 4 | T, the size of the pack's texture bank, or 0 |
| 4 | 4 | O, the size of the pack's object bank, or 0 |
| 8 | 1 | Flags. Bit 0: search the retail texture banks before the level's own. The other bits are 0. |
| 9 | 1 | The number of retail texture banks, 0 to 4 |
| 10 | 1 | The number of retail object banks, 0 to 4 |
| 11 | 1 | 0 |
| 12 | a + b | The retail texture bank numbers, then the retail object bank numbers, one byte each |
| H | T | The pack's texture bank. H is 12 + a + b, rounded up to a multiple of 8. The bytes before it are 0. |
| H + T | O | The pack's object bank |

- T and O are multiples of 8. They are both 0 or both more than 0.
- T + O is at most 4 MiB.
- The flag is 0 when the file names no retail texture bank.
- Each retail number appears once. The font and generic texture banks and object banks 0 and 1 are refused.
- The file is exactly H + T + O bytes long.
- The pack's id hashes `models.bin` after `script.lua`, in the same form as the other files.

## The check

A bank from a pack is untrusted. The game trusts every field of a bank: its loaders follow its offsets and counts
with no bounds. So the client checks each bank in full before the game sees it, as it checks a level file
([level-file-safety.md](level-file-safety.md)). The check is `src/client/bankcheck.c`. It reads the bytes as the
game's loaders do, and it refuses anything it does not understand.

Some rules allow only what Glovebox writes today. A test on Glovebox's real output pins each of them. A new kind
of model in Glovebox widens the rule and its test together.

What the check refuses, in short. No rule counts faces or display list commands across the bank: the 4 MiB limit
bounds them. The renderer stops a display list after 100000 commands (`gfx_dl.c`); that changes only
the picture.

- A bank that starts with `FLA2`. The game would decompress it, and its decompressor has no bounds.
- Any byte that no structure reaches and that is not 0.
- A texture record whose size, format, dimensions or palette differ from what Glovebox writes. A texture that no
  object uses.
- An animated texture. The game keeps at most 20 of them, with no bound check.
- A pointer that is not aligned, that has high bits set, or that names a range outside the bank.
- Two structures that share bytes. The loaders fix each pointer each time they reach it, so a shared one breaks.
- An object tree deeper than 32 or with more than 4096 objects.
- An object tree with more than 10 levels of children. Each level keeps one matrix on the game's matrix stack,
  which has 20 entries, and one entry of `parentScaleStack`, which has 10.
- A face index outside its mesh's vertices.
- A display list command that Glovebox does not write, or a texture load in another form than Glovebox's. A
  texture load must hold the exact words Glovebox writes for the size of its texture.
- An object with the name of the ball or the hand. The game points the ball's and the hand's textures at those.
  Glovebox names an object after its glTF file and does not refuse these five names today (`gball`, `cball`,
  `GShinL`, `GShinR`, `gbl_bt`). Its pack export should refuse or change them.
- A float that is not finite, a position past 16384 or a scale past 64.
- An object whose position, added up along its parents, is past 16384. The game translates a child by its position
  times its parent's scale, and keeps the parent's translation while the child draws. The sum counts each
  position's length times the largest part of the parent's scale. A rotation does not change a length. The bound
  holds for an actor at the origin with scale 1: `guMtxF2L` turns each matrix entry times 65536 into a 32-bit
  integer, which overflows past 32768. The actor's own position and scale come from the level, and add to it.
- A rotation key that is not near unit length. Glovebox writes a key of all zeros today. The game turns it into a
  matrix of NaN values (not-a-number). The pack export writes the identity key in its place.

With the player's ROM, the client also refuses:

- A pack id of an object or texture that the ROM already has.
- A texture, a mesh's vertex list or a G_VTX block that is equal to one in the ROM. A vertex list or a block of
  less than 64 bytes is allowed, and so is a texture of one color.
- A level file that names an object found in no bank the level will have.
- A level whose actors would need more memory for their collision than the budget allows.

The check's API is `g64_models_check` and `g64_models_check_rom` (`src/client/bankcheck.h`). Each returns a
message and the offset in `models.bin` of the fault. The caller gives the first one the game's bank numbers: the
game's program asks `G64BankRom` (`g64_rombanks_game_range`). Programs that do not link the game, the pack tool and
the tests, use a table of the game's bank switch (`src/client/bankranges.h`, written by `tools/gen_bankranges.py`).
`make rombanks` compares the table with `G64BankRom`. The second one applies the first two rules that need the
ROM. The level rules come in step 7.

The pack reader (`src/client/pack.h`) runs the check on every `models.bin`, from a folder too. The online program
runs the rules that need the ROM once, in `netplay_begin`, before the game is built. Only then has the game read
the banks' places from the ROM's code. So a host whose models fail these rules finds out only when the game starts.
`g64pack check DIR --rom ROM` runs the same rules. Both refuse an index of the ROM's banks that lacks banks of a
kind or skipped a bank (`g64_rombanks_complete` in `src/client/rombanks.h`).

A fuzz target (`tests/fuzz/bankcheck_fuzz.c`) runs the check on random banks, on changes to Glovebox's cube, and
on banks made from random shapes. For every bank the check accepts, it runs a copy of the game's loaders that stops
at any read or write outside the bank (`tests/fuzz/bankwalk.c`).

`make bankwalk` (`tests/rom/bankwalk.c`) proves that this copy does what the game does. The game loads the test's
banks through `G64LoadPackBanks` in a real level: Glovebox's cube and banks made by `tests/net/bankbuild.h` (a
cube, a tree of three objects, two textures with three G_VTX batches each, a display list with 400 more texture
loads, ten levels of children, a mesh of 1500 faces). For each bank:

- the check accepts it, and the shadow walk ends without a fault
- the port's converter gives the same bytes as the shadow walk's converter
- the words that the game's loaders change are the words that the shadow walk changes, in both banks
- each changed word holds the shadow walk's value, with the shadow's base replaced by the bank's address

Then each level plays 600 frames with the pack's objects as scenery, twice, with the same memory hash. The output
is equal on Linux and under Wine.

A pack's mesh has no texture coordinates and no vertex colors, and the bank check cannot see what a level does to
its object. Two places in the game wrote through those pointers with no test. `DeformTextureCoords` (object.c)
moves the texture coordinates of an object that WATER or MODGE marks. The space boss's shield (`DoSpaceBoss3`,
boss.c) changes the colors of the first foreground object. Both now stop when the pointer is 0. `make meshguards`
(`tests/rom/meshguards.c`) runs both cases with the ROM, on Linux and under Wine; without the guards the game
crashes.

## Loading

- The game's switch of bank numbers moves into `TextureBankRange` and `ObjectBankRange`. `G64BankRom` gives the
  same ranges to the client.
- The pack's own banks load into free slots of the game's five, right after the level's own object bank
  (`G64LoadPackBanks` in `weemitt/src/game/g64packbanks.c`, called from `LoadLevel`). A level always leaves one
  object slot free. If either kind has no free slot, the level stops as unplayable before anything loads. The
  game's loaders take a slot only after the load, and their search for one has no bound. `make bankwalk` loads a
  pack a second time in a level whose slots it has filled, and the level stops with nothing more loaded.
- `G64SetPackModels` says where the two banks are in the ROM buffer, after the pack's other files. Its values are
  statics in the game's saved data, and hold no pointer. The client sets them with `g64_instances_pack_models`.
- The pack's banks are freed with the level's own, at the three places that free the five slots (`FreeLevel2`
  twice in game.c, and frontend.c). The end of level screen loads its banks only after `FreeLevel`.
- `RestoreDrawList` points the ball's and the hand's texture words at an object named `gball`, `cball`, `GShinL`,
  `GShinR` or `gbl_bt`. The check refuses those names in the pack's own bank, so its load needs no flag. Retail
  banks hold them, and step 9 needs one.
- Retail banks load into a table on the game's heap, not into slots. One free object slot cannot hold the pack's
  bank and a retail bank. Also, the game searches texture slots from the last one down, so a retail bank in a slot
  would win over the level's own. The table is searched after the slots, as in Glovebox's mod. This differs from
  the default chosen at the start.
- Everything loads onto the game's heap, inside the saved and hashed memory. Every machine has the same ROM and
  pack, so every machine makes the same allocations.

## Platforms

Custom models may be platforms. `make models` (`tests/rom/models.c`) decided it.

Why a test was needed:

- A rotation key is a quaternion. `QuaternionToMatrix` (maths.c) divides 2 by its squared length, with no test
  for 0. A key of all zeros gives infinity, and 0 times infinity gives NaN (not a number). The whole rotation part
  of the matrix is then NaN.
- `guMtxF2L` (weemitt's gu.c) turns each entry times 65536 into a 32-bit integer. For NaN, C leaves that undefined.
  Both 64-bit builds give 0x80000000 today, so a NaN entry becomes -32768.
- Glovebox writes a key of all zeros today. The bank check refuses it, and the pack export writes the identity key
  in its place. The test also measures the zero key.

Which actors read the key:

- `DrawObject` (object.c) reads an object's first rotation key unless the actor has a static matrix and `calcMtx`
  is 0. Nothing in the game's code assigns `calcMtx`, and no memset, memclear or memcpy names it or the globals
  before it. The test reads it on every frame, and it stays 0.
- Only a landscape actor gets a static matrix (object.c). It is made from the actor's rotation in the level file.
  LANDSCAPE and BACKGROUND both make a landscape actor, so they never read the key.
- Platforms read it: PLATFORM, ROPEBRIDGE, SINEBRIDGE and ORBITBRIDGE make them (parse.c). So do a WATER actor and
  a broken platform's debris, which are drawn with `DrawActor`. A CAMEO_OBJ makes a cameo actor, which reads it too.
  Step 7 is to add a level rule that refuses a pack object there.
- The glove reads the keys of what it rides or swings on, and sets its own rotation from them
  (`SetHandHopPosition` in hand.c, `HandSwish` in object.c). That is an enemy, a bullet or a cameo actor. An enemy's
  and a bullet's objects have the ids of the game's own objects, and the ROM check refuses a pack id that the ROM
  has. A cameo is the case of step 7's rule above.
- Platform collision uses the actor's rotation, not the key (collision.c, platform.c). Apart from the glove's case
  above, a key reaches only the drawn matrices. No game logic reads those, so they cannot change the game's state.
- The first 256 object matrices of a frame are in Dynamic, in the game's memory, and the memory hash covers them.
  The rest are in `objectMtxMore`, in `port_win`. That is past the 0x1800000 bytes the memory hash covers. So the
  test also prints a hash of `objectMtxMore`.

The test builds a pack from scratch with `tests/net/bankbuild.h`: an 8 by 8 texture, a cube of 12 faces and a
flat plate of 8 faces. The test replaces the plate's collision mesh with a square, 40 units on a side. It puts the
objects in level 13, near the glove's place in the level's first frame. Each edited level file passes
`g64_lev_check`. The cases:

- two PLATFORMs, each with a SPIN, a path of two points, a SPEED and an ACCEL. The test checks that both move and
  turn. The plate starts under the glove.
- a ROPEBRIDGE of 6 planks, a SINEBRIDGE of 5 and an ORBITBRIDGE of 4. The first plank of the rope and sine
  bridges starts under the glove.
- a destructible PLATFORM with 12 pieces of debris. The test breaks it at frame 995 by setting its flag in the
  game's memory. The debris then lives through the save and load at frame 1000.
- a WATER actor
- a LANDSCAPE and a BACKGROUND piece, as controls

Each case runs 3000 frames, with 2 and with 16 players who press buttons at random, with the seeds of bots.c. Every
100 frames it prints the memory hash, the hash of `objectMtxMore`, the count of NaN words in the game's memory,
and the count of matrix words that a NaN gives. At frame 1000 it saves, runs 100 frames, loads and runs them
again. It also checks that the pack's actors are in the level, that the level never stops as unplayable, and that
`calcMtx` stays 0. In the PLATFORM, ROPEBRIDGE and SINEBRIDGE cases, a glove or a ball must stand on or touch one
of the pack's platforms in some frame. The test reads each glove's and ball's platform and contact platform on
every frame. The ORBITBRIDGE case has no such check. In the test no glove or ball touched one of its planks, even
one that starts under the glove. The likely reason is in parse.c: it calls no `InitPlatformInfo` for these planks.
So their collision radius stays 0, and their faces never get the normals that `RecalculatePlatformInfo` keeps for
a platform with no motion and no rotation. There are two variants of the pack:

- Variant I has the identity key. The bank check accepts it.
- Variant Z has keys of all zeros. The bank check refuses it. The test loads it anyway, by setting the game's pack
  globals directly. This is a measurement only. Its results do not fail `make models`.

The rule, which is stricter than the owner's: platforms are allowed if variant I gives equal output on Linux and
under Wine in every case, and every save and load gives equal hashes. The result on one machine, under Wine 64-bit:

| Variant | Linux and Wine | Save and load | NaN in the drawn matrices |
| --- | --- | --- | --- |
| I | equal | equal in all 16 runs | none, in every case |
| Z | equal | equal in all 16 runs | in every case but the two controls |

- Variant Z draws a NaN in each platform, bridge, water and debris case. This shows that each of these cases drew
  its actors.
- It draws none in LANDSCAPE or BACKGROUND. The test does not show that it drew those actors, so this only agrees
  with the code. The evidence that they do not read the key is the static matrix path in `DrawActor`.
- In variant Z the debris drew NaN matrices from frame 997 to frame 1014 (2 players) and 1013 (16 players), after
  the platform was gone. Debris reads the key.
- The test counts a matrix as NaN when the integer halves of its rotation part are 0x80008000. A word of
  0x80008000 elsewhere comes from two entries out of the range of the matrix's numbers, such as a far
  translation. One such word appeared in 2 frames of SINEBRIDGE with 16 players, in both variants.
- Both variants give the same count of frames with a glove or a ball on the pack's platforms, in every case.
- The NaN floats themselves do not stay in memory. Every sample finds 13,018 to 15,359 NaN words in the game's
  memory. At most 6 of them have the bits that 0 times infinity gives. Both variants give the same count of those, frame
  by frame, so they come from the game's own data. What stays is the converted matrix, with
  words of 0x80008000.
- Variant Z's equal output shows only that both builds' compilers emit the same truncating conversion today. C
  leaves the result undefined, so another compiler or other flags could change it. The bank check keeps refusing
  the zero key.

The test found a crash in the game's own code, and g64coopdx now fixes it:

- `CollideActorWithPlane` (collision.c) handles a glove or a ball on a turning platform with motion, such as a rope
  bridge's plank. It looks at each plane that the actor touches, in `planes[4]`, until it finds an empty one.
- When the actor touches 4 planes, all 4 are full. The loop then reads the next word, `contactPos[0].x`, and uses a
  float's bits as a pointer. The game crashed.
- The test's plank mesh has coincident faces, so a glove on it touches 4 at once. Any level pack can do the same
  with a rope bridge of a dense mesh, with or without custom models.
- The fix stops the loop after 4 planes. With 4 planes, the old loop stopped only when that x was 0. Otherwise it
  crashed, or, for a value near 1e-31, read garbage as a plane. The fix changes only those runs. `make regress` says 0 of 49 runs differ. The ROPEBRIDGE case is the fix's test.

## The heap

A full heap stops the game. So the limits on banks, retail banks and collision come from a measurement: the free
heap and the free blocks at the point where the banks load, in the fullest level, with 16 players. `make bankwalk`
measures it in every level, and loads a pack at the limit.

The heap has 22 MiB. Where the banks load, with 16 players:

| What | Least | Level |
| --- | --- | --- |
| Free bytes | 21,204,000 | 46, the intro |
| Free bytes in a played level | 21,334,272 | 7 |
| Free blocks | 1,245 | 35 |

The free bytes all lie between the static and the dynamic blocks there. While a level starts and runs 600 frames
with 16 players, every level keeps at least 20,895,424 bytes and 230 blocks free (both in level 15).

So 4 MiB of banks always fit, and `G64LoadPackBanks` has no test of the heap. The level parser already stops a
level that leaves less than 4 MiB free (`G64_PARSE_FREE`, parse.c). The test's largest pack has a texture bank of
4,144 bytes and an object bank of 4,164,488 bytes: 60 objects with meshes of 1500 faces and 4500 vertices. With all
60 as scenery and 16 players:

- level 7 plays 600 frames, with at least 5,949,936 bytes and 757 blocks free
- level 35 plays 600 frames, with at least 5,953,152 bytes and 388 blocks free
- level 46 stops as unplayable: the intro reads its first actor's animation, and the pack's scenery has none

Collision costs more than the banks. After the parser, every scenery, landscape or water actor gets one more block
and 120 bytes for each face of its mesh (parse.c, "Obj_Planes"). That is 180,000 bytes for a mesh of 1500 faces.
A landscape actor with collision also compares every face of its mesh with every later face. The parser's test
before each token cannot see these. So `G64PlanesFit` (parse.c) adds them up before any plane is made. The level
stops as unplayable when they would leave less than 4 MiB free, would bring the blocks in use to 1,100, or when the
sum of the squares of the landscape face counts is above 40,000,000. With 16 players, the game's own levels need
at most 101 blocks (level 21), 288,048 bytes (level 29) and a sum of 340,884 (level 43). `make bankwalk` puts one
object in level 13 many times, with 16 players, and finds the largest count that starts:

| Object | Most copies | What stops one more | Least free in 600 frames |
| --- | --- | --- | --- |
| A pack's mesh of 1 face, as scenery | 471 | the blocks | 170 blocks |
| A pack's mesh of 1 face, as landscape | 471 | the blocks | 170 blocks |
| Level 13's own landscape object, as scenery, with no pack | 472 | the blocks | 170 blocks |
| A pack's mesh of 1500 faces, as scenery | 94 | the bytes | 4,230,176 bytes |
| A pack's flat mesh of 1500 faces, as landscape | 17 | the work | 18,106,400 bytes |

Each largest count plays 600 frames the same in two runs. One more copy stops the level, and the game runs 60 more
frames. The same cases in levels 15, 20 and 35 also play at the limit. Level 15 uses the most blocks in play, and
keeps at least 166 free there. The 17 flat meshes take about half a second to start (0.45 to 0.52 s over
four runs, on Linux and under Wine, on one machine). The test prints the time to stderr.
Before this change, 700 small copies or 130 copies of 1500 faces made the game stop the program. Step 7 adds the
budget for retail banks and for the level file's object ids.

## Steps

| Step | What | Done |
| --- | --- | --- |
| 1 | This page, and the bank switch split out of the loaders with no change to the game | yes |
| 2 | A bounded reader of compressed banks, and an index of the ROM's banks | yes |
| 3 | The bank check, its tests and its fuzz target | yes |
| 4 | The pack's own banks in free slots, and the proof that the check matches the loaders | yes |
| 5 | The test of platforms with custom models, on Linux and under Wine | yes |
| 6 | `models.bin` in the pack format, the pack tool and the cache | yes |
| 7 | The level file's object ids against the banks, and the heap budget | |
| 8 | The offer and the transfer (protocol 12) | |
| 9 | Retail banks in the heap table | |
| 10 | Glovebox's pack export with models | |
| 11 | A model in the sample pack | |
