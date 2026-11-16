# Playing online (prototype)

This is the first online version. Two to sixteen players play together over the internet or a local network. Everyone
starts in the first hub section, and each player can then go through doors alone: every player sees the level they
are in, with the other players who are in it. It is a test build: see "What does not work yet" below.

## What you need

- Your own Glover (USA) ROM. Every player needs the same ROM: the host refuses a different one.
- The online program:
  - Windows: `g64coopdx-windows` from the repository's Actions page (the "Online program" workflow) or from a
    release. Unzip it; `g64coopdx.exe` and `SDL2.dll` stay together.
  - Linux: `g64coopdx-linux.tar.gz` from the same places, or `make client`, which builds
    `weemitt/build/studio64/weemitt-studio`.

## Start

1. Put your ROM next to `g64coopdx.exe` and name it `glover.z64`.
2. Double-click `g64coopdx.exe`. A start screen opens.
3. To host: choose the number of players and press **Host**. The screen shows a room code such as `F3J7WZ`; press
   **Copy code** and send it to the other players. The game starts when everyone is in.
4. To join: type the code under **Join an online game** and press **Join**.

**Play alone** starts the normal game.

## From a command prompt

The same, without the start screen:

```sh
g64coopdx.exe --rom glover.z64 --online-host --players 2
g64coopdx.exe --rom glover.z64 --online-join F3J7WZ
```

The host's program prints the room code (`Room code: F3J7WZ`). The start screen opens unless an option asks for
something else; `--launcher` opens it in any case.

Nobody forwards a port: every player connects out to the relay on the lobby server, which passes the game's packets
on inside the room. Windows may ask whether to allow the program on networks. Allow it.

Each program applies your input 1 frame (50 ms) after you press it, and sends it at once: the others then get it
before they need it, and the other gloves move without corrections. The start screen's **Input delay** (or
`--input-delay N`) changes it, from 0 to 8 frames. A higher value helps when the other gloves still jump; a lower one
makes your own glove answer sooner. Each player chooses their own.

Everyone plays with the easy ball controls. The host can tick **Normal ball controls** on the start screen (or add
`--normal-ball`) for the game's own controls; every player then has them.

For players far apart, the host can add `--deadline 8` (or more). It gives inputs more time to reach the host: each
step is 50 ms, and the default is 5 (250 ms). A higher value means fewer jumps of the other gloves, but the confirmed
game lags further behind.

If the program says "no answer from the relay", open https://g64coopdx-lobby.fly.dev/v1/info in a browser. A line
such as `"relay":{"port":27065,"rooms":1,"players":2}` shows that the relay runs and how many rooms and players it
holds.

### Without the relay

Players on one network, or a host who forwards UDP port 27064 on their router, can skip the relay:

```sh
g64coopdx.exe --rom glover.z64 --host 27064 --players 2
g64coopdx.exe --rom glover.z64 --join HOST-ADDRESS:27064
```

`--relay HOST:PORT` uses another relay than the lobby's (`g64coopdx-lobby.fly.dev:27065`); anyone can run the lobby
server (`server/`).

The game starts when every player is in. The host is player 1. Each player sees their own glove's camera. Player 1's
glove keeps the game's colors; player 2's is blue, player 3's green and player 4's yellow. The
window's title shows your player number, how many frames your game runs ahead of the confirmed ones, and any desyncs.
The program also prints a status line every 100 frames.

## Controls

A gamepad works as an N64 controller. On the keyboard:

| Key | N64 |
| --- | --- |
| Arrow keys or W A S D | Stick |
| Space | A |
| X | B |
| Z or left Shift | Z |
| Q, E | L, R |
| I, K, J, L | C buttons |
| Enter | Start |

The online game has no menu: its cheats, level jumps and the studio's tools would change one player's game only.

## How it works

The host confirms every player's input for each frame, a quarter of a second behind its own clock. Every machine
predicts the frames it has no confirmed input for yet, and goes back and runs them again when the confirmed input
differs ([discovery/netcode.md](discovery/netcode.md)). Only inputs cross the network; no game data does. Each machine
checks the hash of its game against the host's every 10 frames.

Each level that has players in it runs as its own game, an instance. Your machine runs only the instance you are in.
The host runs a copy of every instance. When you take an exit, you leave your instance alone. The host puts you in
the instance of the level the exit leads to, or starts a new one. To join an instance that is already running, your
machine builds the level from its start and runs the inputs played in it so far; that takes a moment, and the picture
stops meanwhile. The window's title shows the level number you are in.

g64coopdx keeps its own folder for your settings, saves and logs: `%APPDATA%\g64coopdx\Glover` on Windows. On its
first start it copies weemitt's settings, so your keys and picture settings carry over. In an online game, the
settings that change the game (cheats, the easy ball controls, the camera) are always off: every machine must run the
same game.

Each program writes a log of the online game, `g64coopdx-host.log` or `g64coopdx-join.log`, in that folder. It has a status line every 100 frames. On the host, it also lists where the
host's own game and its copy of the same level differ, when they do: send it with a report of desyncs. It holds no
game data, only addresses and counts.

## What does not work yet

- There is no sound.
- Enemies, collectables and puzzles react only to one player in each level.
- An exit takes you at once: there is no spin, fade or end-of-level screen.
- A level that is already running sees new progress only when it starts again. For example, a hub door that a
  finished level opens stays shut while someone is in the hub; it opens once the hub is empty and someone enters it
  again.
- Players can go to different worlds. The planned rule (a portal to another world opens only when everyone is in
  the hub) is not there yet.
- No list of open games: players share the room code themselves.
- A joiner who leaves vanishes; the game goes on for the others. If the host leaves, the game ends.
