# Playing online (prototype)

This is the first online version. Two to sixteen players play together over the internet or a local network. Everyone
starts in the first hub section, and each player can then go through doors alone: every player sees the level they
are in, with the other players who are in it. It is a test build: see "What does not work yet" below.

## What you need

- Your own Glover (USA) ROM. Every player needs the same ROM: the host refuses a different one.
- The online program:
  - Windows: `g64coopdx-windows` from the repository's Actions page (the "Online program" workflow) or from a
    release. Unzip it; `g64coopdx.exe`, `g64coopdx-sound.dll` and `SDL2.dll` stay together.
  - Linux: `g64coopdx-linux.tar.gz` from the same places, or `make client`, which builds
    `weemitt/build/studio64/weemitt-studio` and its sound player `g64coopdx-sound.so` next to it.
  - Without the sound player file the game plays without sound.

## Start

1. Put your ROM next to `g64coopdx.exe` and name it `glover.z64`.
2. Double-click `g64coopdx.exe`. The start screen opens. It is the game's own title menu, with the online choices on
   its bars: **Host a game**, **Join a game**, **Your options** and **Play alone**.
3. To host: open **Host a game**, set **Players** and press **Start hosting**. The screen shows a room code such as
   `F3J7WZ`; press **Copy code** and send it to the other players. The game starts when everyone is in, or when you
   press **Start now**.
4. To join: open **Join a game**, type the code on the **Code** bar (Ctrl+V pastes it) and press **Join**.

On the start screen:

| Key | Controller | What it does |
| --- | --- | --- |
| Up, Down | D-pad or stick | Chooses a bar |
| Left, Right | D-pad or stick | Changes the value on the bar (the bar shows - and +) |
| Enter | A or Start | Presses the bar |
| Esc | B | Goes back, or cancels while the program connects |
| Letters | | Type on a bar with a blinking mark: a name, a title or a room code |
| Backspace | | Takes back a letter; on other bars it goes back |

The controller's buttons are the ones you set under Esc, **Settings**, **Controls**. The game's letters have capitals
only, so names and titles show in capitals. They keep the letters you typed.

The number of players is the most the game takes. Players can join a game that has started, while it has a free
place: they come into the game's first level, or the hub's newest stage if it started in the hub. A player who
leaves frees their place for the next one. The host finds the room code again under Esc, **Online game**,
**Players**.

A host can set **In the list** to yes and type a **Title**. Every player with the same version then sees the game
on **Join a game**, below the code, with the number of players in it. Pressing it joins. No code is needed. The game
stays on the list while it has a free place, also after it has started. Without it, only players with the code can
join. `--public TITLE` does the same from a command prompt.

**Play alone** starts the normal game.

## From a command prompt

The same, without the start screen:

```sh
g64coopdx.exe --rom glover.z64 --online-host --players 2
g64coopdx.exe --rom glover.z64 --online-join F3J7WZ
```

The host's program prints the room code (`Room code: F3J7WZ`). With `--start-alone` the host starts at once and
the others join the running game. The start screen opens unless an option asks for
something else; `--launcher` opens it in any case.

Nobody forwards a port: every player connects out to the relay on the lobby server, which passes the game's packets
on inside the room. Windows may ask whether to allow the program on networks. Allow it.

Each program applies your input 1 frame (50 ms) after you press it, and sends it at once: the others then get it
before they need it, and the other gloves move without corrections. The start screen's **Input delay** (or
`--input-delay N`) changes it, from 0 to 8 frames. A higher value helps when the other gloves still jump; a lower one
makes your own glove answer sooner. Each player chooses their own. It is under **Your options**.

