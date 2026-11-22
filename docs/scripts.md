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
- The machines compare their games now and then. The comparison includes how much memory the script uses and how
  many times it asked for memory. A script that ran differently on one machine shows up as a desync.
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
`type`, `tonumber`, `select`, `pcall`, `error`, `assert`, `setmetatable` and the like), without the parts below.

## What is left out, and why

| Left out | Why |
| --- | --- |
| `math.random`, `math.randomseed` | each machine would get other numbers |
| `math.sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log` | their last digit may differ between Windows and Linux. The game's own will take their place |
| `x ^ y` with an exponent that is not a whole number from -1024 to 1024, or 0.5 | the same reason. Such a power is an error. Written with two plain numbers (`2 ^ 0.3`), it stops the script from starting |
| `print`, `tostring` | `tostring` of a table gives its address, which differs between machines. Use `game.log`; a number joins a text with `..` |
| `string.format` with `%p`, `%a` or `%q`, or with `%s` for anything but a text or a number | an address, or a number in a form that Windows and Linux write differently |
| `string.match`, `gmatch`, `gsub`, and patterns in `string.find` | one search with a pattern can take longer than any limit. `string.find` looks for plain text only |
| `string.pack`, `unpack`, `packsize` | their sizes follow the machine |
| `table.move` | one call can walk a range of any size |
| `table.insert`, `remove`, `sort`, `concat`, `unpack` on a table that has a metatable | such a table can claim any length |
| `__gc` in a metatable | Lua would run it outside the limit on work |
| `load`, `loadfile`, `dofile`, `require`, `string.dump` | a script is one file of source text |
| `collectgarbage` | the game runs Lua's memory by itself |
| `warn`, `io`, `os`, `debug`, `coroutine`, `utf8` | not there |

## Limits

- **Work:** one call into the script may do about two million units of work. A step of Lua is one unit, and a
  call of a function is 50. Each byte of memory the script asks for is one unit, and so is each byte a
  `string.find` searches. The script's first run,
  which also reads its text, may do 50 million.
- **Memory:** a game's script has 2 MB. A request that does not fit is an error ("not enough memory").
- **A stopped script** stays stopped for that game: the game goes on without it, the same on every machine. The log
  says why (`script: ...`). A script stops when an error reaches the game, or when a call uses up its work. `pcall`
  can catch an error, a memory error too. It cannot keep a script going that has used up its work.
- A script with an error in its text does not start.
- Do not let the game's course depend on a number turned into text.

## A first script

    -- Every 50 frames, the host's glove takes a step along x.
    function on_frame()
      local f = game.frame()
      if f > 0 and f % 50 == 0 and game.has_player(0) then
        local x, y, z = game.pos(0)
        game.place(0, x + 5, y, z)
      end
    end
