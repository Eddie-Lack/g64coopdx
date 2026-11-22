# Scripts

A level pack may hold a script: a file `script.lua` of Lua source text
([discovery/level-pack.md](discovery/level-pack.md)). This page is for someone who writes one. The scripting is a
first version: a script can read a little of the game and move a glove. More comes later.

## How a script runs

- Every game of the session runs the script: one game for each level that has players in it. Each game has its own
  copy of the script's variables. A script in one level does not see another level's.
- The script's text runs once when a game starts, before its first frame.
- If the script has a function `on_frame`, the game calls it once a frame, before the game's own step. The game
  runs 20 frames a second.
- Every machine runs the same script on the same game and must reach the same result. So a script has no clock, no
  random numbers of its own, no files, and no way to load other code.
- The online game sometimes goes back a few frames and runs them again with corrected inputs. The script's
  variables go back with the game. A script does not notice.

## What a script may call

The table `game`:

| Call | What it gives or does |
| --- | --- |
| `game.frame()` | the number of frames this game has run |
| `game.level()` | the level's number, 0 to 47 |
| `game.has_player(p)` | `true` when player `p` is in this level. Players are numbered 0 to 15, the same on every machine; the host is 0 |
| `game.pos(p)` | the place of player `p`'s glove: three numbers `x, y, z`, or nothing when the player is not in this level |
| `game.place(p, x, y, z)` | puts player `p`'s glove at a place. Each coordinate is from -100000 to 100000 |
| `game.log(text)` | writes a line to the program's log. It is for you to read: nothing in the game reads it |

From Lua's own libraries a script has `table`, `string` and `math`, and the basic functions (`pairs`, `ipairs`,
`type`, `tonumber`, `select`, `pcall`, `error`, `assert`, `setmetatable` and the like).

## What is left out, and why

| Left out | Why |
| --- | --- |
| `math.random`, `math.randomseed` | each machine would get other numbers |
| `math.sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log` | their last digit may differ between Windows and Linux. The game's own will take their place |
| `x ^ y` with an exponent that is not a whole number from -1024 to 1024, or 0.5 | the same reason. Such a power is an error |
| `print`, `tostring` | `tostring` of a table gives its address, which differs between machines. Use `game.log`; a number joins a text with `..` |
| `load`, `loadfile`, `dofile`, `require`, `string.dump` | a script is one file of source text |
| `collectgarbage` | the game runs Lua's memory by itself |
| `io`, `os`, `debug`, `coroutine`, `utf8` | not there |

## Limits

- **Work:** one call into the script may run about two million steps of Lua. A script that takes more is stopped.
- **Memory:** every script of a game shares 2 MB. A script that asks for more is stopped.
- **A stopped script** stays stopped for that game: the game goes on without it, the same on every machine. The log
  says why (`script: ...`). A script with an error in its text does not start.
- Do not let the game's course depend on a number turned into text, or on the order of `pairs` over keys that are
  C functions.

## A first script

    -- Every 50 frames, the host's glove takes a step along x.
    function on_frame()
      local f = game.frame()
      if f > 0 and f % 50 == 0 and game.has_player(0) then
        local x, y, z = game.pos(0)
        game.place(0, x + 5, y, z)
      end
    end
