# Test fixtures

These files hold no game data. Nothing in them comes from the ROM.

## The cube

- `cube.gltf` is a glTF 2.0 model of a cube, made from scratch. It has 12 triangles and one 8 by 8 checker
  texture. A script draws both: `crates/core/tests/data/make_cube.py` in Glovebox. The buffer and the PNG are data
  URIs inside the file.
- `cube.models.bin` is that cube as a pack's `models.bin` ([model-banks.md](../../docs/discovery/model-banks.md),
  "The file"). It has one texture bank and one object bank of the pack's own, and no numbers of the game's banks.

Glovebox made `cube.models.bin` with its own model converter. In a Glovebox checkout, run:

    cargo run -p glovebox-core --example gbtool -- modelbanks crates/core/tests/data/cube.gltf cube.models.bin

The command needs no ROM. It converts the model at scale 1, with the file's name (`cube`) as the model's name. It
gives the same bytes on every run. Glovebox's test `the_cube_clashes_with_no_game_id` checks that the cube's object
id and texture id are in none of the game's banks: it reads every object bank and texture bank in the ROM map.

What the file holds:

- The texture bank is 176 bytes: a count of 1, one record of 168 bytes, then 4 zero bytes. The record is
  16-bit RGBA, 8 by 8. Its size field counts the 4 bytes of padding at its end. No list of names follows it.
- The object bank is 1056 bytes. It holds one object, id `0x94D4B65B`, which uses texture `0x0B21BC4C`.
- The rotation key is (0, 0, 0, 1), the key of no turn.
