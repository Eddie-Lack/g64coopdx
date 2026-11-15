# Upstream notes

Changes that would help in gloveworld, weemitt or Glover-Practice-Rom, and errors found in them. Each entry names
the project, the file, the change or the error, and whether the owner needs to decide.

## weemitt

The brute forcer now lives in weemitt (`bruteforce/`), so these can be changed directly when they are needed.

- `src/port/loaders.c`: `converted` is a 1 MB static bitmap that is only used while a bank loads. It makes up most
  of the game's writable data outside `port_ram`, so every savestate of the brute forcer holds it. A buffer on the
  heap or a smaller bitmap would save 1 MB a state. No decision needed: the brute forcer can work around it.
- `src/port/platform.c`, `hash_ram`: on Windows the program loads at 0x400000, and the ROM offsets in the game's data
  (up to 0x77DE20) fall in the program's address range, so the hash counts them as pointers to the program. Its
  docs already say to compare traces, not hashes, between Linux and Windows. Linking the Windows build at a higher
  address (`-Wl,--image-base=0x08000000`, as the brute forcer does) would remove that cause. Raw sound data in
  `pbankBuffer` that looks like an address in `port_ram` still differs. No decision needed.
- `src/game/game.c` and `src/game/cheat.c`: R or Z in the pause menu opens weemitt's cheat list, which the N64 game
  does not have. It also hides those presses from the game's own cheat-code entry. The brute forcer turns it off
  (`engine/hooks.def`, `CheatMenuAvailable`). An option in weemitt that turns off every PC addition that the
  controller can reach would make this sure for later additions. No decision needed now.
- Floating point: the N64 flushes denormal results to 0 (libultra sets FCSR.FS). weemitt's x86 build keeps them.
  In the carnival and space 3 bosses the camera computes a denormal at times. Setting the SSE flush-to-zero flag
  would bring weemitt nearer the console, but BizHawk keeps denormals too, and the brute forcer must match BizHawk.
  No decision needed.
