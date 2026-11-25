# Models in level packs

This page is the plan for custom models in a level pack ([level-pack.md](level-pack.md)), and the record of what
was built and tested. A model is an object the game draws, such as a platform or a piece of scenery. The game reads
models from banks. A texture bank holds pictures, and an object bank holds objects that use those pictures. The
game's own banks are blocks of the ROM. A pack may bring one bank of each kind of its own. The work is in steps. Each
step is one pull request. The table at the end says which steps are done. README.md's [terms](README.md#terms)
define the words that this page uses without explaining them, such as the owner, the host and an instance.

## What a pack may hold

The owner decided these points.

- A pack may carry the author's own models: models imported from glTF files and Glovebox's kit models (models that
  Glovebox makes by code). They come in one object bank and one texture bank of the pack's own. Both are stored
  uncompressed.
- A pack may not carry a reskin or a copy of a retail model. Both hold the game's own data.
- A pack may also name retail banks by number. A retail bank is one of the ROM's own banks. Each player's game
  loads it from their own ROM. Only the number crosses the network. The session already checks that every player
  has the same ROM.
- The models in a pack are at most 4 MiB.
- Custom models may be platforms, if a test shows that they give the same game on Linux and Windows. The test
  passed ("Platforms" below).

## The file

A pack may have one more file, `models.bin`. The pack tool (`g64pack`, [level-pack.md](level-pack.md)) finds it by
its presence, as it finds `script.lua`, the pack's script. The manifest, `pack.txt`, says `format=1` with or without
`models.bin`.

All words are big-endian, as in the game's banks.

| Offset | Size | What |
| --- | --- | --- |
| 0 | 4 | T, the size of the pack's texture bank, or 0 |
| 4 | 4 | O, the size of the pack's object bank, or 0 |
| 8 | 1 | Flags. Bit 0, the first flag: search the retail texture banks before the level's own. The other bits are 0. |
| 9 | 1 | a, the number of retail texture banks, 0 to 4 |
| 10 | 1 | b, the number of retail object banks, 0 to 4 |
| 11 | 1 | 0 |
| 12 | a + b | The retail texture bank numbers, then the retail object bank numbers, one byte each |
| H | T | The pack's texture bank. H is 12 + a + b, rounded up to a multiple of 8. The padding bytes between the bank numbers and H are 0. |
| H + T | O | The pack's object bank |

- T and O are multiples of 8. They are both 0 or both more than 0.
- T + O is at most 4 MiB.
- The flag is 0 when the file names no retail texture bank.
- Each retail number appears once. The font's texture bank (10), the GENERIC texture bank (2) and object banks 0 and
  1 are refused.
- The file is exactly H + T + O bytes long.
- The pack's id ([level-pack.md](level-pack.md), "The pack's id") hashes `models.bin` after `script.lua`, in the
  same form as the other files: the file's name, a zero byte, its length and its bytes. A pack without `models.bin`
  has the same id as before the file was part of the format.

## The check