Your glove's and ball's colors are under **Your options**, **Colors**: **Automatic** (the color of your player
number), **Classic** (the game's own), **Cross-Stitch**, a few plain colors, or **Custom**. With Custom, **Glove** and
**Ball** each step through a list of colors. A color tints the glove's and the ball's own, so white keeps them as they
are. A glove and a ball at the right of the start screen show your choice. Every player sees everyone's choice. The
start screen keeps yours for next time.

Everyone plays with the easy ball controls. The host can set **Ball** to normal on **Host a game** (or add
`--normal-ball`) for the game's own controls; every player then has them.

The session is in one world at a time. When a player goes through a portal to a world, the hub's portals to the
other worlds are shut for everyone, and the game tells you so when you walk into one. They open again when every
player is back in the hub. The portal to the world the others are in stays open. The host can set **Worlds** to any (or
add `--any-world`) to play without this rule.

A glove takes only its own ball. The host can set **Balls** to shared (or add `--any-ball`): a glove
that touches another player's free ball then takes it, and that player gets the ball you left. A ball that is lost
still kills the player it first belonged to, whoever has it.

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

The game starts when every player is in. The host is player 1. Each player sees their own glove's camera. With
**Automatic** colors, player 1's glove keeps the game's colors; player 2's is blue, player 3's green and player 4's
yellow. The
window's title shows your player number, how many frames your game runs ahead of the confirmed ones, and any desyncs.
The program also prints a status line every 100 frames.

## A custom level

A level pack is a folder with a custom level that takes the place of one of the game's levels
([discovery/level-pack.md](discovery/level-pack.md)). The host starts the program with `--pack FOLDER`. The level
shows when you enter the level it replaces. A pack may also hold a script ([scripts.md](scripts.md)).

The game comes with one pack to try: `packs/garib-island`. It is a small level of its own in the place of Atlantis 1:
a sand island with a row of 11 garibs, three boxes, a checkpoint and the exit. Its script counts each player's garibs
on the screen, gives a life to a player who finds all 11, and says who reaches the exit first. To play it, host with `--pack packs/garib-island`; the game
starts in the hub, and the level is behind Atlantis 1's door. Its Glovebox source is `level.xml` in the same folder.

A player who joins without the pack can download it from the host:

- On the start screen, the program shows the pack's name and size and asks: **Download and play** or **No, leave**.
  Nothing is downloaded before you say yes. If you say no, you leave the game.
- A pack with models also shows the size of its models. Models make a pack larger: they may be up to 4 MB.
- How long a download takes depends on the time a message needs to go to the host and back. The program gets about
  8 KB each time. Over the internet that is often about 100 ms, and then 1 MB takes about 13 seconds. On a home
  network 1 MB takes about 5 seconds. It takes longer when several players download at once.
- The host keeps your place while you download: five minutes, or longer for a large pack.
- With options on the command line nobody can be asked. Add `--accept-pack` to download the host's pack. Without
  it, the program ends with a message that names the pack.
- The program checks a downloaded pack before use. The check does not catch everything a bad level file can do
  ([discovery/level-file-safety.md](discovery/level-file-safety.md)). Take a pack only from a host you trust.
- The program keeps a downloaded pack in the folder `packs` next to its settings. The next time, it asks and then
  uses the kept copy.
- A player who has the folder can still start with `--pack FOLDER`, and nothing is downloaded.
- A player who starts with a pack and joins a host without one plays without it.

The host does not send a pack in three cases, and a player then needs the folder. The first: the pack's level file
is an unchanged level of the game. That is game data, which never travels. The second: the level file does not pass
the check. The third: the pack's models do not pass the check on the host's ROM. The host's log says which case it
was. A pack whose level names an object that is in none of the level's banks is never sent: the host's session
stops before it starts. A pack may take the place of any level: a hub, a boss level, a bonus level or a scene too.
`g64pack check FOLDER --rom ROM` says whether a pack can be downloaded. Without `--rom` it cannot check the first and
the third case, and it says so.

### Making a pack

The program `g64pack` comes with the game. It makes a pack from a level you edited in Glovebox, the level editor.
Run it from a command prompt in the game's folder.

1. In Glovebox, open the level and edit it.
2. Get the level out of Glovebox, one of two ways:
   - press the `.lev` button, which saves the level as a `.lev` file
   - or build a ROM with your level in it
3. Find the number of the level you edited: `g64pack levels` lists them.
4. Make the pack:

       g64pack make MyLevel --name "My level" --level 10 --lev mylevel.lev
       g64pack make MyLevel --name "My level" --level 10 --rom built.z64

   Add `--script script.lua` for a pack with a script.
5. Host with `--pack MyLevel`. The other players download the pack when they join, or start with the same folder.

`g64pack check MyLevel` reads a pack as the game does and prints its id. `g64pack check MyLevel --rom ROM` also
checks it against your ROM, as the host does before it sends a pack. Two players have the same pack when their
ids are the same. It also prints a line on the level file's structure. A warning there names a part of the file
that the game may read wrongly.

If a pack's level names something the game does not have, the level cannot be played: the game tells you, and
sends everyone in it back to the hub.

What a pack cannot hold yet:

- A level that uses what Glovebox's game mod adds: rotation and scale on enemies, platforms and pick-ups, more
  bank slots, and the extra level slots. Glovebox says when a level needs the mod.
- Models you imported into Glovebox.
- Objects of another level. A pack's level uses the objects of the level whose place it takes.

## Names, the player list and chat

Type your name under **Your options**, **Name** (or add `--name NAME`). It can have 16 letters, and the program keeps it for
next time.

In a game, press Esc and open **Online game**:

- **Players** lists everyone by player number and name. The host has a **Remove** button beside each other player.
  It asks once more before it removes them. A removed player's program says so, and the game goes on for the rest.
- **Chat** has the lines everyone wrote and a line to type in. Press Enter or **Send**.

A new chat line also shows at the bottom left of the picture for a few seconds. Notices, such as a player who left,
show there too.

A removed player cannot come back: their address is refused for that game. Another player at the same address,
such as someone in the same household, is refused too.

## Everyone needs the same version

Every version of the program plays a slightly different game, and two machines must run the same one. The start
screen shows your build (a short code such as `3f5352f67584`). A host refuses a player whose build is another:
that player sees "the host has another version". Download the same release as the host.

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
| Esc | The menu |

Esc opens the menu at the top of the window, as in weemitt. **Settings** has **Graphics**, **Audio** and
**Controls**: in Controls you set your keys and your controller's buttons, also in the middle of an online game.
**Online game** has the pause menu's choices for the mouse. The game goes on for everyone while the menu is open,
and your keyboard goes to the menu.

The menu has no cheats, enhancements, developer tools or studio tools, online or alone. Only the **Log** window is
there besides the settings. The keys that pause or step the game do nothing.

## Your pause menu

Start (Enter on the keyboard) opens your own pause menu. It does not stop the game: the others play on, and your
glove waits. The menu, the chat's lines over the picture and a script's text are drawn by the game, in its own
letters and panes.

| Choice | What it does |
| --- | --- |
| Resume | Closes the menu |
| Back to checkpoint | Puts your glove and ball at your last checkpoint, or at the level's start |
| Back to the hub | Takes you out of the level, the same way the hub exit does |
| Leave the online game | Closes the program; the others play on |

Up and down choose, A picks, and B or Start closes it. The choices act for you alone. The Esc menu has the same
choices under **Online game**.

## Tips

Press B at a tip to read it. Reading does not stop the game: the others play on, and your glove waits. Nothing
hurts you while you read. A turns the page, and on the last page it closes the tip. The tips that the game opens by
itself open for every player in the level, and each player closes their own.

## How it works

The host confirms every player's input for each frame, a quarter of a second behind its own clock. Every machine
predicts the frames it has no confirmed input for yet, and goes back and runs them again when the confirmed input
differs ([discovery/netcode.md](discovery/netcode.md)). Only inputs cross the network; no game data does. Each machine
checks the hash of its game against the host's every 10 frames.

The messages between you and the host are encrypted: chat, names and inputs cannot be read on the way, also not by
the relay. The room code and the version of your program are not secret.

Each level that has players in it runs as its own game, an instance. Your machine runs only the instance you are in.
The host runs a copy of every instance. When you take an exit, you leave your instance alone. The host puts you in
the instance of the level the exit leads to, or starts a new one. To join an instance that is already running, your
machine builds the level from its start and runs the inputs played in it so far; that takes a moment, and the picture
stops meanwhile. The window's title shows the level number you are in.

You hear your own game: the music of your level, and the sounds near your own glove's camera, wherever the other
players are. Another player's own clicks (their camera, their pause) are theirs alone. The master, music and effects
volumes in Settings work as in weemitt. When the network corrects a wrong guess about another player, a sound it had
started stops, and a sound it had missed plays, up to half a second late.

g64coopdx keeps its own folder for your settings, saves and logs: `%APPDATA%\g64coopdx\Glover` on Windows. On its
first start it copies weemitt's settings, so your keys and picture settings carry over. In an online game, the
settings that change the game (cheats, the easy ball controls, the camera) are always off: every machine must run the
same game.

Each program writes a log of the online game, `g64coopdx-host.log` or `g64coopdx-join.log`, in that folder. It has a status line every 100 frames. On the host, it also lists where the
host's own game and its copy of the same level differ, when they do: send it with a report of desyncs. It holds no
game data, only addresses and counts.

## What does not work yet

- Every boss sees every player. In the last space boss's level each player has a turret of their own, and the
  boss goes for the players in turn. The shield's glow and the crosshair's fade on screen are the first player's.
- In the Fortress, Prehistoric and Atlantis bonus levels you win or lose alone, and the others play on. When the
  clock runs out, the level ends for everyone. In the Carnival bonus level every player sits on the one cannon with
  an aim of their own: your stick turns your aim, your A fires along it, and in your picture the cannon points where
  you aim. The targets and the score are everyone's.
- Any player takes a garib or a pick-up. The players of a level share one count of garibs. A power-up is its
  taker's own. A power-up that changes the look of the glove or the ball changes it for every player.
- Any player's glove or ball works a puzzle's switch or zone. Each player has their own camera zones.
- An enemy goes for the nearest player, or the one it holds. A bullet that homes in on a glove keeps to that player.
  A bullet that flies straight can only hit the player nearest to it.
- A cutscene plays for the first player in the level. The other players are held while it plays and see its
  camera. In the control room before the last boss, the other players watch the first player's scene.
- An exit takes you at once: there is no spin, fade or end-of-level screen.
- Progress made by another player reaches your level at once, but only some things show it before the level starts
  again. A way room door opens at once. The pedestals in a way room move only at its next start.
- The hub has a stage for each crystal in the fountain (HUB1 to HUB8). When someone puts a crystal in, you stay in
  your hub stage. Your next exit to the hub takes you to the newest stage.
- In the cave, each player who comes with a crystal due has their own flight to the fountain: only their glove waits.
  A player who came with nothing due has no flight, even if a crystal becomes due while they are there. If another
  player puts the last crystal due in first, there is no flight left for you.
- A player who comes into a running level starts where the level put the first player.
- A joiner who leaves vanishes; the game goes on for the others, and someone else can take the place. If the host
  leaves, the game ends.
- A player who joins a running game waits while their program runs the level's inputs so far. That takes longer
  the longer the level has run.
