# Playing online (prototype)

This is the first online version. Two to sixteen players play the first hub level together over the internet or a
local network. It is a test build: see "What does not work yet" below.

## What you need

- Your own Glover (USA) ROM. Every player needs the same ROM: the host refuses a different one.
- The online program:
  - Windows: `g64coopdx-windows` from the repository's Actions page (the "Online program" workflow) or from a
    release. Unzip it; `g64coopdx.exe` and `SDL2.dll` stay together.
  - Linux: `g64coopdx-linux.tar.gz` from the same places, or `make client`, which builds
    `weemitt/build/studio64/weemitt-studio`.

The examples below use the Windows name, `g64coopdx.exe`, run from a command prompt in the unzipped folder.

## Host a game

The host starts first and waits for the others:

```sh
g64coopdx.exe --rom glover.z64 --host 27064 --players 2
```

- Windows may ask whether to allow the program on networks. Allow it.
- The host's computer must accept UDP on that port from the internet. On a home router, forward UDP port 27064 to the
  host's computer.
- Give the other players your public IP address (a search for "what is my IP" shows it).

## Join a game

```sh
g64coopdx.exe --rom glover.z64 --join HOST-ADDRESS:27064
```

The game starts when every player is in. The host is player 1. Each player sees their own glove's camera. Player 1's
glove keeps the game's colors; player 2's is blue, player 3's green and player 4's yellow. The
window's title shows your player number, how many frames your game runs ahead of the confirmed ones, and any desyncs.
The program also prints a status line every 100 frames.

## How it works

The host confirms every player's input for each frame, a quarter of a second behind its own clock. Every machine
predicts the frames it has no confirmed input for yet, and goes back and runs them again when the confirmed input
differs ([discovery/netcode.md](discovery/netcode.md)). Only inputs cross the network; no game data does. Each machine
checks the hash of its game against the host's every 10 frames.

## What does not work yet

- There is no sound.
- Only the first hub level. Enemies, collectables and puzzles react only to player 1.
- No lobby or relay: the host must forward its port. The lobby at `lobby.slugs.io` comes later.
- A player who leaves ends the game for everyone.
