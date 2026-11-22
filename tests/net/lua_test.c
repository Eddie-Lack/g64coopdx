/* The scripts' sandbox, without the game: each script here runs for some frames, and the test prints what the
   script logged, whether it still runs, and the counts of its memory. The Linux and the Windows program must print
   the same, byte for byte (make test-windows). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int G64LuaLoad(const char *source, unsigned int size);
void G64LuaFrame(void);
int G64LuaActive(void);
void G64LuaCounts(unsigned int out[4]);
void G64LuaReset(void);
void G64LuaEvent(int kind, int player);

/* The game's side of the scripts' calls: one player at a place that the script can move. */
static int frames;
static float place[3];

int G64PlayerPosition(int p, float *x, float *y, float *z) {
    if (p != 0) {
        return 0;
    }
    *x = place[0];
    *y = place[1];
    *z = place[2];
    return 1;
}

void G64PlacePlayer(int p, float x, float y, float z) {
    if (p == 0) {
        place[0] = x;
        place[1] = y;
        place[2] = z;
    }
}

/* Player 0 is always there. Player 1 is in the level from frame 5 to frame 7. */
int G64PlayerInLevel(int p) {
    return p == 0 || (p == 1 && frames >= 5 && frames < 8);
}

int G64BallPosition(int p, float *x, float *y, float *z) {
    if (p != 0) {
        return 0;
    }
    *x = place[0] + 1;
    *y = place[1] + 2;
    *z = place[2] + 3;
    return 1;
}

int G64PlayerBallType(int p) {
    return p == 0 ? 2 : -1;
}

int G64PlayerGaribs(int p) {
    return p == 0 ? frames / 10 : -1;
}

/* A hurt takes a point of the stand-in player's health; the health call sets it. */
static int health = 3;

void G64HurtPlayer(int p) {
    if (p == 0 && health > 0) {
        health--;
    }
}

void G64SetPlayerHealth(int p, int n) {
    if (p == 0) {
        health = n;
    }
}

int G64PlayerHealth(int p) {
    return p == 0 ? health : -100;
}

int G64PlayerLives(int p) {
    return p == 0 ? 5 : -100;
}

/* The game's sine and cosine take their constants from the ROM. These stand in for them here; the test with the
   real game compares the real ones between Linux and Windows (tests/rom/instances.c). */
float port_sinf(float x) {
    return x * 0.5f;
}

float port_cosf(float x) {
    return 1.0f - x * 0.25f;
}

int G64FramesRun(void) {
    return frames;
}

int G64Level(void) {
    return 7;
}

