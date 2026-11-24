# The level pack

A level pack is a folder with a custom level for a session. This file is the format, as g64coopdx reads it today
(`src/client/pack.h`), and what is still to come. [mods.md](mods.md) has the design around it.

## What works today

- A pack's level takes the place of one of the game's levels for the whole session. Entering that level loads the
  pack's level.
- The host gives the pack's folder to the online program (`--pack FOLDER`). A joiner who lacks the pack downloads
  it from the host, after the player says yes ("Packs that came from a host" below). A joiner may also have the
  folder already.
- The level places objects that the borrowed level's own banks hold, and the pack's own models
  ([model-banks.md](model-banks.md)). The borrowed level is the game's level whose place the pack's level takes. A
  bank is one block of the ROM: a texture bank holds pictures, and an object bank holds objects. A level file that
  names an object the banks do not hold makes the game stop: there is no check for it yet.
- A pack with models travels as the other files do. The host checks its models on its own ROM before it offers
  the pack, and a joiner checks them again on theirs.

## The folder

| File | What it holds |
| --- | --- |
| `pack.txt` | the manifest |
| `level.lev` | the level, in the game's own format, as Glovebox writes it |
| `script.lua` | a script, as Lua source text. A pack may have none ([scripts.md](../scripts.md)) |
| `models.bin` | the pack's own models and the numbers of retail banks, the ROM's own banks. A pack may have none ([model-banks.md](model-banks.md)) |

A pack holds no game data. The level file names models and textures by their ids, and each player's own ROM
supplies them. The models in `models.bin` are the author's own. They must hold none of the ROM's ids or data.

### `pack.txt`

Lines of `key=value`. Each line ends with a line feed alone: a carriage return is refused. The pack's id is a hash of
the bytes of all the pack's files, this one too ("The pack's id" below). The same pack must not have two ids, and a
carriage return would give it a second one. Each key comes once, in any order. Nothing else is allowed:
no empty line, no comment, no other key.

| Key | Value |
| --- | --- |
| `format` | `1` |
| `name` | 1 to 32 printable ASCII characters |
| `level` | the level whose place the pack's level takes: 0 to 47 (weemitt's `enum Level`), with no leading zero |

The file is 1,024 bytes at most.

    format=1
    name=My first level
    level=10

The manifest is a list of keys and not JSON. A list of keys needs a parser of some twenty lines, which is easier to
make safe for bytes from the network.

### `level.lev`

The game's level file: a length (4 bytes, big-endian), the name of the level's landscape file, then the level's
commands. The length is the file's whole size, and the reader checks it. The file is 1 MB at most. The game's largest
level file is about 15 KB.

### Making a pack

`g64pack` (`tools/g64pack.c`, `make packtool`, and in each release) writes a pack's files and reads the pack back
with the game's own reader. [playing-online.md](../playing-online.md#making-a-pack) has the steps and the command
lines. `--script FILE` adds a script, and `--models FILE` adds a `models.bin`. The tool refuses
to write into a folder that already holds a `script.lua` or a `models.bin` it was not given. The level file comes
from Glovebox: the `.lev` file its `.lev` button saves, or one level of a ROM it built. For a ROM, the tool finds
the level files through the two code pointers of the game's loader, which Glovebox changes when it moves the files
(`g64_pack_level_from_rom` in `src/client/pack.c`). For the same level, the tool's pack has the same bytes and the
same id as a pack made by Glovebox's own pack export (its `Export level pack` button).

