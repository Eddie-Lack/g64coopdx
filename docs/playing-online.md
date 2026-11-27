# Playing online (prototype)

This is the first online version. Two to sixteen players play together over the internet or a local network. Everyone
starts in the hub, the level with the portals to the game's worlds. The hub has a stage for each crystal in its
fountain (HUB1 to HUB8), and everyone starts in the first. Each player can then go through portals and doors alone:
every player sees the level they're in, with the other players who are in it. Each player sees their own glove's
camera. It's a test build: see "Limits" below.

## What you need

- Your own Glover (USA) ROM. Every player needs the same ROM: the host refuses a different one.
- The online program:
  - Windows: `g64coopdx-windows` from the Actions page of the
    [g64coopdx repository](https://github.com/Eddie-Lack/g64coopdx) (the "Online program" workflow) or from a
    release. Unzip it; `g64coopdx.exe`, `g64coopdx-sound.dll` and `SDL2.dll` stay together.
  - Linux: `g64coopdx-linux.tar.gz` from the same places. Its program is `g64coopdx`, with its sound player
    `g64coopdx-sound.so` next to it. In the repository, `make client` builds the same program under weemitt's name
    for it, `weemitt/build/studio64/weemitt-studio`, with `g64coopdx-sound.so` next to it.
  - Without the sound player file the game plays without sound.
  - Both downloads also hold `g64pack`, the tool that makes level packs ([below](#making-a-pack)), and the folder
    `packs`.

## Start

1. Put your ROM next to the program (`g64coopdx.exe` on Windows, `g64coopdx` on Linux) and name it `glover.z64`.
2. Start the program: double-click `g64coopdx.exe` on Windows, or run `./g64coopdx` on Linux. The start screen opens.
   It's the game's own title menu, with the online choices on its bars (its rows): **HOST GAME**, **JOIN GAME**,
   **OPTIONS** and **SOLO**. **SETTINGS** opens the menu with your settings ([Controls](#controls)), and **QUIT**
   closes the program.
3. To host: open **HOST GAME**, set **PLAYERS** and press **START**. The screen shows a room code such as
   `F3J7WZ`; press **COPY CODE** and send it to the other players. The game starts when everyone is in, or when you
   press **START NOW**. Below the code, the screen shows how many players are in. A joiner's screen says what it
   waits for.
4. To join: open **JOIN GAME**, type the code on the **CODE** bar (Ctrl+V pastes it) and press **JOIN**. A room code
   has no I, O, 0 or 1: the **CODE** bar skips them.

On the start screen:

| Key | Controller | What it does |
| --- | --- | --- |
| Up, Down | D-pad or stick | Chooses a bar |
| Left, Right | D-pad or stick | Changes the value on the bar (the bar shows - and +) |
| Enter | A or Start | Presses the bar |
| Esc | B | Goes back, or cancels while the program connects. A host on the **HOSTING** page is asked first: "Leave?" with **YES** and **NO**. On the first page, Esc opens the menu with **Settings** ([Controls](#controls)) |
| Letters | Left, Right, A and Z | Type on a bar with a blinking mark: a name, a title or a room code. With the controller, Left and Right change the last letter (Left past A takes it away), A adds a letter, and Z takes the last letter away |
| Backspace | | Takes back a letter; on other bars it goes back. It doesn't cancel while the program connects |
| F11 or Alt+Enter | | Switches between a window and full screen, as in the game. The program keeps the choice |

The controller's buttons are the ones you set under Esc, **Settings**, **Controls** ([Controls](#controls)). The
game's letters have capitals only, so names and titles show in capitals. They keep the letters you typed. A name or
a title takes only the letters, digits and marks that the game's letters have: `!`, `'`, `*`, `,`, `-`, `.`, `/`,
`:` and `?`. A **NAME** left empty shows the name the program uses, `PLAYER`.

The number of players is the most the game takes. Players can join a game that has started, while it has a free
place: they come into the game's first level, or the hub's newest stage if it started in the hub. A player who
leaves frees their place for the next one. The host finds the room code again under Esc, **Online game**,
**Players**.

A host can set **LISTED** to **YES** and type a **TITLE** (the **TITLE** bar shows while **LISTED** is **YES**). Every player with the same version
([below](#everyone-needs-the-same-version)) then sees the game on **JOIN GAME**, below the code, with the number of
players in it. Pressing it joins. No code is needed. The game
stays on the list while it has a free place, also after it has started. A star after the number of players marks a
game that has started. The list also shows each game's ping, in milliseconds: the time a message takes to the host
and back, through the relay. The relay measures the host's part. A **TITLE** takes up to 32 letters; the list cuts a
long one at a word, with `...`. Without the list, only players with the code
can join. `--public TITLE` does the same from a command prompt.

The start screen keeps the choices on **HOST GAME** for next time, in `options.txt` in g64coopdx's folder
([below](#how-it-works)). `--players N` on the command line sets **PLAYERS** for that run.

**SOLO** starts the vanilla game at its file select. There, the second file is **CHOSIS**, a complete game: every
level done, every garib, every crystal in the cave. It's the only file with a name of more than 3 letters. Your
glove's and ball's models and colors from **OPTIONS** show in SOLO too.

Solo play keeps its progress. The game saves at the usual times, for example when you leave a level or copy a file.
The save goes to its own file, `eeprom-solo.bin`, in g64coopdx's folder ([below](#how-it-works)). It isn't weemitt's
save, and online games don't touch it. `--save FILE --launcher` uses another file, and its file 2 becomes CHOSIS too.

- Files 1, 3, 4, 5 and 6 are yours. They're there the next time you start SOLO.
- File 2 is CHOSIS again at every SOLO start. What you do with CHOSIS isn't kept. Copy it to another file to keep a
  game that starts from 100%.
- A damaged save file is read as weemitt reads one: a file whose checksum is wrong comes up empty. A short save
  file is read as far as it goes, and the rest is zeros.
- The first time the game saves in a SOLO session, the program keeps the file as it was before, as
  `eeprom-solo.bin.bak`. Put that file back in place of `eeprom-solo.bin` to go back to it.

An online game doesn't read or write this file. It starts from an empty save, which lasts as long as the game.

## From a command prompt

The same, without the start screen:

```sh
g64coopdx.exe --rom glover.z64 --online-host --players 2
g64coopdx.exe --rom glover.z64 --online-join F3J7WZ
```

On Linux, write `./g64coopdx` in place of `g64coopdx.exe`.

The host's program prints the room code (`Room code: F3J7WZ`). The game starts when every player is in. With
`--start-alone` the host starts at once and the others join the running game. The start screen opens unless an
option asks for something else: it opens when the options are only `--rom` and online settings such as `--players`,
`--pack` or `--name`. `--launcher` opens it in any case. The picture options (`--fullscreen`, `--no-vsync`,
`--max-fps`, `--render-scale`, `--msaa`, `--texture-filter`, `--widescreen`), `--skip-intro` and `--log` count as
such settings too. Any other option, such as a cheat, starts weemitt's own game without the start screen. That game
uses weemitt's save file, `eeprom.bin`, not SOLO's.

Nobody forwards a port: every player connects out to the relay, which passes the game's packets on inside the room.
The relay runs on the lobby server, the project's own server on the internet. Windows may ask whether to allow the
program on networks. Allow it.

Each program applies your input 1 frame (50 ms) after you press it, and sends it at once: the others then get it
before they need it, and the other gloves move without corrections. The start screen's **INPUT DELAY** (or
`--input-delay N`) changes it, from 0 to 8 frames. A higher value helps when the other gloves still jump to a
corrected place; a lower one makes your own glove answer sooner. Each player chooses their own. It's under
**OPTIONS**, and the program keeps it for next time.

Your glove's and ball's colors are under **OPTIONS**, **COLORS**: **AUTOMATIC** (the color of your player number),
**CLASSIC** (the game's own), **CROSS-STITCH**, a few plain colors, or **CUSTOM**. CROSS-STITCH makes your glove
Cross-Stitch, the evil glove, in his own colors. With CUSTOM, **GLOVE COLOR** and **BALL COLOR** each step through a
list of colors. A color tints the glove's and the ball's own, so white keeps them as they are.

**GLOVE MODEL** and **BALL MODEL** choose the models of your glove and your ball, each on its own:

| Bar | Models |
| --- | --- |
| GLOVE MODEL | GLOVER (the game's own), CROSS-STITCH, WEEVIL, FISH, BUTTERFLY |
| BALL MODEL | BALL (the game's own), WEEVIL, FISH, BUTTERFLY |

They come from the game's GENERIC bank, which every level has. Nothing more is loaded. A model only changes the
picture: the game itself still has Glover and his ball. The glove's model takes your moves as well as it can: it
walks when you walk, and stands when you stand. When the frog spell turns you into a frog, you're the game's frog.
Your ball keeps the game's look while a cheat has made it an enemy.

A glove and a ball at the right of the start screen show your choice. Every player sees everyone's choice: only the
numbers of your models and colors go over the network, and each program draws the models from its own ROM. The start
screen keeps your choice for next time.

The host is player 1. With **Automatic** colors, player 1's glove keeps the game's colors; player 2's is blue,
player 3's green and player 4's yellow. Player 5's is red, player 6's purple, player 7's orange and player 8's cyan.
Players 9 and 10 are blue, and players 11 to 16 have the colors of players 3 to 8.

Everyone plays with the easy ball controls. The host can set **BALL CONTROLS** to **NORMAL** on **HOST GAME** (or add
`--normal-ball`) for the game's own controls; every player then has them.

The session is in one world at a time. When a player goes through a portal to a world, the hub's portals to the
other worlds are shut for everyone, and the game tells you so when you walk into one. They open again when every
player is back in the hub. The portal to the world the others are in stays open. The host can set **Worlds** to any (or
add `--any-world`) to play without this rule.

A glove takes only its own ball. The host can set **Balls** to shared (or add `--any-ball`): when your glove touches
another player's free ball, it takes that ball, and that player gets the ball you left. Either way, a ball that is
lost kills the player it first belonged to, whoever has it.

For players far apart, the host can add `--deadline 8` (or more). It gives inputs more time to reach the host: each
step is 50 ms, and the default is 5 (250 ms). A higher value means fewer jumps of the other gloves, but the confirmed
game ([How it works](#how-it-works)) lags further behind.

An address holds at most 4 rooms at the relay at once. A fifth host from the same address gets "your address has 4
open games". A room that the host cancels or leaves closes at once.

If the program says "no answer from the relay", open https://g64coopdx-lobby.fly.dev/v1/info in a browser. A line
such as `"relay":{"port":27065,"rooms":1,"players":2}` shows that the relay runs and how many rooms and players it
holds. The program talks to the relay over UDP, on port 27065. The page opens over the web (TCP), so it can open
while a firewall or a network blocks UDP. Then the game can't reach the relay: allow the program through the
firewall, or try another network.

The window stays open while a game from a command prompt waits for its players. Closing it, or Ctrl+C in the
command prompt, stops the wait.

### Without the relay

Players on one network, or a host who forwards UDP port 27064 on their router, can skip the relay:

```sh
g64coopdx.exe --rom glover.z64 --host 27064 --players 2
g64coopdx.exe --rom glover.z64 --join HOST-ADDRESS:27064
```

`--relay HOST:PORT` uses another relay than the lobby's (`g64coopdx-lobby.fly.dev:27065`); anyone can run the lobby
server (the folder `server/` of the repository).

## A custom level

A level pack is a folder with a custom level that takes the place of one of the game's levels
([discovery/level-pack.md](discovery/level-pack.md)). A pack may take the place of any level: a hub, a boss level, a
bonus level or a scene too. The host starts the program with `--pack FOLDER`. The level shows when you enter the
level it replaces. A pack may also hold a script ([scripts.md](scripts.md)).

On the start screen, **HOST GAME** has a **PACK** bar: Left and Right step through the packs the program finds and
none (`-`). It finds each folder with a `pack.txt` in `packs` next to the program, in `packs` in the folder you
started it in, and the packs it kept from hosts. A pack that doesn't load says why and isn't chosen. The start
screen keeps the choice for next time. HOST GAME has more bars than the screen shows at once: Up and Down move past
the first or the last to show the others.

To try a pack alone, choose it on **PACK** and press **SOLO** below **START**, or start the program with
`--try-pack FOLDER`. The game starts at once in the pack's level, hosted on your own computer without the relay.
Nobody else can join it.

The game comes with one pack to try: the folder `packs/garib-island`, in the download and in the repository. It's a
small level of its own in the place of Atlantis 1: a sand island with a row of 11 garibs, three boxes, two checkpoints
and the exit. You land on a wooden raft that hovers over the sand and moves to and fro at the start of the row. A
second raft sits in the sea, and a bell from Carnival stands beside the row. The rafts are the pack's own model. The
bell comes from your own ROM. The game reads its models from banks, which are blocks of the ROM, and the pack names
the bell's banks, not the model. Its script counts each player's garibs on the screen, gives a life to a player who
finds all 11, and says who reaches the exit first. To play it, host with `--pack packs/garib-island`; the game starts
in the hub, and the level is behind Atlantis 1's door: go through the hub's portal to Atlantis, and in its way room
take the door to Atlantis 1. Its source for Glovebox, the level editor, is `level.xml` in the same folder.

A player who joins without the pack can download it from the host:

- On the start screen, the program shows the pack's name and size and asks: **DOWNLOAD AND PLAY** or **LEAVE**.
  Nothing is downloaded before you say yes. If you say no, you leave the game.
- A pack with models also shows the size of its models. Models make a pack larger: they may be up to 4 MB.
- How long a download takes depends on the time a message needs to go to the host and back. The program gets about
  8 KB each time. Over the internet that's often about 100 ms, and then 1 MB takes about 13 seconds. On a home
  network 1 MB takes about 5 seconds. It takes longer when several players download at once.
- The host keeps your place while you download: five minutes for a pack of up to about 1.9 MB. For a larger pack it
  waits longer, up to about 12.5 minutes.
- With options on the command line nobody can be asked. Add `--accept-pack` to download the host's pack. Without
  it, the program ends with a message that names the pack.
- The program checks a downloaded pack before use. The check doesn't catch everything a bad level file can do
  ([discovery/level-file-safety.md](discovery/level-file-safety.md)). Take a pack only from a host you trust.
- The program keeps a downloaded pack in the folder `packs` next to its settings (`%APPDATA%\g64coopdx\Glover\packs`
  on Windows, `~/.local/share/g64coopdx/Glover/packs` on Linux). The next time, it asks and then uses the kept copy.
  Each kept pack has a folder named by its id, such as `packs/f98693a92e74c8a301d8b0a269f0febb`. `g64pack check` on
  that folder prints the pack's name. To host a pack you received, use that folder with `--pack`.
- A player who has the folder can still start with `--pack FOLDER`, and nothing is downloaded.
- A player who starts with a pack and joins a host without one plays without it.

The host doesn't send a pack in two cases, and a player then needs the folder. The first: the pack's level file
is an unchanged level of the game. That is game data (data from the ROM), which never travels. The second: the
pack's models don't pass the check on the host's ROM. The host's log says which case it was. A pack whose level file
doesn't pass the check of its structure isn't played at all: the program doesn't start with it, from a folder or
from a download. A pack whose level names an object that is in none of the level's banks is never sent: the
host's session stops before it starts. `g64pack check FOLDER --rom ROM` says whether a pack can be downloaded
(`g64pack` is the pack tool, [below](#making-a-pack)). Without `--rom` it can't check the first and the third case,
and it says so.

### Making a pack

The program `g64pack` comes with the game. In the repository, `make packtool` builds it as `build/g64pack`. It makes a
pack from a level you edited in Glovebox. Run it from a command prompt in the game's folder.

On Linux, write `./g64pack` in place of `g64pack` in the commands below.

1. In Glovebox, open the level and edit it.
2. Get the level out of Glovebox, one of two ways:
   - press the `.lev` button, which saves the level as a `.lev` file
   - or build a ROM with your level in it
3. Find the number of the level you edited: `g64pack levels` lists them. Atlantis 1, for example, is 10.
4. Make the pack:

       g64pack make MyLevel --name "My level" --level 10 --lev mylevel.lev
       g64pack make MyLevel --name "My level" --level 10 --rom built.z64

   Add `--script script.lua` for a pack with a script. `--rom` takes a ROM that Glovebox built: `g64pack` refuses
   the retail ROM, whose levels are game data.
5. Host with `--pack MyLevel`. The other players download the pack when they join, or start with the same folder.

`g64pack make` and `g64pack check` compile the pack's script as the game does, and end with Lua's message when it
doesn't compile.

`g64pack check MyLevel` reads a pack as the game does and prints its id. Two players have the same pack when their
ids are the same. `g64pack check MyLevel --rom ROM` also checks it against your ROM, as the host does before it sends
a pack. `g64pack check` also prints a line on the level file's structure. A warning there names a part of the file
that the game may read wrongly. The program doesn't start with such a pack, and `g64pack` ends with an error.

To share a pack yourself, send the whole folder: `pack.txt`, `level.lev`, and `script.lua` and `models.bin` when it
has them. `g64pack make` writes `pack.txt`. Don't edit it by hand: the pack's id comes from the files' exact bytes,
and the reader takes only the lines that `g64pack` writes (an editor's Windows line ends, for example, make it fail).
Change a name with `g64pack make`. A folder from someone else is checked as a download is, but the check doesn't
catch everything: take a folder only from someone you trust.

The game also checks a level as it reads it. If a pack's level names something the game doesn't have, the level
can't be played: the game tells you, and sends everyone in it back to the hub.

A pack's level may use models you imported into Glovebox, and objects of another level. The pack holds the
imported models in its `models.bin`. For objects of another level, it names that level's banks, and each player's
game loads them from their own ROM. A pack names at most 4 texture banks and 4 object banks of the game. Glovebox's
**Export level pack** button, in its **Export patch** dialog, writes the pack, with a `models.bin` when the level
needs one. With `--models FILE`, `g64pack make` adds a `models.bin` that Glovebox wrote.

A pack can't yet hold a level that uses what Glovebox's game mod adds: rotation and scale on enemies, platforms,
pick-ups and water, and the extra level slots. Glovebox says when a level needs the mod.

## Names, the player list and chat

Type your name under **OPTIONS**, **NAME** (or add `--name NAME`). It can have 16 letters, and the program keeps it for
next time, also a name given with `--name`.

Each other player's name shows over their glove. **NAME TAGS** under **OPTIONS** turns it off (**NO**) or on
(**YES**). The tags only change your own picture.

In a game, press Esc and open **Online game**:

- **Players** lists everyone by player number and name, with the level each player is in, by the names that
  `g64pack levels` prints. It shows the room code to every player, with **Copy**: anyone can invite a friend. The
  host has a **Remove** button beside each other player.
  It asks once more before it removes them. A removed player's program says so, and the game goes on for the rest.
  The reason shows in the removed player's picture, which stops there, and in their log. Leave the online game
  (Esc, **Online game**) to close the program.
- **Chat** has the lines everyone wrote and a line to type in, which has the keyboard when the window opens. Press
  Enter or **Send**.

The chat key, Y, opens a line to type in over the picture, without the menu. Enter sends it and closes it; Esc
closes it. If you give Y to an input under **Controls**, the key is that input's.

A new chat line also shows at the bottom left of the picture for a few seconds. Notices, such as a player who left,
show there too. When another player goes to another level, a notice says where, such as "ALICE went to Atlantis 1",
by the names that `g64pack levels` prints.

A player who typed no name is "Player" and their number, such as "Player 2", in the list, the chat and the name tags.

A removed player can't come back: their address is refused for that game. Another player at the same address,
such as someone in the same household, is refused too.

## Everyone needs the same version

Every version of the program plays a slightly different game, and two machines must run the same one. The log of an
online game names your build: the commit the program was built from, as a short code such as `3f5352f67584`. A host
refuses a player whose build is another: that player sees "the host has another version". Download the same release
as the host.

## Controls

A gamepad works as an N64 controller. On the mouse and keyboard:

| Mouse or key | N64 | What it does |
| --- | --- | --- |
| Mouse | | Turns the free camera round your glove; with **FREE CAMERA** off, left and right turn the game's camera with C-left and C-right |
| Left click | A, or B with the ball | Jumps; with the ball, bounces it |
| Right click | B, or A with the ball | Points; with the ball, lobs it or slaps it |
| W A S D or arrow keys | Stick | Moves; a diagonal is as fast as a straight move |
| Left Shift or Z | Z | |
| Space or Q | L | |
| R or E | R | |
| C | A | For menus and tips |
| X | B | For menus and tips |
| I, K, J, L | C buttons | |
| T, G, F, H | D-pad up, down, left and right | |
| Enter | Start | |
| Esc | | The menu |
| Y | | The chat's line, in an online game |
| F11 or Alt+Enter | | Switches between a window and full screen, as **Full screen** in Esc, **Settings**, **Graphics** does. The program keeps the choice |

Under Esc, **Settings**, **Controls** you can change every key. A key belongs to one input at a time: when you give
it to another input, the first one loses it, and shows `(none)` if it had no other key.

The master volume (Esc, **Settings**, **Audio**) starts at 20%. That's a part of the game's own level. Raise it if
the game is too quiet.

The start screen plays the game's menu theme. **Menu music** (Esc, **Settings**, **Audio**, or **MENU MUSIC** under
**OPTIONS**) chooses how:

| Choice | What plays |
| --- | --- |
| Remix (the default) | The menu theme, remixed: a little faster, a whole step higher, the two lead instruments trading their parts, a kick, a snare and a hat added to the drums, and more reverb |
| Original | The menu theme as the game plays it |
| Off | Nothing |

The remix is made as it plays, from the music in your own ROM. It follows the master and the music volume. It stops
when a game or SOLO starts, and there's none with `--no-sound`.

The window holds the mouse while you play: the pointer's hidden and only its motion counts. It lets go while the
Esc menu or your pause menu is open, and when you switch to another window. When you play alone, it also lets go on
the game's title screen and its menus.

In an online game the mouse turns a free camera, your own. It goes all the way round your glove, as far from it as
the game's camera is, and stays where you turn it. Mouse up and down raise and lower it. **INVERT Y** under
**OPTIONS** swaps up and down. The stick and the d-pad move your glove from the free camera's point of view: up moves
it away from the camera. C-left and C-right turn the free camera too. The game's own camera shows in C-up's
look-around mode, in the game's scenes and while a level points the camera at something. Then the mouse turns the
game's camera, as with **FREE CAMERA** off. The free camera has no walls: it can stand inside one.

The game's own camera goes on underneath, the same on every machine, and the game uses it for everything but your
picture. Things that the game leaves out of its own view, such as faraway enemies, may stand still in your picture
when the free camera looks their way.

**FREE CAMERA** under **OPTIONS** turns it off (**NO**) or on (**YES**, the default). Off, the mouse turns the game's
camera with the C buttons, as a controller does. Then the camera keeps the game's own rules: walls and floors stop
it, and it turns no faster than a held C button turns it. Moving the mouse up or down does nothing. When you play
alone (SOLO), the mouse always works this way.

**SENSITIVITY** under **OPTIONS** sets how far the camera turns for the same motion, from 1 to 40 (10 at first).
The program keeps these choices with your other settings.

Each click gives the button the game expects for what your glove does when the click starts:

- Without the ball, a left click is A: it jumps. A right click is B: hold it to point, or click near your ball to
  grab it.
- With the ball, a left click is B: a quick click bounces the ball, and a held click aims a throw that goes when
  you let go.
- With the ball, a right click is A. The game lobs the ball for a short A and winds up a slap for a long one. A
  right click that you let go within 4 frames (a fifth of a second) gives a short A: a lob. One that you hold
  longer gives A until you let go, and at least 4 frames of it: the glove winds up and slaps when you let go.
- While you read a tip, a left click is A and a right click is B.

Your mouse sends nothing of its own over the network. It only presses your controller's buttons. The others get them
as your normal input.

If you played before this version, your settings file keeps your old keys: Space stays A, and R does nothing. Esc,
**Settings**, **Controls**, **Keyboard**, **Reset to defaults** gives you the new keys.

Esc opens the menu at the top of the window, as in weemitt, the PC port of Glover that g64coopdx builds on.
**Settings** has **Graphics**, **Audio**, **Controls** and **Player**: in Controls you set your keys and your
controller's buttons, also in the middle of an online game. **Player** has your choices from **OPTIONS**, also in the
middle of a game: **Free camera**, **Invert Y**, **Mouse sensitivity**, **Glove model**, **Ball model**, **Colors**
(with **Glove color** and **Ball color** for CUSTOM), **Name tags** and **Name**. A name changes when you leave its
box. The program keeps each change, as the start screen does. In an online game your new models, colors and name
reach everyone: the models and colors in the same game frame on every machine. When you play alone, **Player** has
**Mouse sensitivity** only. **INPUT DELAY** changes only on the start screen. **Online game** has the pause menu's choices
([below](#your-pause-menu)) for the mouse. The game goes on while the menu is open, also when you play alone. Your
keys and your controller still play it. While you type in a box of the menu, such as the chat's line, your keys go to
the box.

The menu has no cheats, enhancements, developer tools or studio tools, online or alone. When you play alone, it has
**Settings** and **Log** only. In an online game it also has **Online game**. The keys that pause or step the game do
nothing.

## Your pause menu

Start (Enter on the keyboard) opens your own pause menu. It doesn't stop the game: the others play on, and your
glove waits. An enemy can still hurt your glove while it waits, and so can anything else in the level. The menu, the chat's lines over the picture and a script's text are drawn by the game, in its own
letters and panes.

| Choice | What it does |
| --- | --- |
| Resume | Closes the menu |
| Back to checkpoint | Puts your glove and ball at your last checkpoint, or at the level's start |
| Back to the hub | Takes you out of the level, the same way the hub exit does |
| Leave the online game | Asks "Leave?" with Yes and No, and then closes the program; the others play on |

Up and down choose, A picks, and B or Start closes it. The choices act for you alone. The Esc menu has the same
choices under **Online game**.

## Tips

Press B at a tip to read it. Reading doesn't stop the game: the others play on, and your glove waits. Nothing
hurts you while you read. A turns the page, and on the last page it closes the tip. The tips that the game opens by
itself open for every player in the level, and each player closes their own.

## How it works

The host confirms every player's input for each frame, a quarter of a second behind its own clock. Every machine
predicts the frames it has no confirmed input for yet, and goes back and runs them again when the confirmed input
differs ([discovery/netcode.md](discovery/netcode.md)). Only inputs cross the network; no game data does. Each machine
checks the hash of its game against the host's every 10 frames. A hash that differs from the host's is a desync.

The window's title starts with g64coopdx, as the program's error boxes do. In a game it shows your player number, how many frames your game runs ahead of the confirmed ones, and any
desyncs. The program also prints a status line every 100 frames.

The messages between you and the host are encrypted: chat, names and inputs can't be read on the way, also not by
the relay. The room code and the version of your program aren't secret.

Each level that has players in it runs as its own game, an instance. Your machine runs only the instance you're in.
The host runs a copy of every instance. When you take an exit, you leave your instance alone. The host puts you in
the instance of the level the exit leads to, or starts a new one. To join an instance that's already running, your
machine builds the level from its start and runs the inputs played in it so far; that takes a moment, and the picture
stops meanwhile. The window's title shows the level number you're in.

You hear your own game: the music of your level, and the sounds near your own glove's camera, wherever the other
players are. Another player's own clicks (their camera, their pause) are theirs alone. The master, music and effects
volumes in Settings work as in weemitt. When the network corrects a wrong guess about another player, a sound it had
started stops, and a sound it had missed plays, up to half a second late.

g64coopdx keeps its own folder for your settings, saves and logs: `%APPDATA%\g64coopdx\Glover` on Windows,
`~/.local/share/g64coopdx/Glover` on Linux. On its
first start it copies weemitt's settings, if there are any, so your keys and picture settings carry over. In an
online game, your own settings that change the game don't apply: the cheats are off, the camera follows at the
original game's speed, and the ball controls are the host's choice ([above](#from-a-command-prompt)). Every machine
must run the same game.

Each program writes a log of the online game, `g64coopdx-host.log` or `g64coopdx-join.log`, in that folder. It has a status line every 100 frames. On the host, it also lists where the
host's own game and its copy of the same level differ, when they do: send it with a report of desyncs. It holds no
game data, only addresses and counts.

## Limits

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
- An exit takes you at once: there's no spin, fade or end-of-level screen.
- Progress made by another player reaches your level at once, but only some things show it before the level starts
  again. A way room door opens at once. The pedestals in a way room move only at its next start.
- When someone puts a crystal in the hub's fountain, you stay in your hub stage. Your next exit to the hub takes you
  to the newest stage.
- The way back from the crystal cave leads to the hub stage you came from, where the friends you left still are. As
  in the game, the first visit to the cave counts as a crystal, so the hub moves on to its next stage. When nobody's
  left in the stage you came from, the way back leads to the newest stage. Your next exit to the hub from a world or a
  level takes you to the newest stage.
- In the crystal cave, each player who comes with a crystal due has their own flight to the fountain: only their
  glove waits. A player who came with nothing due has no flight, even if a crystal becomes due while they're there.
  If another player puts the last crystal due in first, there's no flight left for you.
- A player who comes into a running level starts a glove's width from where the level put the first player, when
  there's floor there at the same height. Otherwise they start at the same spot.
- Online, the counter where the game shows your lives shows your deaths in this level instead: a death takes no
  life.
- A joiner who leaves vanishes; the game goes on for the others, and someone else can take the place. If the host
  leaves, the game ends: each player's picture stops, with the reason in it, until they leave the online game.
- A player who joins a running game waits while their program runs the level's inputs so far. That takes longer
  the longer the level has run.