void port_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("    ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

void port_dma(void *dst, unsigned int devAddr, unsigned int size) {
    (void)devAddr;
    memset(dst, 0, size);
}

static int failed;

/* Runs a script for a number of frames. want: 1 when the script must still run at the end, 0 when it must have
   been stopped. */
static void run(const char *name, const char *script, int count, int want) {
    unsigned int c[4];
    int loaded, active;
    printf("%s\n", name);
    frames = 0;
    place[0] = place[1] = place[2] = 0;
    loaded = G64LuaLoad(script, (unsigned int)strlen(script)) == 0;
    for (frames = 0; frames < count; frames++) {
        G64LuaFrame();
    }
    active = G64LuaActive();
    G64LuaCounts(c);
    printf("    loaded %d, runs %d, x %.3f, high mark %u, in use %u, allocations %u, frees %u\n", loaded, active,
           place[0], c[0], c[1], c[2], c[3]);
    if (active != want) {
        printf("FAIL %s: runs %d, expected %d\n", name, active, want);
        failed++;
    }
    G64LuaReset();
}

int main(void) {
    run("a script moves the glove",
        "function on_frame()\n"
        "  local x, y, z = game.pos(0)\n"
        "  if game.frame() % 10 == 0 then game.place(0, x + 1.5, y, z) end\n"
        "end\n",
        100, 1);

    run("numbers as text",
        "game.log(1 / 3 .. ' ' .. 0.1 + 0.2 .. ' ' .. 2 ^ 0.5 .. ' ' .. 1e300 * 10 .. ' ' .. -(0 / 0) .. ' ' .. 2 ^ 63)\n"
        "game.log(string.format('%.3f %g %5.1f %d %x %e %s %s', 2 / 3, 1e-7, 12.25, 42, 255, 12345.678, 7, 'text'))\n"
        "game.log(tonumber('0.1e-3') .. ' ' .. tonumber('0x10') .. ' ' .. math.sqrt(2) .. ' ' .. 7 // 2 .. ' ' .. 7.5 % 2)\n"
        "game.log((0x1.000000000000080000000001p0 == 1 and 'hex equal ' or 'hex above ') ..\n"
        "         (tonumber('0x1.000000000000080000000001p0') == 1 and 'equal ' or 'above ') .. 0x.8p1 .. ' ' .. 0xA)\n"
        "game.log(0x1p99999999999 .. ' ' .. 0x1p-99999999999 .. ' ' .. 0x1p8000000000 .. ' ' .. 1e99999999999)\n"
        "game.log(math.pi .. ' ' .. math.huge .. ' ' .. math.maxinteger .. ' ' .. 3 ^ 40 .. ' ' .. 2 ^ -3)\n",
        1, 1);

    run("the order of a table's keys",
        "local t, keys = {}, {}\n"
        "for i = 1, 40 do local k = {i}; keys[i] = k; t[k] = i end\n"
        "t[game.log] = 100; t[game.pos] = 200; t.name = 300; t[1.5] = 400; t[true] = 500\n"
        "local s = ''\n"
        "for k, v in pairs(t) do s = s .. v .. ' ' end\n"
        "game.log(s)\n"
        "local a = {}\n"
        "for i = 1, 200 do a[i] = (i * 7919) % 211 end\n"
        "local calls = 0\n"
        "table.sort(a, function(x, y) calls = calls + 1; return x < y end)\n"
        "game.log(a[1] .. ' ' .. a[100] .. ' ' .. a[200] .. ' ' .. calls)\n",
        1, 1);

    run("what the sandbox leaves out",
        "local function no(what, f, ...)\n"
        "  local ok, err = pcall(f, ...)\n"
        "  game.log(what .. ': ' .. (ok and 'ALLOWED' or 'refused'))\n"
        "end\n"
        "local gone = ''\n"
        "for _, n in ipairs({'print', 'tostring', 'load', 'loadfile', 'dofile', 'collectgarbage', 'warn', 'require',\n"
        "                    'io', 'os', 'debug', 'coroutine', 'utf8', 'package'}) do\n"
        "  if _G[n] ~= nil then gone = gone .. n .. ' ' end\n"
        "end\n"
        "for _, n in ipairs({'dump', 'pack', 'unpack', 'packsize', 'match', 'gmatch', 'gsub'}) do\n"
        "  if string[n] ~= nil then gone = gone .. 'string.' .. n .. ' ' end\n"
        "end\n"
        "for _, n in ipairs({'random', 'randomseed', 'tan', 'asin', 'acos', 'atan', 'exp', 'log'}) do\n"
        "  if math[n] ~= nil then gone = gone .. 'math.' .. n .. ' ' end\n"
        "end\n"
        "if table.move ~= nil then gone = gone .. 'table.move ' end\n"
        "game.log('still there: [' .. gone .. ']')\n"
        "no('%p', string.format, '%p', {})\n"
        "no('%a', string.format, '%a', 1.5)\n"
        "no('%q', string.format, '%q', 1.5)\n"
        "no('%s of a table', string.format, '%s', {})\n"
        "no('%5s of a function', string.format, 'a %% %5s', game.log)\n"
        "no('__gc', setmetatable, {}, {__gc = function() end})\n"
        "local third = 0.3\n"
        "no('a power with a fraction', function() return 2 ^ third end)\n"
        "no('insert with a metatable', table.insert, setmetatable({}, {__len = function() return 1e15 end}), 1, 1)\n"
        "local big = {__len = function() return 2 ^ 30 end, __index = rawlen, __newindex = rawlen}\n"
        "no('sort with a metatable', table.sort, setmetatable({}, big))\n"
        "no('concat with a metatable', table.concat, setmetatable({}, big))\n"
        "no('unpack with a metatable', table.unpack, setmetatable({}, big))\n"
        "no('remove with a metatable', table.remove, setmetatable({}, {__len = function() return 1e15 end}), 1)\n"
        "game.log('#' .. string.rep('', 1e15) .. '# ' .. string.rep('ab', 3, '-') .. ' ' .. ('x'):rep(3))\n"
        "game.log('find: ' .. string.find('a.b', '.') .. ' ' .. (string.find('abc', '%a') or 'none'))\n"
        "local t = {}; table.insert(t, 'x'); table.insert(t, 1, 'y'); game.log(table.concat(t, ',') .. table.remove(t))\n",
        1, 1);

    run("what a script reads",
        "local x, y, z = game.ball_pos(0)\n"
        "game.log(x .. ' ' .. y .. ' ' .. z .. ' ' .. game.ball_type(0) .. ' ' .. game.garibs(0) .. ' ' ..\n"
        "         game.health(0) .. ' ' .. game.lives(0) .. ' ' .. game.level())\n"
        "game.log((game.ball_pos(3) == nil and game.ball_type(3) == nil and game.garibs(3) == nil and\n"
        "          game.health(3) == nil and game.lives(3) == nil) and 'an absent player gives nothing' or 'WRONG')\n"
        "game.log(math.sin(1.5) .. ' ' .. math.cos(2) .. ' ' .. (pcall(math.sin, 1e7) and 'ALLOWED' or 'refused') ..\n"
        "         ' ' .. (pcall(math.cos, 0 / 0) and 'ALLOWED' or 'refused'))\n",
        1, 1);

    run("hurting and healing",
        "game.hurt(0); game.hurt(0); local a = game.health(0)\n"
        "game.set_health(0, 3); local b = game.health(0)\n"
        "game.hurt(7); game.set_health(7, 2)\n"
        "game.log(a .. ' ' .. b .. ' ' .. ((pcall(game.set_health, 0, 0) or pcall(game.set_health, 0, 4) or\n"
        "         pcall(game.set_health, 0, 1.5) or pcall(game.hurt, 16)) and 'ALLOWED' or 'refused'))\n",
        1, 1);

    run("random numbers",
        "local s = ''\n"
        "for i = 1, 8 do s = s .. game.random(6) .. ' ' end\n"
        "for i = 1, 4 do s = s .. game.random(-3, 3) .. ' ' end\n"
        "game.log(s .. game.random() .. ' ' .. game.random(2147483647) .. ' ' .. game.random(5, 5))\n"
        "local low, high, sum = 1, 0, 0\n"
        "for i = 1, 1000 do local r = game.random(); sum = sum + r\n"
        "  if r < low then low = r end; if r > high then high = r end end\n"
        "game.log((low >= 0 and high < 1 and sum > 450 and sum < 550) and 'spread from 0 to 1' or 'WRONG')\n"
        "game.log((pcall(game.random, 0) or pcall(game.random, 3, 2) or pcall(game.random, 1.5) or\n"
        "          pcall(game.random, -2147483648, 0) or pcall(game.random, 1, 2147483648)) and 'ALLOWED' or 'refused')\n",
        1, 1);

    run("players who join and leave",
        "local s = ''\n"
        "function on_join(p) s = s .. 'join ' .. p .. ' at ' .. game.frame() .. ', ' end\n"
        "function on_leave(p) s = s .. 'leave ' .. p .. ' at ' .. game.frame() .. ', ' end\n"
        "function on_frame() if game.frame() == 11 then game.log(s) end end\n",
        12, 1);

    run("an error in on_join ends the script", "function on_join(p) error('no ' .. p) end\n", 3, 0);

    run("the events of a frame share its work",
        "function on_join(p) for i = 1, 1500000 do end end\n"
        "function on_frame() for i = 1, 1500000 do end end\n",
        3, 0);

    run("on_frame behind a metatable is not called",
        "setmetatable(_G, {__index = function(t, k)\n"
        "  if k == 'on_frame' then return function() game.place(0, 99, 0, 0) end end\n"
        "end})\n",
        5, 1);

    run("a script without end is stopped", "function on_frame() while true do end end\n", 3, 0);

    run("a script that catches the stop is stopped",
        "function on_frame()\n"
        "  while true do pcall(function() while true do end end) end\n"
        "end\n",
        3, 0);

    run("a handler that does not end is stopped",
        "function on_frame()\n"
        "  while true do xpcall(function() while true do end end, function() while true do end end) end\n"
        "end\n",
        3, 0);

    run("a script that runs too long at its start does not start", "while true do end\n", 3, 0);

    run("text without end is stopped",
        "function on_frame()\n"
        "  local s = 'x'\n"
        "  while true do pcall(function() s = s .. s end) end\n"
        "end\n",
        3, 0);

    run("a search without end is stopped",
        "local s = string.rep('a', 500000)\n"
        "function on_frame()\n"
        "  for i = 1, 1000 do pcall(string.find, s, 'b') end\n"
        "end\n",
        3, 0);

    run("a script that keeps everything runs out of memory",
        "local all = {}\n"
        "function on_frame()\n"
        "  for i = 1, 2000 do all[#all + 1] = {i, i + 1, i + 2} end\n"
        "end\n",
        200, 0);

    run("blocks that grow reuse the memory",
        "local keep\n"
        "function on_frame()\n"
        "  local t = {}\n"
        "  for i = 1, 100 + game.frame() * 20 do t[i] = i end\n"
        "  keep = t\n"
        "end\n",
        400, 1);

    run("an error ends the script", "function on_frame() error('line one\\nline two \\200 ' .. game.frame()) end\n", 3, 0);

    run("a power in the text that has no exact answer", "local x = 2 ^ 0.3\n", 1, 0);

    run("an error in the text", "function on_frame( end\n", 3, 0);

    run("compiled code is not taken", "\033Lua", 1, 0);

    /* Events from the game's step: the script hears of them at the next frame, in their order. Events of an
       unknown kind or player are dropped, and so are those past the 64th of one step. */
    {
        static const char script[] =
            "local s, n = '', 0\n"
            "function on_garib(p) n = n + 1; if n <= 3 then s = s .. 'garib ' .. p .. ' at ' .. game.frame() .. ', ' end end\n"
            "function on_death(p) s = s .. 'death ' .. p .. ' at ' .. game.frame() .. ', ' end\n"
            "function on_frame() if game.frame() == 3 then game.log(s .. n .. ' garibs') end end\n";
        int i;
        printf("events from the game's step\n");
        frames = 0;
        G64LuaEvent(1, 0);
        G64LuaLoad(script, (unsigned int)strlen(script));
        G64LuaFrame();
        G64LuaEvent(1, 3);
        G64LuaEvent(2, 15);
        G64LuaEvent(0, 0);
        G64LuaEvent(3, 0);
        G64LuaEvent(1, 16);
        G64LuaEvent(1, -1);
        frames = 1;
        G64LuaFrame();
        for (i = 0; i < 100; i++) {
            G64LuaEvent(1, 2);
        }
        frames = 2;
        G64LuaFrame();
        frames = 3;
        G64LuaFrame();
        if (!G64LuaActive()) {
            printf("FAIL events from the game's step: the script stopped\n");
            failed++;
        }
        G64LuaReset();
    }

    printf("%s: %d failures\n", failed ? "FAIL" : "ok", failed);
    return failed != 0;
}