`g64pack` also prints whether the level file's structure is as the game reads it
([level-file-safety.md](level-file-safety.md), "What is checked today"). For `models.bin` it prints the banks'
sizes, the counts of textures and objects, and the retail bank numbers. `g64pack check DIR --rom ROM` also runs the
rules that need the player's ROM: the level file is not one of the ROM's, the models hold none of the ROM's ids,
textures or vertices, and every object the level names is in a bank the level will have
([model-banks.md](model-banks.md), "The level's object ids"). The online program runs the same rules on the host
before its offer, on a joiner after a download, and again when the game starts. The tool does not link the game.
It finds the ROM's banks with a table of the game's bank switch (`src/client/bankranges.h`, written by
`tools/gen_bankranges.py`; `make rombanks` compares it with the game's own), and each level's banks with a table of
their numbers (`make levcheck` compares it with the game's). The bank switch is the game's code that gives each
bank's range in the ROM.

### `script.lua`

Lua source text: 1 byte to 256 KB, with no zero byte. Lua's compiled form is refused. The script runs in every game
of the session, not only in the pack's level. There is one game for each level that has players in it: an instance
([scripts.md](../scripts.md)).

### `models.bin`

A header, then the pack's own texture bank and object bank, at most 4 MiB together. The header may also name up to
four retail texture banks and four retail object banks by number. [model-banks.md](model-banks.md), "The file", has
the layout. The file is 4 MiB and 24 bytes at most.

Every `models.bin` passes the bank check (`src/client/bankcheck.h`), even one from a folder the player chose. The
game's loaders trust every field of a bank. A bad one would make them write outside it. Once the program has the
ROM, the models also pass the rules that need it (`g64_models_check_rom`). Each player's game loads the retail banks
that the file names from the player's own ROM ([model-banks.md](model-banks.md), "Retail banks"). A retail object
bank needs the texture banks of all its sprites: the font's (bank 10), GENERIC (bank 2), the level's own, or retail
texture banks that the file names. The client refuses a pack that lacks one.

## The pack's id

The id is the first 16 bytes of the BLAKE2b hash (32 bytes of output, no key) of the pack's canonical form:

1. the text `g64pack 1` and a zero byte;
2. for `pack.txt`, then `level.lev`, then `script.lua` and `models.bin` if the pack has them: the file's name and a
   zero byte, the file's length (4 bytes, little-endian), and the file's bytes.

A pack without `models.bin` has the same id as before the file was part of the format.

BLAKE2b comes with the cryptography library that g64coopdx already holds (`third_party/monocypher`).

A joiner's first message to the host, its hello, carries the id of its pack (`src/session/session.h`). A pack
changes the game, so the host and every joiner must have the same one.

## Packs that came from a host

A joiner's hello names its pack. When the host's pack is another one, the host still gives the joiner a place among
the session's players. It offers its pack over the sealed link: the encrypted messages that follow the hello and the
host's welcome (`src/session/session.h`: offer, want, piece, ready).

- The offer has the pack's id, name and the sizes of its four files. The joiner's program shows the name, the size
  and the size of the models, and asks the player.
  Nothing is stored before the answer. On the command line, `--accept-pack` is the answer; without it the session
  ends.
- The joiner asks for the bytes by offset, and the host answers each request with up to 8 pieces of 1,024 bytes.
  The host keeps no state for a download. It serves one player at most every 20 ms and sends at most 256 pieces a
  second in all. A download must not crowd out a running game's messages.
- One joiner gets at most three times the pack's size, and then loses the place. Players without the pack hold at
  most four places at once.
- The joiner takes pieces in order only, up to the sizes of the first offer. A second offer of another pack, or
  with other sizes, ends the session.
- The bytes are `pack.txt`, `level.lev`, `script.lua` and `models.bin`, one after another, with no bytes for a
  file the pack lacks (`g64_pack_blob`). The joiner splits them by the offer's sizes (`g64_pack_from_blob`).
- The joiner reads the bytes as a pack in the strict form, with the bank check. Its models must also pass the rules
  that need the joiner's own ROM. It compares the pack's id and name with the offer's (`g64_pack_from_download`).
  Every object the level file names must be in a bank the level will have, on the joiner's own ROM
  (`g64_pack_check_ids_rom`). Only then does it tell the
  host that it is ready. Until then the host does not count the player: they are not in the
  game, get no start and no chat, and their place is free again after five minutes. For a pack larger than about
  1.9 MB the place is held for longer: one minute more than the download takes when each round trip takes one
  second ([model-banks.md](model-banks.md), "The network").
- A host without a pack offers "none", and a joiner who came with a pack plays without it.
- The host's pack is the joiner's for that session only. The joiner's next session starts with its own pack.
- The host does not offer a pack whose models fail the bank check or the rules that need the host's ROM
  (`g64_pack_may_send`). Its log says why.
- The host does not offer a pack whose level file is a level of the ROM, one whose level file fails the check of its
  structure, or one that
  takes the place of a hub, a boss level, a bonus level or a scene. The game's code for those levels expects fixed
  things in them ([level-file-safety.md](level-file-safety.md)). In that case the host refuses a joiner who lacks
  the pack.
- A pack whose level file names an object that no bank of its level has stops the host's session before the offer
  ([model-banks.md](model-banks.md), "The level's object ids").

The host's test for a level of the ROM compares whole files (`g64_pack_rom_level`, below). It catches exact copies
only. A pack whose level file is a shipped level with small changes does travel.

- `g64_pack_from_bytes` reads a pack from its files' bytes. Its strict form also holds the level file to the
  check of its structure ([level-file-safety.md](level-file-safety.md), "What is checked today"). A pack from
  another player is read this way. A pack from a folder the player chose is not held to the check of its level
  file's structure.
- A downloaded pack is kept in `packs/ID` in the program's folder for its own files, where `ID` is the pack's id as
  32 hexadecimal digits. No text from the network is part of a path. A kept pack is read with the strict form
  and the bank check again, and its files must give the id its folder names. Keeping a pack removes a `script.lua`
  or `models.bin` in its folder that the pack does not have.
- `g64_pack_rom_level` says whether a pack's level file has the same bytes as one of the ROM's levels. Such a pack
  holds game data, and a host must not send it.

## How the game reads it

The client puts the level file, the script and the models' two banks after the ROM's bytes in a buffer of its own,
and the game reads its ROM from that buffer (`engine_set_rom`). The banks start at an offset that is a multiple of 8
(`g64_pack_layout_of` in `src/client/pack.h`). The level's loader then reads the pack's file as it reads the ROM's
(`LoadLandscapeFile` in `weemitt/src/port/loaders.c`). The game's state holds only the borrowed level's number and
the file's offset and size, and the banks' offsets and sizes, which are the same on every machine. It holds no
pointer to the pack. The game loads the pack's banks when the pack's level starts
(`weemitt/src/game/g64packbanks.c`).

## Still to come

- **The rest of the level file's check.** A downloaded pack must pass the check of its structure, and the game
  guards many of the values it reads. [level-file-safety.md](level-file-safety.md) lists what is still open. The
  prompt before a download tells the joiner to take a pack only from a host they trust.
- **A list of packs in the lobby.** A room's entry on the relay's list does not name its pack yet. A room is a
  host's session on the relay ([server/README.md](../../server/README.md)).
- **The rest of models in a pack:** the loading of retail banks. [model-banks.md](model-banks.md) is the plan.
- **More in a pack:** more than one level, and more for a script to call.
