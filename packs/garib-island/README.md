# Garib Island

A sample level pack for g64coopdx. It takes the place of Atlantis 1.

## The files

| File | What |
| --- | --- |
| `pack.txt` | The pack's name and the level it replaces |
| `level.xml` | The level's Glovebox source, written from scratch |
| `level.lev` | The level file that Glovebox made from `level.xml` |
| `script.lua` | The script: it counts garibs and says who reaches the exit first |
| `models.bin` | The raft's texture bank and object bank, and the numbers of the bell's banks (Carnival's texture bank 4 and object bank 20) |
| `models/make_raft.py` | Draws the raft from scratch and writes `models/raft.gltf` |

The game reads models from banks. A texture bank holds pictures, and an object bank holds objects that use those
pictures. The game's own banks are blocks of the ROM, and a pack names them by number
([model-banks.md](../../docs/discovery/model-banks.md)). `models.bin` holds no data from the game. The bell comes
from each player's own ROM.

## Making it again

You need your own ROM and `gbtool` from Glovebox, the level editor. In a Glovebox checkout,
`cargo run -p glovebox-core --example gbtool --` runs it. Glovebox keeps imported models in its user folder, the
folder that `--home DIR` names. Use one outside the repository. `make packtool` builds `build/g64pack`. Run the
commands from the repository's root.

```sh
python3 packs/garib-island/models/make_raft.py
gbtool import ROM packs/garib-island/models/raft.gltf --home DIR
mkdir OUT && cp packs/garib-island/script.lua OUT/
gbtool pack ROM 0a packs/garib-island/level.xml OUT "Garib Island" --home DIR
cp OUT/pack.txt OUT/level.lev OUT/models.bin packs/garib-island/
build/g64pack check packs/garib-island --rom ROM
```

`0a` is Glovebox's id for level 10, Atlantis 1: the level's number in hexadecimal.

The raft's object id is 0x60CB35B6. Glovebox makes it on import from the name `raft` and the bytes of `raft.gltf`.
A change to the model gives a new id, and `level.xml` must then use it. The level uses the id twice: as scenery,
and as a platform with tag 200 (`PlatSetTag` in `level.xml`).
