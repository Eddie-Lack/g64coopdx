# The level pack

A level pack is a folder with a custom level for a session. This file is the format, as g64coopdx reads it today
(`src/client/pack.h`), and what is still to come. [mods.md](mods.md) has the design around it.

## What works today

- A pack's level takes the place of one of the game's levels for the whole session. Entering that level loads the
  pack's level.
- Every player has the pack's folder already and gives it to the program (`--pack FOLDER`). A host refuses a joiner
  whose pack differs, or who has none.
- The level places objects that the borrowed level's own banks hold. A level file that names an object the banks
  do not hold makes the game stop: there is no check for it yet.

## The folder

| File | What it holds |
| --- | --- |
| `pack.txt` | the manifest |
| `level.lev` | the level, in the game's own format, as Glovebox writes it |
| `script.lua` | a script, as Lua source text. A pack may have none ([scripts.md](../scripts.md)) |

A pack holds no game data. The level file names models and textures by their ids, and each player's own ROM
supplies them.

### `pack.txt`

Lines of `key=value`. Each line ends with a line feed alone: a carriage return is refused, since the pack's id is a
hash of the file's bytes and the same pack must not have two ids. Each key comes once, in any order. Nothing else is allowed: no empty line, no comment, no other key.

| Key | Value |
| --- | --- |
| `format` | `1` |
| `name` | 1 to 32 printable ASCII characters |
| `level` | the level whose place the pack's level takes: 0 to 47 (weemitt's `enum Level`), with no leading zero |

The file is 1,024 bytes at most.

    format=1
    name=My first level
    level=10

The first plan named a JSON manifest. A list of keys needs a parser of some twenty lines, which is easier to make
safe for bytes from the network.

### `level.lev`

The game's level file: a length (4 bytes, big-endian), the name of the level's landscape file, then the level's
commands. The length is the file's whole size, and the reader checks it. The file is 1 MB at most. The game's largest
level file is about 150 KB.

### Making a pack

`g64pack` (`tools/g64pack.c`, `make packtool`, and in each release) writes a pack's three files and reads the pack
back with the game's own reader. The level file comes from Glovebox: the `.lev` file its `.lev` button saves, or one
level of a ROM it built. For a ROM, the tool finds the level files through the two code pointers of the game's
loader, which Glovebox changes when it moves the files (`g64_pack_level_from_rom` in `src/client/pack.c`). For the
same level, the tool's pack has the same bytes and the same id as a pack made by Glovebox's own builder.

### `script.lua`

Lua source text: 1 byte to 256 KB, with no zero byte. Lua's compiled form is refused. The script runs in every game
of the session, not only in the pack's level ([scripts.md](../scripts.md)).

## The pack's id

The id is the first 16 bytes of the BLAKE2b hash (32 bytes of output, no key) of the pack's canonical form:

1. the text `g64pack 1` and a zero byte;
2. for `pack.txt`, then `level.lev`, then `script.lua` if the pack has one: the file's name and a zero byte, the
   file's length (4 bytes, little-endian), and the file's bytes.

BLAKE2b comes with the cryptography library that g64coopdx already holds (`third_party/monocypher`).

The session's hello carries the id (`src/session/session.h`). A pack changes the game, so both machines must have
the same one.

## How the game reads it

The client puts the level file, and the script after it, after the ROM's bytes in a buffer of its own, and the game reads its ROM from that
buffer (`engine_set_rom`). The level's loader then reads the pack's file as it reads the ROM's (`LoadLandscapeFile`
in `weemitt/src/port/loaders.c`). The game's state holds only the borrowed level's number and the file's offset and
size, which are the same on every machine. It holds no pointer to the pack.

## Still to come

- **A check of the level file.** The game's level parser is the original game's and trusts what it reads. Before a
  pack may come from another player, the reader must check every command of the level file, and the ids it names
  against the player's ROM. [level-file-safety.md](level-file-safety.md) lists what that check must hold: it is a
  large piece of work, and the owner decides whether to do it or to keep packs to people who trust each other.
- **Packs from the host.** A joiner without the pack is shown its name and size and asked. The host then sends it in
  pieces, and the joiner checks its id ([mods.md](mods.md#how-mods-reach-other-players)).
- **More in a pack:** the author's own models and textures in banks of their own, more than one level, and more
  for a script to call.
