# Experiments

Small tools from the discovery, the study that [README.md](../README.md) reports. The terms are defined in
[README.md](../README.md#terms). They aren't part of the project's code yet; `determinism.py` is meant to grow into its
N-player determinism test. They hold no game data: each reads the player's own ROM at run time. Keep their output
(dumps, saves, traces) outside the repository.

| File | What it does | Results |
| --- | --- | --- |
| `determinism.py` | Runs a weemitt build twice with the same input and compares the trace (the per-frame state line of weemitt's `--trace`) and memory hash; with `--other`, also a second build (`--other-prefix wine` for a Windows build) | [weemitt.md](../weemitt.md#repeated-runs-with-the-same-input) |
| `memdiff.py` | Counts the bytes and pages of the game's memory that change in 1 and 20 frames, and how well they compress | [weemitt.md](../weemitt.md#how-much-memory-changes-in-a-frame) |
| `g64probe.c` | A tiny program that loads `libglover` (or `glover.dll`), steps the game, and with two copies of the library runs two games in one process | [weemitt.md](../weemitt.md#a-separate-program-on-the-library) |
| `inventory/` | A clang syntax-tree scan of every use of the glove, ball and other globals in weemitt's `src/game`, and gdb probes of the frame limits | [state-inventory.md](../state-inventory.md#4-measuring-the-refactor-every-use-of-the-glove-and-ball-globals) |

## Running them

They need a weemitt checkout. The discovery used weemitt at commit `f509ef7` ([README.md](../README.md#method)). Set
`WEEMITT` to its path; without it, `determinism.py` looks for `../weemitt` from the folder you run it in. Make weemitt's
test builds first: `make headless headless64 lib64-stub libhost64` (the last two give the library and its test host,
`build/libhost64/glover-host`), and the Windows ones for the Wine runs (see weemitt's docs/building.md).
`libhost-windows64` gives `build/libhost-windows64/glover-host.exe`.

```sh
WEEMITT=~/weemitt python3 determinism.py --rom ROM --exe ~/weemitt/build/headless/weemitt \
  --other ~/weemitt/build/headless64/weemitt --frames 6000 $(seq 0 47) attract
```

```sh
gcc -O2 -I ~/weemitt/src/lib g64probe.c -o g64probe -ldl
cp ~/weemitt/build/lib64-stub/libglover.so libglover-a.so && cp libglover-a.so libglover-b.so
./g64probe ROM 5 2000 ./libglover-a.so ./libglover-b.so
```

The arguments of `g64probe` are the ROM, the level, the number of frames, and one or two library files.

```sh
x86_64-w64-mingw32-gcc -O2 -I ~/weemitt/src/lib g64probe.c -o g64probe.exe -lpthread -static
```

The same comparison against the Windows library under Wine (copy `glover.dll` next to `glover-host.exe` first):

```sh
WEEMITT=~/weemitt python3 determinism.py --rom ROM --exe ~/weemitt/build/libhost64/glover-host \
  --other ~/weemitt/build/libhost-windows64/glover-host.exe --other-prefix wine --other-env WINEPREFIX=$HOME/.wine64 --frames 3000 $(seq 0 47) attract
```

`memdiff.py` reads three dumps that weemitt writes with `--dump`; its header says how to make them. The inventory needs
clang (and gdb for the probes): `sh inventory/run_all.sh`, with `REPO` set to the weemitt checkout. Unlike
`determinism.py`, this script doesn't read `WEEMITT`.

For the brute forcer's savestate numbers, build it first (`cd bruteforce && make` in weemitt), then run
`bruteforce/build/linux/glover bench --rom ROM --level 13 --seconds 3 --workers 1`, and the same with
`GLOVER_FULL_STATES=1`. That setting turns page tracking off: every save and load copies everything.
