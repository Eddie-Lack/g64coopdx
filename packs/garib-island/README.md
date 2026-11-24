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

`models.bin` holds no data from the game. The bell comes from each player's own ROM.

## Making it again

You need Glovebox's `gbtool` and your own ROM. Use a user folder outside the repository.

```sh
python3 packs/garib-island/models/make_raft.py
gbtool import ROM packs/garib-island/models/raft.gltf --home DIR
mkdir OUT && cp packs/garib-island/script.lua OUT/
gbtool pack ROM 0a packs/garib-island/level.xml OUT "Garib Island" --home DIR
cp OUT/pack.txt OUT/level.lev OUT/models.bin packs/garib-island/
build/g64pack check packs/garib-island --rom ROM
```

The raft's object id is 0x60CB35B6. Glovebox makes it on import from the name `raft` and the bytes of `raft.gltf`.
A change to the model gives a new id, and `level.xml` must then use it. The level uses the id twice: as scenery,
and as a platform with tag 200.
