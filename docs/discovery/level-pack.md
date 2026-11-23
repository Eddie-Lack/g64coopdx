# The level pack

A level pack is a folder with a custom level for a session. This file is the format, as g64coopdx reads it today
(`src/client/pack.h`), and what is still to come. [mods.md](mods.md) has the design around it.

## What works today

- A pack's level takes the place of one of the game's levels for the whole session. Entering that level loads the
  pack's level.
- The host gives the pack's folder to the program (`--pack FOLDER`). A joiner who lacks the pack downloads it from
  the host, after the player said yes ("Packs that came from a host" below). A joiner may also have the folder
  already.
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
level file is about 15 KB.

### Making a pack

`g64pack` (`tools/g64pack.c`, `make packtool`, and in each release) writes a pack's three files and reads the pack
back with the game's own reader. The level file comes from Glovebox: the `.lev` file its `.lev` button saves, or one
level of a ROM it built. For a ROM, the tool finds the level files through the two code pointers of the game's
loader, which Glovebox changes when it moves the files (`g64_pack_level_from_rom` in `src/client/pack.c`). For the
same level, the tool's pack has the same bytes and the same id as a pack made by Glovebox's own builder.

`g64pack` also prints whether the level file's structure is as the game reads it
([level-file-safety.md](level-file-safety.md), "What is checked today").

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

## Packs that came from a host

A joiner's hello names its pack. When the host's pack is another one, the host still gives the joiner a place, and
offers its pack over the sealed link (`src/session/session.h`: offer, want, piece, ready).

- The offer has the pack's id, name and sizes. The joiner's program shows the name and size and asks the player.
  Nothing is stored before the answer. On the command line, `--accept-pack` is the answer; without it the session
  ends.
- The joiner asks for the bytes by offset, and the host answers each request with up to 8 pieces of 1,024 bytes.
  The host keeps no state for a download. It serves one player at most every 20 ms and sends at most 256 pieces a
  second in all: a download must not crowd out a running game's messages.
- One joiner gets at most three times the pack's size, and then loses the place. Players without the pack hold at
  most four places at once.
- The joiner takes pieces in order only, up to the sizes of the first offer. A second offer of another pack ends
  the session.
- The joiner reads the bytes as a pack in the strict form, and compares its id and name with the offer's. Only then
  does it tell the host that it is ready. Until then the host does not count the player: they are not in the
  game, get no start and no chat, and their place is free again after five minutes.
- A host without a pack offers "none", and a joiner who came with a pack plays without it.
- The host's pack is the joiner's for that session only. The joiner's next session starts with its own pack.
- The host does not offer a pack whose level file is a level of the ROM, one that fails the check, or one that
  takes the place of a hub, a boss level, a bonus level or a scene. The game's code for those levels expects fixed
  things in them ([level-file-safety.md](level-file-safety.md)). It then
  refuses a joiner who lacks the pack, as before.

A pack whose level file is a shipped level with small changes does travel. The comparison catches exact copies
only.

- `g64_pack_from_bytes` reads a pack from its three files' bytes. Its strict form also holds the level file to the
  check of its structure ([level-file-safety.md](level-file-safety.md), "What is checked today"). A pack from
  another player is read this way. A pack from a folder the player chose is not held to that check.
- A downloaded pack is kept in `packs/ID` in the program's folder for its own files, where `ID` is the pack's id as
  32 hexadecimal digits. No text from the network is part of a path. A kept pack is read with the strict form
  again, and its files must give the id its folder names.
- `g64_pack_rom_level` says whether a pack's level file has the same bytes as one of the ROM's levels. Such a pack
  holds game data, and a host must not send it.

## How the game reads it

The client puts the level file, and the script after it, after the ROM's bytes in a buffer of its own, and the game reads its ROM from that
buffer (`engine_set_rom`). The level's loader then reads the pack's file as it reads the ROM's (`LoadLandscapeFile`
in `weemitt/src/port/loaders.c`). The game's state holds only the borrowed level's number and the file's offset and
size, which are the same on every machine. It holds no pointer to the pack.

## Still to come

- **The rest of the level file's check.** A downloaded pack must pass the check of its structure, and the game
  guards many of the values it reads. [level-file-safety.md](level-file-safety.md) lists what is still open. The
  joiner is told to take a pack only from a host they trust.
- **A list of packs in the lobby.** A room's entry does not name its pack yet.
- **More in a pack:** the author's own models and textures in banks of their own, more than one level, and more
  for a script to call.
