# Playing online (prototype)

This is the first online version. Two to sixteen players play the first hub level together over the internet or a
local network. It is a test build: see "What does not work yet" below.

## What you need

- Your own Glover (USA) ROM. Every player needs the same ROM: the host refuses a different one.
- The online program, built with `make client`: `weemitt/build/studio64/weemitt-studio`.

## Host a game

The host starts first and waits for the others:

```sh
weemitt/build/studio64/weemitt-studio --rom glover.z64 --host 27064 --players 2
```

The host's machine must accept UDP on that port from the internet. On a home router this means forwarding UDP port
27064 to the host's computer. Give the other players your public IP address.

## Join a game

```sh
weemitt/build/studio64/weemitt-studio --rom glover.z64 --join HOST-ADDRESS:27064
```

The game starts when every player is in. The host is player 1. The window's title shows your player number, how many
frames your game runs ahead of the confirmed ones, and any desyncs. The program also prints a status line every 100
frames.

## How it works

The host confirms every player's input for each frame, a quarter of a second behind its own clock. Every machine
predicts the frames it has no confirmed input for yet, and goes back and runs them again when the confirmed input
differs ([discovery/netcode.md](discovery/netcode.md)). Only inputs cross the network; no game data does. Each machine
checks the hash of its game against the host's every 10 frames.

## What does not work yet

- Every machine shows player 1's camera. Your glove may be off screen.
- There is no sound.
- Only the first hub level. Enemies, collectables and puzzles react only to player 1.
- The other gloves have no color of their own.
- No lobby or relay: the host must forward its port. The lobby at `lobby.slugs.io` comes later.
- A player who leaves ends the game for everyone.