A bank from a pack is untrusted. The game trusts every field of a bank: its loaders follow its offsets and counts with
no bounds. So the client (g64coopdx's own code in `src/client/`) checks each bank in full before the game sees it, as it
checks a level file ([level-file-safety.md](level-file-safety.md)). The check is `src/client/bankcheck.c`. It reads the
bytes as the game's loaders do, and it refuses anything it does not understand.

Some rules allow only what Glovebox writes today. `tests/net/bankcheck_test.c` holds each of them to Glovebox's real
output: a cube that Glovebox wrote, `tests/fixtures/cube.models.bin` (Glovebox's cube below). The cube passes, and one
change to it for each rule fails with that rule's message. A new kind of model in Glovebox widens the rule and its
test together.

No rule counts faces or display list commands across the bank: the 4 MiB limit bounds them. The renderer stops a
display list after 100000 commands (`gfx_dl.c`); that changes only the picture.

In short, the check refuses these:

- A bank that starts with `FLA2`. The game would decompress it, and its decompressor has no bounds.
- Any byte that no structure reaches and that is not 0.
- A texture record whose size, format, dimensions or palette differ from what Glovebox writes. A texture that no
  object uses.
- An animated texture. The game keeps at most 20 of them, with no bound check.
- A pointer that is not aligned, that has high bits set, or that names a range outside the bank.
- Two structures that share bytes. A pointer in a bank is an offset in the bank. The loaders turn each pointer into
  an address each time they reach it, so a pointer that two structures share is changed twice and breaks.
- An object tree deeper than 32, or with more than 4096 objects. Each child and each sibling is one step deeper:
  `RestoreObjectPointers` and `TransformAndDrawObject` call themselves for each.
- An object tree with more than 10 levels of children. Each level keeps one matrix on the game's matrix stack,
  which has 20 entries, and one entry of `parentScaleStack`, which has 10.
- A face index outside its mesh's vertices.
- A display list command that Glovebox does not write, or a texture load in another form than Glovebox's. A
  texture load must hold the exact words Glovebox writes for the size of its texture.
- An object whose id is one that the game asks for by a name in its own code, for an enemy, a bullet or a pick-up, when
  the retail ROM has no object with that id. The game turns such a name into an id with `UpdateCRC`. Nine enemy kinds
  (kinds 0 to 6 share one name, and kinds 46 and 57 have one each) and three pick-ups have such names
  (`g64_models_game_ids` in bankcheck.c). Such an object would be that enemy or pick-up, which reads an animation, and a
  pack's model has none. `make levcheck` checks the list against the game's names and the ROM. The names it checks are
  those of `G64GameObjectIds` (g64packbanks.c): the enemies', the pick-ups' and garibs', the standard bullets' without
  sprites, and the fixed names in the game's code. The ROM has an object for every other such name, so the rule against
  a ROM id (below) refuses those ids.
- An object with the name of the ball or the hand. The game points the words for the ball's and the hand's textures at
  the objects with these names. Glovebox names an object after its glTF file. Its pack export refuses a model with one
  of these five names (`gball`, `cball`, `GShinL`, `GShinR`, `gbl_bt`).
- A float that is not finite, a position past 16384 or a scale past 64.
- An object whose position, added up along its parents, is past 16384. The game translates a child by its position
  times its parent's scale, and keeps the parent's translation while the child draws. The sum counts each
  position's length times the largest part of the parent's scale. A rotation does not change a length. The bound
  holds for an actor at the origin with scale 1: `guMtxF2L` turns each matrix entry times 65536 into a 32-bit
  integer, which overflows past 32768. The actor's own position and scale come from the level file, and add to this
  sum.
- A rotation key that is not near unit length: its squared length is not 0.5 to 2. A rotation key is a quaternion
  that turns an object. Glovebox's model converter writes a key of all zeros today. The game turns it into a matrix of
  NaN values (not-a-number). Glovebox's pack export writes the identity key, (0, 0, 0, 1), in its place.

With the player's ROM, the client also refuses:

- An object or texture of the pack whose id the ROM already has.
- A texture, a mesh's vertex list or a G_VTX block that is equal to one in the ROM. A vertex list or a block of
  less than 64 bytes is allowed, and so is a texture of one color.
- A level file that names an object found in no bank the level will have, or a cameo of the pack's own model
  ("The level's object ids").

The client does not limit the memory for a level's collision. The game's parser does: a level whose collision
would need more memory than the budget allows stops as unplayable (`G64PlanesFit` in parse.c, "The heap" below).

The check's API is `g64_models_check` and `g64_models_check_rom` (`src/client/bankcheck.h`). Each returns a message and
the offset in `models.bin` of the fault. The caller gives the first one a function that says which bank numbers the game
has, and each bank's range in the ROM. A program that links the game, such as the online program, asks `G64BankRom`
(`g64_rombanks_game_range`). The pack tool, the pack tests in `tests/net` and the pack fuzz target do not link
the game. They use a table of the game's bank switch (`src/client/bankranges.h`, written by `tools/gen_bankranges.py`).
`make rombanks` compares the table with `G64BankRom`. `g64_models_check_rom` applies the first two rules of the list
above that need the ROM: the ROM's ids, and data equal to the ROM's. The rule for the level file is a separate one
("The level's object ids").

The pack reader (`src/client/pack.h`) runs the check on every `models.bin`, from a folder too. The online program
runs the rules that need the ROM: the rules of `models.bin` and the rule for the level file's object ids ("The
level's object ids"). It runs them three times:

- on the host, before it offers the pack (`host_offer_pack` in `src/client/netplay.c`). It runs the rule for the
  level file's ids first (`g64_pack_check_ids_rom`), and a pack that fails it stops the session. It then runs
  `g64_pack_may_send`. A pack that fails that is not offered, and the log says why. Joiners then need the pack's
  folder.
- on a joiner, on the downloaded bytes, before it says ready (`g64_pack_from_download`, called by `joiner_pack`). A
  pack that fails ends the session. The joiner then runs the rule for the level file's ids on its own ROM
  (`g64_pack_check_ids_rom`), and a pack that fails it ends the session too.
- on every machine in `netplay_begin`, before the game is built.

The first two run before the game has read the banks' places (their ranges in the ROM) from the ROM's code, which it
reads at power-on. From the command line the game is not even powered on then. So they take the places from the table of
`bankranges.h`, which reads the same code. `netplay_begin` takes them from the game. `make rombanks` shows that the two
agree. `g64pack check DIR --rom ROM` runs the same rules with the table. They use an index of the ROM's banks
(`src/client/rombanks.h`): every object id and texture id of the player's ROM, and hashes of its textures, vertex lists
and G_VTX blocks. All of them refuse an index that has no bank of one kind, or that skipped a bank
(`g64_rombanks_complete`). The index takes about 40 ms to build on one machine.

A fuzz target (`tests/fuzz/bankcheck_fuzz.c`) runs the check on random banks, on changes to Glovebox's cube, and
on banks made from random shapes. For every bank the check accepts, it runs a copy of the game's loaders that stops
at any read or write outside the bank (`tests/fuzz/bankwalk.c`). This copy is the shadow walk.

`make bankwalk` (`tests/rom/bankwalk.c`) proves that this copy does what the game does. The game loads the test's
banks through `G64LoadPackBanks` in a real level, level 13: Glovebox's cube and banks made by `tests/net/bankbuild.h` (a
cube, a tree of three objects, two textures with three G_VTX batches each, a display list with 400 more texture
loads, ten levels of children, a mesh of 1500 faces). For each bank:

- the check accepts it, and the shadow walk ends without a fault
- the port's converter (`port_convert_texture_bank` and `port_convert_object_bank`, which put a bank's big-endian
  words in the PC's byte order) gives the same bytes as the shadow walk's converter
- the words that the game's loaders change are the words that the shadow walk changes, in the texture bank and in the
  object bank
- each changed word holds the shadow walk's value, with the shadow's base replaced by the bank's address

Then, for each bank, level 13 plays 600 frames with the pack's objects as scenery, twice, with the same memory hash. The
output is equal on Linux and under Wine.

A pack's mesh has no texture coordinates and no vertex colors: Glovebox writes neither, and the bank check refuses them.
The bank check cannot see what a level does to its object. Two places in the game wrote through those pointers with no
test. `DeformTextureCoords` (object.c) moves the texture coordinates of an object that a WATER or MODGE token of the
level file marks. The space boss's shield (`DoSpaceBoss3`, boss.c) changes the colors of the first foreground object. In
g64coopdx's copy, both stop when the pointer is 0. `make meshguards` (`tests/rom/meshguards.c`) runs both cases with the
ROM, on Linux and under Wine; without the guards the game crashes.

## The network

A pack with `models.bin` travels as the other files do ([level-pack.md](level-pack.md), "Packs that came from a
host"). Protocol 12 adds one field to the offer, the message (type 0x49) that the host sends to a joiner who lacks
its pack (`src/session/session.h`):

| Offset | Size | What |
| --- | --- | --- |
| 0 | 1 | 0x49 |
| 1 | 16 | The pack's id |
| 17 | 2 | The size of `pack.txt` |
| 19 | 4 | The size of `level.lev` |
| 23 | 4 | The size of `script.lua`, or 0 |
| 27 | 4 | The size of `models.bin`, or 0 |
| 31 | 1 to 32 | The pack's name |

- The size of `models.bin` is 0, or 16 to 4 MiB and 24 bytes (`G64_PACK_MODELS_MIN`, `G64_PACK_MODELS_LIMIT`). 24
  bytes is the largest header: 12 bytes and 8 bank numbers, rounded up to a multiple of 8. 16 bytes is a header alone:
  a file that names 1 to 4 retail banks and has no banks of its own. A joiner drops an offer outside these bounds.
- A second offer with another size of `models.bin` ends the session, as for the other sizes.
- The bytes are the four files one after another, `models.bin` last. The joiner holds them in one block of at most
  about 5.3 MB. The largest `pack.txt`, `level.lev`, `script.lua` and `models.bin` are 1 KiB, 1 MiB, 256 KiB, and 4
  MiB and 24 bytes (`src/session/session.h`).
- A joiner asks for 8 pieces of 1,024 bytes at a time, and asks again when they have come. So a download moves about
  8 KB for each round trip between the joiner and the host. The host also sends at most 256 pieces a second, to all
  players together.
- A download of 1 MB takes about 5 seconds on a round trip of 10 ms, about 13 seconds at 100 ms and about 64 seconds
  at 500 ms. A pack of the largest size, about 5.3 MB, takes about 25 seconds, 67 seconds and 5.6 minutes. These
  times come from the real session on a simulated clock.
- The host holds a joiner's place in the session for five minutes while the joiner downloads
  ([level-pack.md](level-pack.md), "Packs that came from a host"). For a pack larger than about 1.9 MB it holds the
  place for one minute more than the download takes on a round trip of one second: about 12.5 minutes for a pack of the
  largest size. `session_test` (tests/net/session_test.c) downloads such a pack with the host answering once a second,
  and it comes whole.
- `packsend_test` (tests/net/packsend_test.c) sends a pack with the cube, a level and a script of 20,000 bytes from a
  host to a joiner through the session. The host checks it with `g64_pack_may_send` and the joiner reads it with
  `g64_pack_from_download`, each on a made-up ROM. These are refused: a ROM with the cube's ids, a damaged `models.bin`,
  and bytes or a name that are not the offer's. The host's gate also runs the rule for the level file's object ids after
  `g64_pack_may_send`: a level with a platform whose id is in no bank passes `g64_pack_may_send` and is refused.
- Only the pack's own banks and the retail bank numbers cross the network. The banks pass the rules that need the
  ROM on both ends: they hold none of the ROM's ids or data. Protocol 12 leaves the instances' messages
  (`src/client/imsg.h`) as they were.

## The level's object ids

The game's level parser asks `FindObject` for an object by an id that the level file holds. An id that no loaded
bank has makes the level unplayable. `g64_pack_check_ids` (src/client/pack.c) finds that on the host, before the
game starts. A pack's maker finds it with `g64pack check DIR --rom ROM`.

- `g64_lev_object_ids` walks the file with the level check's token callback and reads each id after the check has
  passed. The callback comes before the check of the token's length, so it only notes where the token starts.
- Nine tokens carry an id, at these offsets of the token's fields: PLATFORM, BACKGROUND, LANDSCAPE and CAMEO_OBJ at 0,
  SINEBRIDGE at 4, DESTRUCTIBLE (its debris) and ROPEBRIDGE at 6, ORBITBRIDGE at 8, WATER at 38. A DESTRUCTIBLE with
  id 0 has no debris, as in the game's own levels.
- Every id must be in a bank the level will have: one of the level's own object banks, the pack's own object bank,
  or a retail object bank that models.bin names.
- The level's own banks are the ones `LoadLevel` loads before the pack's: bank 0, the world's bank for a level whose
  number is below the intro's (46), bank 1 (69 in the fly-through, level 44) and the level's bank. `G64LevelBankNumbers`
  (g64packbanks.c) reads them from the game's tables. The online program asks it right after power-on. The pack tool has
  no game, so it uses a table of the same numbers (`g64_level_object_banks` in src/client/bankranges.c).
- The index of the ROM's banks says which bank has an id (`g64_rombanks_bank_has_object`). Two bank numbers that
  name one range both get its ids.
- A CAMEO_OBJ may not name a model of the pack's own bank. A cameo actor is animated at once, and a pack's model has
  no animation. An object of a retail bank may be a cameo.
- An ENEMY names a kind of enemy, not an object, and a bullet's object comes from the enemy or the vent that makes it.
  Their objects, and the pick-ups' and garibs', have the game's own names. A pack's model cannot have one of their ids
  (the bank check's rules above). So no enemy, bullet or pick-up can use a pack's model.
- The online host runs the rule before it offers its pack (`host_offer_pack` in src/client/netplay.c). The game has not
  read its tables yet, so the host uses the committed tables, as the pack tool does (`g64_pack_check_ids_rom` in
  src/client/packrom.c, which runs `g64_pack_check_ids` with them). It runs before `g64_pack_may_send`, on a pack whose
  level file is not a copy of one of the ROM's and passes the structure check (`g64_pack_check_strict`, the check of
  [level-file-safety.md](level-file-safety.md)). A pack that fails stops the session at once, before any joiner
  downloads it. This holds even when its models would fail too.
- A joiner runs the same rule on its own ROM after a download, before it keeps the pack and says ready. A pack that
  fails ends the session.
- The online program runs the rule again in `netplay_begin`, with the game's own tables and with the rules of
  models.bin that need the ROM, also for a pack without models.bin. It skips a pack from a folder whose level file
  fails the structure check. Such a pack is never sent, and the game still marks its level as unplayable when an
  object is missing.

`make levcheck` (tests/rom/levcheck.c) proves the rule against the game:

- The game's `FindObject` records each id it is asked for while the parser reads a token (`G64FindObjectTrace`, for
  tests). On all 48 levels, the ids of the nine tokens are the ids the check reads, at the same tokens. Every other
  token that asks for an object asks for an id that is not a word of its fields.
- No level of the ROM has a bridge. The test adds a ROPEBRIDGE, a SINEBRIDGE and an ORBITBRIDGE to a copy of level 13,
  with an object of the level's banks, and compares again. It adds a LANDSCAPE with an id that no bank has: the rule
  refuses that file, the rule with the committed tables refuses it at the same token, and the game stops the level.
- After each level starts, the game's five object slots (the places that hold the object banks it has loaded) hold the
  banks that `G64LevelBankNumbers` names, slot by slot, and nothing more. Right after power-on and after each start, its
  numbers equal the table's.
- Each level's own file passes the rule with its own banks, from the game's tables and from the committed tables.

## Loading

- The game's switch on bank numbers gives each bank's range in the ROM. It is split out of the loaders into
  `TextureBankRange` and `ObjectBankRange`. `G64BankRom` gives the same ranges to the client.
- The pack's own banks load into free slots of the game's five, right after the level's own object bank
  (`G64LoadPackBanks` in `weemitt/src/game/g64packbanks.c`, called from `LoadLevel`). A level always leaves one
  object slot free. If either kind has no free slot, the level stops as unplayable before anything loads. The
  game's loaders take a slot only after the load, and their search for one has no bound. `make bankwalk` loads a
  pack a second time in a level whose slots it has filled, and the level stops with nothing more loaded.
- The client copies the pack's files after the ROM's own bytes, in the memory that holds the ROM (the ROM buffer).
  `G64SetPackModels` says where the two banks are in the ROM buffer, after the pack's other files. Its values are
  statics in the game's saved data, and hold no pointer. The client sets them with `g64_instances_pack_models`.
- The pack's banks are freed with the level's own, at the three places that free the five slots (`FreeScreen` and
  `FreeLevel2` in game.c, and frontend.c). The end of level screen loads its banks only after `FreeLevel`, which
  calls `FreeLevel2`.
- `RestoreDrawList` points the game's words for the ball's and the hand's textures at an object named `gball`, `cball`,
  `GShinL`, `GShinR` or `gbl_bt`. The check refuses those names in the pack's own bank. Retail banks hold them: OUTRO
  has `GShinL` and `GShinR`, and FLYTHRU has `gball`. So `RestoreDrawList` skips those names while `G64LoadPackBanks`
  runs (`G64PackBankLoading`). The flag is set and cleared within that one call, so no saved state holds it set.
- Retail banks load into a table on the game's heap, not into slots ("Retail banks" below). This differs from the
  default chosen at the start.
- Everything loads onto the game's heap, inside the saved and hashed memory. Every machine has the same ROM and
  pack, so every machine makes the same allocations.

### Retail banks

`models.bin` may name up to 4 retail texture banks and 4 retail object banks by number. Each player's game loads them
from the player's own ROM. Only the numbers cross the network, and the session's hello (the joiner's first message,
which carries the ROM's hash) has already shown that every player has the same ROM.

Why a table and not the slots:

- A level always leaves one object slot free, and the pack's own bank may need it.
- The game searches the texture slots from the last one down. A retail bank in a slot would win over the level's own
  banks when an id is in both. Glovebox's mod is a small game mod that Glovebox can add to the ROMs it builds, and it
  loads extra banks for a level. It searches the slots first, and the author tests the level with the mod.

How they load (`G64LoadPackBanks` in `weemitt/src/game/g64packbanks.c`):

- The order is: each retail texture bank, the pack's texture bank, each retail object bank, the pack's object bank.
  An object bank's load looks up its textures, so the texture banks come first.
- The table is one block on the game's heap, `G64Banks`. It holds up to 4 texture bank records, up to 4 object bank
  records, their counts and the header's first flag.
- Each retail bank loads with the swap of Glovebox's mod: the five slots of its kind are put aside and emptied, the
  game's own loader puts the bank in slot 0, slot 0 is copied into the table, and the five slots come back.
- For a texture bank, `cyclingTextures` and its count are put back too. A retail texture in the table does not
  animate, as in the mod. The game's list of 20 animated textures cannot overflow.
- During an object bank's load the texture slots stay as they are. Its objects find the level's textures and the
  table's.
- The game skips a number that its bank switch lacks, the font and GENERIC texture banks, and object banks 0 and 1. The
  client refuses the same numbers. A bad number never reaches the loaders, which stop the game on a number their switch
  lacks (`LoadTextureBank` and `LoadObjectBank` in banks.c).
- The client passes the numbers and the flag to the game with `g64_instances_pack_retail` (`src/client/instances.h`),
  from the pack's checked header. The game keeps them with `G64SetPackRetail`.

How the game finds them:

- `FindTexture` (banks.c): with the first flag, the table first. Then slots 4 down to 0, as in the original game.
  Then the table, without the flag.
- `FindObject` (banks.c): slots 0 to 4, as in the original game, then the table.
- Without the first flag, a level's own picture therefore wins when a retail model's texture id is also in the level's
  world. That is what the author sees in the emulator with Glovebox's mod.

When the table is gone:

- Two statics hold the table's address as the game stores it (a 32-bit word: on the 64-bit builds, an offset into
  `port_ram`), and the value of `portHeapInits` when the table was made (the stamp). `JallocInit` raises
  `portHeapInits` each time it sets up the heap, so a lookup never reads a table on a heap that was set up again.
- The three places that free the five slots also forget the table (`G64DropPackBanks`): `FreeScreen` and `FreeLevel2` in
  game.c, and frontend.c. The design said the stamp alone needs no edit there. But `LoadFont` looks up textures
  after the frees and before `JallocInit`, while the stamp is still current.
- The statics are in the game's saved sections (`gstate_bss`). objdump shows this on the Linux online program and on the
  brute forcer's engine (weemitt/bruteforce/engine), which the ROM tests link, for Linux and for Windows. A saved state
  holds them with the heap they describe.

An object bank may be named without the texture banks it needs. A texture that no bank has then stays missing:

- A display list's texture word keeps the texture's id. The renderer reads a texture only inside the game's memory
  (`gfx_mem` in gfx_mem.c), so a wrong word changes the picture only. This was read in the code, and not tested: the ROM
  tests have no renderer.
- A face's texture is NULL. `LoadTexture` returns at once for NULL.
- A sprite's texture is NULL. The game reads it with no test (`AddObjectsSpritesToSpriteList`, actor.c), and the program
  crashes. `make packbanks` found this with bank 19. So `G64LoadPackBanks` walks each retail object bank after its load,
  and a sprite with no texture stops the level as unplayable ("RETAIL TEXTURES", the reason that the error screen
  shows).
- The game checks every object of the bank, also the ones the level never places. The game can find an object of a
  retail bank by a name of its own (an enemy's or a pick-up's), so a placed object is not the only way in.

The client refuses the same packs before the game sees them (`g64_pack_check_retail_sprites`, `src/client/packrom.h`):

- The index of the ROM's banks (`rombanks.h`) also keeps each texture bank's ids, and the texture ids that each object
  bank's sprites name. The walk reads the sprites as `RestoreObjectPointers` does. Their bytes count against the bank's
  budget: the bytes that the index hashes or reads for one bank are at most four times the bank's size and 1 MiB more. A
  bank that needs more is skipped.
- Every sprite texture of each retail object bank that `models.bin` names must be in one of these texture banks: the
  font's (10), GENERIC (2), the level's own, and the retail texture banks that `models.bin` names. The level's own
  come from a table in `bankranges.c` (`g64_level_texture_banks`), which `make packbanks` compares with the game's.
- The rule runs in `g64_pack_check_ids_rom`, after the rule for the level's ids. The host runs it before its offer, a
  joiner on the download, and the pack tool on a folder. `check_pack_ids_and_models` in netplay.c runs it again with the
  game's own bank ranges. A refusal sets the rule to 2 (`*rule` in `packrom.h`; 1 is a refused id of the level file) and
  gives the object bank's number.
- Glovebox's pack export keeps this rule (Glovebox pull request 3, `pack::add_sprite_banks`). It adds texture banks
  until every sprite texture of each object bank it names is in one: first the texture banks of the levels that load
  the object bank, then any other. It refuses a pack that would need more than 4, or a sprite texture in no bank. A
  pack for Atlantis 1 that names Carnival's object bank 19 names Carnival's texture bank 4 too.

`make packbanks` (`tests/rom/packbanks.c`) tests the table with the ROM. Its output is equal on Linux and under Wine.

- It measures each allowed bank's cost in level 13: 17 texture banks and 54 object banks. The table takes 1 block (one
  entry of the heap's list of 1,280 memory blocks). A texture bank takes 1 block and at most 689,824 bytes. An object
  bank takes at most 22 blocks (bank 29, one for each mesh with texture coordinates) and 288,096 bytes (bank 65).
- With its world's texture banks, every object bank finds all its textures but FLYTHRU (69), which misses 2 in
  level 13. Neither is a sprite's.
- The 54 object banks make 14 groups of at most 4, each with its world's texture banks. Every group plays in every
  level, with 16 players who press buttons at random, for 600 frames. That is 672 runs. Every bank loads, a level
  stops only where it stops without the pack's banks too, and the program never stops. Every level keeps at least
  200 blocks (level 15) and 19,675,600 bytes free. The whole test takes about 10 minutes on Linux and under Wine
  together.
- The four costliest object banks by blocks, the four by bytes, and the four largest banks of each kind play in
  levels 15, 35, 7 and 46. The least free is 180 blocks (level 15) and 17,781,632 bytes.
- One level file names, as scenery, every object of a retail bank that level 13 lacks. It plays, and the table answers
  each lookup. Without the retail number the level stops as unplayable, and `g64_pack_check_ids` refuses the file.
  The same objects play with the first flag.
- Every object bank alone, without its texture banks, with all of its objects that level 13 lacks as scenery:
  the level plays, or stops as unplayable where a sprite's texture is missing (banks 19, 27, 38, 55, 62, 66 and 69).
  The program never stops, and two runs give the same hash.
- The lookup order: level 13's own world texture bank (3) as a retail bank, with an object bank of its world. Every
  id is then in a slot and in the table. The table answers no lookup without the first flag, and 76 with it.
- OUTRO and FLYTHRU in levels 13, 0 and 1 leave the ball's and the hand's texture words and their bits unchanged.
  The table holds `GShinL`, `GShinR` and `gball`.
- The client's rule agrees with the game in all 5,184 cases: every allowed object bank, alone and with its world's
  texture banks, in every level. The game stops 300 of the cases alone and none with their texture banks, and the
  client refuses exactly those. The table of the levels' texture banks equals `G64LevelTextureBanks`.
- A save at frame 300 and a load give the same memory hash at frame 600. So does a save, then another level with no
  retail banks or with other ones, then the load. After the load the table has the saved level's counts and ids. The
  hash alone would not show a lost table: with the table dropped right after the load, the hash stays the same, and
  only the table's counts differ. The build already refuses a static outside the saved sections (`checkgame` in the
  engine's Makefile).
- Level 1 ends offline, with the first flag, and the hub loads. It also ends as a completed level. Both runs free
  the slots in `FreeLevel2`; neither reaches `FreeScreen`. The table is gone, and no lookup is answered from it after the level's end. A test counter counts lookups that reach a
  live table while all five object slots are empty, outside `G64LoadPackBanks`: that happens only between the frees of
  the slots and `JallocInit`. It stays 0. Without the drop in `FreeLevel2` it is 2 (the lookups in `LoadFont`), and
  the test fails. Without the drop in `FreeScreen` or in frontend.c the test still passes: no test reaches those two
  places with a live table.
- `make instances` plays the sample pack (`packs/garib-island`, "The sample pack" below), whose `models.bin` names
  Carnival's texture bank (4) and object bank 20 for a bell. It goes through the pack reader and
  `g64_instances_pack_retail`, and the game loads both banks into its table. The bell is found in the table and in no
  slot. The client accepts that pack. It refuses the same pack with object banks 19 and 20 and no texture bank (rule 2,
  bank 19), and the same pack without object bank 20 (rule 1).

`make packbanks` also found faults of level files that have nothing to do with retail banks. A level file with about
104 of object bank 29's objects as scenery crashed the game, in level 35, whose own bank it is, and with no pack
banks: `CheckCollisions` and `UpdateWater` found a broken list of actors. 100 of them played. The meshes without a
display list had filled the frame's buffer of vertices, and the game had written on past it, over the lists of
actors. A sprite without its texture and a level that stopped before its collision planes crashed too. The game now
guards all three, and `make scenery` tests them (level-file-safety.md, "What the game itself guards"). `make
packbanks` now names every object of a bank that level 13 lacks, with no cap.

The caps stay at 4 retail texture banks and 4 retail object banks. The costs leave room for more: the table and 4 banks
of each kind take at most 51 blocks of the 1,245 free where the banks load. The level parser already stops a level that
would bring the blocks in use to 1,100 (`G64_PARSE_BLOCKS`), and the table's blocks count toward that limit. More banks
would change the header and the bank check, and can come in a later step.

## Platforms

Custom models may be platforms. `make models` (`tests/rom/models.c`) decided it.

Why a test was needed:

- A rotation key is a quaternion. `QuaternionToMatrix` (maths.c) divides 2 by its squared length, with no test
  for 0. A key of all zeros gives infinity, and 0 times infinity gives NaN (not a number). The whole rotation part
  of the matrix is then NaN.
- `guMtxF2L` (weemitt's gu.c) turns each entry times 65536 into a 32-bit integer. For NaN, C leaves that undefined.
  Both 64-bit builds give 0x80000000 today, so a NaN entry becomes -32768.
- Glovebox's model converter writes a key of all zeros today. The bank check refuses it, and the pack export writes the
  identity key in its place. The test also measures the zero key.

Which actors read the key:

- `DrawObject` (object.c) reads an object's first rotation key unless the actor has a static matrix and `calcMtx`
  is 0. Nothing in the game's code assigns `calcMtx`, and no memset, memclear or memcpy names it or the globals
  before it. The test reads it on every frame, and it stays 0.
- Only a landscape actor gets a static matrix (object.c). It is made from the actor's rotation in the level file.
  LANDSCAPE and BACKGROUND both make a landscape actor, so they never read the key.
- Platforms read it: PLATFORM, ROPEBRIDGE, SINEBRIDGE and ORBITBRIDGE make them (parse.c). So do a WATER actor and
  a broken platform's debris, which are drawn with `DrawActor`. A CAMEO_OBJ makes a cameo actor, which reads it too.
  The rule for the level's object ids refuses a pack's model there.
- The glove reads the keys of what it rides or swings on, and sets its own rotation from them
  (`SetHandHopPosition` in hand.c, `HandSwish` in object.c). That is an enemy, a bullet or a cameo actor. An enemy's
  and a bullet's objects have the ids of the game's own objects, and the ROM check refuses a pack id that the ROM
  has. A cameo is refused by the rule for the level's object ids.
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
every frame. The ORBITBRIDGE case has no such check. In the test no glove or ball touched one of its planks. The
likely reason is in parse.c: it calls no `InitPlatformInfo` for these planks.
So their collision radius stays 0, and their faces never get the normals that `RecalculatePlatformInfo` keeps for
a platform with no motion and no rotation.

There are two variants of the pack:

- Variant I has the identity key. The bank check accepts it.
- Variant Z has keys of all zeros. The bank check refuses it. The test loads it anyway, by setting the game's pack
  globals directly. This is a measurement only. Its results do not fail `make models`.

Platforms are allowed if variant I gives equal output on Linux and under Wine in every case, and every save and load
gives equal hashes. This rule is stricter than the owner's. The result on one machine, under Wine 64-bit:

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
Before the game had `G64PlanesFit`, 700 small copies or 130 copies of 1500 faces made the game stop the program.
Retail banks add at most 51 blocks and about 3.1 MB ("Retail banks" above), and their blocks count toward the
parser's limit.

## The sample pack

The sample pack `packs/garib-island` has a model of its own and one retail model.

- The raft is a flat box, 48 by 6 by 36 units, with one picture of four planks, 16 by 16. Both are drawn from scratch
  by `packs/garib-island/models/make_raft.py`, which writes `raft.gltf` next to it. Its object id is 0x60CB35B6.
- It was imported with `gbtool import ROM raft.gltf --home DIR` into a user folder outside the repository. `gbtool
  pack ROM 0a level.xml OUTDIR "Garib Island" --home DIR` then wrote `pack.txt`, `level.lev` and `models.bin`.
- The level places the raft twice: as scenery in the sea, and as a platform (tag 200) that moves along z between
  z 145 and 205, at y 12. The glove falls onto that platform at the start and rides it.
- The level also places a Carnival 1 bell (0x45F8316C) as scenery. `models.bin` names its banks by number: texture
  bank 4 and object bank 20. It holds no retail data.
- `models.bin` is 1,632 bytes: the header of 16 bytes, the texture bank (560 bytes) and the object bank (1,056 bytes).
- `sample_pack` in tests/rom/instances.c checks the pack with the ROM's rules, finds the raft in the pack's object slot
  and the bell in the retail table, checks that the platform moves and that a glove with no input rides it, and plays
  the level with two players over a lossy network. The output is equal on Linux and under Wine.
- Glovebox's level reader cannot read a level file whose name and commands leave an odd number of padding bytes
  (`level::commands` reads the padding 2 bytes at a time). The level's name is `ISLANDS`, 7 characters. Its
  padding is then even.

## Steps

| Step | What | Done |
| --- | --- | --- |
| 1 | This page, and the bank switch split out of the loaders with no change to the game | yes |
| 2 | A bounded reader of compressed banks, and an index of the ROM's banks | yes |
| 3 | The bank check, its tests and its fuzz target | yes |
| 4 | The pack's own banks in free slots, and the proof that the check matches the loaders | yes |
| 5 | The test of platforms with custom models, on Linux and under Wine | yes |
| 6 | `models.bin` in the pack format, the pack tool and the cache | yes |
| 7 | The level file's object ids against the banks | yes |
| 8 | The offer and the transfer (protocol 12) | yes |
| 9 | Retail banks in the heap table, and their heap budget | yes |
| 10 | Glovebox's pack export with models | yes |
| 11 | A model in the sample pack | yes |
