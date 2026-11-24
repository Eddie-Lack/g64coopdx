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
- Custom models may be platforms, if a test shows that they give the same game on Linux and Windows.

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
- The file is exactly H + T + O bytes long.
- The pack's id hashes `models.bin` after `script.lua`, in the same form as the other files.

## The check

A bank from a pack is untrusted. The game trusts every field of a bank: its loaders follow its offsets and counts
with no bounds. So the client checks each bank in full before the game sees it, as it checks a level file
([level-file-safety.md](level-file-safety.md)). The check is `src/client/bankcheck.c`. It reads the bytes as the
game's loaders do, and it refuses anything it does not understand.

Some rules allow only what Glovebox writes today. A test on Glovebox's real output pins each of them. A new kind
of model in Glovebox widens the rule and its test together.

What the check refuses, in short:

- A bank that starts with `FLA2`. The game would decompress it, and its decompressor has no bounds.
- Any byte that no structure reaches and that is not 0.
- A texture record whose size, format, dimensions or palette differ from what Glovebox writes. A texture that no
  object uses.
- An animated texture. The game keeps at most 20 of them, with no bound check.
- A pointer that is not aligned, that has high bits set, or that names a range outside the bank.
- Two structures that share bytes. The loaders fix each pointer each time they reach it, so a shared one breaks.
- An object tree deeper than 32 or with more than 4096 objects.
- A face index outside its mesh's vertices.
- A display list command that Glovebox does not write, or a texture load in another form than Glovebox's.
- An object with the name of the ball or the hand. The game points the ball's and the hand's textures at those.
- A float that is not finite, a position past 16384 or a scale past 64.
- A rotation key that is not near unit length. Glovebox writes a key of all zeros today. The game turns it into a
  matrix of NaN values (not-a-number). The pack export writes the identity key in its place.

With the player's ROM, the client also refuses:

- A pack id of an object or texture that the ROM already has.
- A texture, a vertex list or a mesh that is equal to one in the ROM.
- A level file that names an object found in no bank the level will have.
- A level whose actors would need more memory for their collision than the budget allows.

A fuzz target runs the check on random and mutated banks. For every bank the check accepts, it runs a copy of the
game's loaders that stops at any read or write outside the bank. A ROM test proves that this copy does what the
game does.

## Loading

- The game's switch of bank numbers moves into `TextureBankRange` and `ObjectBankRange`. `G64BankRom` gives the
  same ranges to the client.
- The pack's own banks load into free slots of the game's five, right after the level's own object bank. A level
  always leaves one object slot free. If none is free, the level stops as unplayable.
- Retail banks load into a table on the game's heap, not into slots. One free object slot cannot hold the pack's
  bank and a retail bank. Also, the game searches texture slots from the last one down, so a retail bank in a slot
  would win over the level's own. The table is searched after the slots, as in Glovebox's mod. This differs from
  the default chosen at the start.
- Everything loads onto the game's heap, inside the saved and hashed memory. Every machine has the same ROM and
  pack, so every machine makes the same allocations.

## Platforms

A rotation key of all zeros gives NaN values in the drawn matrices. Platforms, water and debris read the key, but
scenery does not. The game then turns each NaN into an integer, which C leaves undefined. The check refuses the
zero key, so no NaN from a pack reaches the game. Platforms are allowed if a test with the identity key gives the
same output on Linux and under Wine. The test also records what the zero key gives, as a measurement only. This
is stricter than the owner's rule.

## The heap

A full heap stops the game. So the limits on banks, retail banks and collision come from a measurement: the free
heap and the free blocks at the point where the banks load, in the fullest level, with 16 players. A test loads a
pack at those limits and checks that the level plays or stops as unplayable, and never stops the game.

## Steps

| Step | What | Done |
| --- | --- | --- |
| 1 | This page, and the bank switch split out of the loaders with no change to the game | yes |
| 2 | A bounded reader of compressed banks, and an index of the ROM's banks | yes |
| 3 | The bank check, its tests and its fuzz target | |
| 4 | The pack's own banks in free slots, and the proof that the check matches the loaders | |
| 5 | The test of platforms with custom models, on Linux and under Wine | |
| 6 | `models.bin` in the pack format, the pack tool and the cache | |
| 7 | The level file's object ids against the banks, and the heap budget | |
| 8 | The offer and the transfer (protocol 12) | |
| 9 | Retail banks in the heap table | |
| 10 | Glovebox's pack export with models | |
| 11 | A model in the sample pack | |
