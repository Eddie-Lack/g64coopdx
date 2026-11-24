# The Lua trial

Before g64coopdx had any scripting code, a trial answered five questions about Lua in a game where every machine
must reach the same state ([mods.md](mods.md#scripts-in-a-rollback-game)). It ran on 22 November 2026 with Lua 5.4.8,
built for 64-bit Linux (gcc) and for 64-bit Windows (MinGW-w64, static), the Windows program under Wine. Both builds
used the game's float flag (`-ffp-contract=off`). The trial's program is not in the repository; this file holds what
it found and what follows from it. [roadmap.md](roadmap.md), phase 5, says what g64coopdx built after it.

## The source

Lua 5.4.8 from `https://www.lua.org/ftp/lua-5.4.8.tar.gz`. Its SHA-256 is
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`, as lua.org's own list gives it.

## What the trial did

Lua ran on a fixed block of 4 MB with an allocator of the trial's own (blocks with their size in front, a free list
by address, first fit). One script ran on both systems. It printed arithmetic on floats and integers, number
literals, text to number and number to text, `string.format`, the `math` library, the order of `pairs` over text,
number and boolean keys, the order of `pairs` over table and function keys, a sort, and the memory in use after a
full collection. The program then printed the allocator's own counts and a hash of the block's bytes.

## What it found

| Question | Answer |
| --- | --- |
| Are a script's results the same? | Yes: every line the script printed was equal, the `math` library's sine, cosine, exponent and logarithm included |
| Is the order of `pairs` the same? | For text, number and boolean keys: yes, once the string hash's seed is a constant (`luai_makeseed`). For table and function keys: no, as built. Lua hashes such a key by its address |
| Can that be fixed? | Yes. Lua's `point2uint` turns an address into a hash. With it defined as the address's offset in the block (and 0 for an address outside it), the order was equal on both systems. Lua 5.4.8 defines `point2uint` without a guard (an `#if !defined` test that lets the build give its own definition), so this needs one changed line in `llimits.h` |
| Does the allocator behave the same? | Yes: the block's high mark, the bytes in use, the peak, and the numbers of allocations and frees were equal |
| Are the block's bytes the same? | No. The block holds addresses: of its own objects, of Lua's C functions, and of Lua's static objects. Its hash differed between the systems, and between two runs on Linux |

## What follows

1. **Vendor Lua 5.4.8 with one patch**: a guard around `point2uint`. Build it with a constant `luai_makeseed`, the
   block-offset `point2uint`, and the game's float flags.
2. **The block is saved but not hashed.** A savestate must hold the block: a rollback restores the scripts with the
   game, in the same process, where the addresses are still right. The state hash that machines compare must leave
   it out. At the time of the trial, the brute forcer's engine saved and hashed the game's data sections together
   (`weemitt/bruteforce/engine/engine.c`). The block needs a section that is saved and tracked but not hashed.
3. **A script's state is compared through numbers, not bytes.** The allocator's counts were equal on both systems.
   Kept as game variables, they are in the hash, and a script that went another way on one machine shows as a
   mismatch soon after. A script also changes the game only through the scripts' API
   ([mods.md](mods.md#the-lua-api)), so the game's own hash shows a difference that matters.
4. **C functions as table keys** all hash to 0, because their addresses are outside the block. Such keys work, and
   their order is the order they were put in.
5. **Number to text and text to number go through the C library** (`snprintf` with `%.14g`, `strtod`). They agreed
   in the trial, on Linux and under Wine. Wine's C library is not Windows's, so this is not proven for a real Windows
   machine. MinGW's own `printf` and `strtod` (`__USE_MINGW_ANSI_STDIO`) are linked into the Windows program and do
   not depend on the system: build with them, and keep number to text out of a script's logic as [mods.md](mods.md)
   already says.
6. **The `math` library's functions come from the C library too.** They agreed in the trial for the values tried.
   The plan in [mods.md](mods.md) is to replace them with the game's own sine and cosine and with functions made of
   the four basic operations.

## Not tried

- A real Windows machine.
- The block inside the engine's savestate: a rollback over a frame in which a script ran.
- The count hook (`lua_sethook`) as a limit on a script's work, and what happens when the block is full: both
  must end a script the same way on every machine.
- Coroutines, `string.format('%a')`, and integer division and modulo by zero.
