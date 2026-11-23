/* The level file's structure check (levcheck.h). The lengths and rules below are from a read of weemitt's
 * src/game/parse.c and src/game/nmeprocess.c (NMEInitInstruction); the test tests/rom/levcheck.c compares them with
 * that parser on every level of the ROM. Token numbers are weemitt's enum Token (include/enums.h). */
#include "levcheck.h"

#include <string.h>

enum {
    T_HAND_START = 1, T_BALL_START = 2, T_PUZZLE = 4, T_PUZZLE_NUMTIMES = 7, T_PUZZLE_ANY = 8,
    T_HEADSTART = 92, T_DUMMY = 93, T_LOOKATPOINT = 94, T_GOFORWARDS = 95, T_LOOKATHAND = 96, T_LOOKATBALL = 97,
    T_PLATFORM = 98, T_CRUMBLE = 103, T_CATAPULT = 105, T_TERRAIN = 106, T_POINT = 107, T_ACCEL = 109, T_TAG = 111,
    T_PENDULUM = 112, T_ORBIT = 117, T_ORBITPAUSE = 118, T_ORBITFLIP = 119, T_SYMMETRY = 120, T_SCALE = 121,
    T_TAKESPIN = 123, T_SPINPAUSE = 124, T_SPINFLIP = 125, T_MAXROT = 126, T_SPIN = 127, T_SEESAW = 128,
    T_TOPPLE = 129, T_ENEMY = 131, T_END_ENEMY = 132, T_GARIBGROUP = 133, T_GARIB = 134, T_TELEPORT = 137,
    T_BACKGROUND = 145, T_LANDSCAPE = 146, T_LANDROT = 147, T_LANDSCALE = 148, T_CONDITION = 149, T_ACTION = 150,
    T_NORMAL_INSTRUCTION = 154, T_CONDITIONAL_INSTRUCTION = 155, T_ATTACK_INSTRUCTION = 156, T_TILTY = 159,
    T_CONFINE_ENEMY = 161, T_VENT = 162, T_VENT_PATTERN = 163, T_STARTPOINT = 167, T_CAMEO = 170,
    T_CAMEO_INSTRUCTION = 171, T_MODGE = 179, T_SPARKLY = 180, T_BUZZER = 181, T_BUZZER_PATTERN = 182,
    T_NOCAMCOLLIDE = 185, T_DISABLE_REDIRECTION = 186, T_CAMEO_OBJ = 188, T_CAMEO_CAM = 191, T_STOP_SOUND = 194,
    T_SPIN_SOUND = 195, T_ORBIT_SOUND = 196, T_SPIN_STOP_SOUND = 197, T_ORBIT_STOP_SOUND = 198,
    T_CRUMBLE_SOUND = 199, T_DESTROY_SOUND = 200, T_DESTRUCTIBLE = 101, T_END = 0x7D00, T_MAX = 200
};

/* The bytes of each token's fields, after the token's own two. NONE: the parser has no handler for the number.
   VARIES: the length follows the fields or what came before (fields_of). */
#define NONE (-1)
#define VARIES (-2)
static const short fieldBytes[T_MAX + 1] = {
    /*   0 */ NONE, 12, VARIES, 20, 0, 0, 0, 2, 2, NONE,
    /*  10 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  20 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  30 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  40 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  50 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  60 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  70 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    /*  80 */ NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, 6, 18,
    /*  90 */ 6, 14, 2, 0, 8, 16, 8, 8, 12, 6,
    /* 100 */ 0, 18, 0, 8, 12, 26, 2, 14, 4, 4,
    /* 110 */ 6, 2, 16, 2, 24, 14, 12, 18, 4, 6,
    /* 120 */ 2, 12, 16, 0, 4, 6, 4, 10, 24, 28,
    /* 130 */ 0, 20, 0, 4, 16, 18, NONE, 20, 30, 30,
    /* 140 */ 52, 70, 2, 60, 60, 24, 24, 12, 12, VARIES,
    /* 150 */ VARIES, 14, 6, 26, VARIES, VARIES, VARIES, 0, 16, 16,
    /* 160 */ 62, 24, 30, 4, 0, 8, 12, 2, 4, 4,
    /* 170 */ 0, VARIES, NONE, NONE, NONE, NONE, NONE, NONE, NONE, 0,
    /* 180 */ 2, 48, 4, 4, 0, 0, 0, 14, 24, 6,
    /* 190 */ 32, 6, 0, 6, 6, 6, 6, 6, 6, 6,
    /* 200 */ 6};

/* What a token's handler uses, which a token before it in this file must have made. */
enum { NEED_PLATFORM = 1, NEED_ORBIT = 2, NEED_MOTION = 4, NEED_PUZZLE = 8, NEED_ENEMY = 16, NEED_VENT = 32,
       NEED_LAND = 64, NEED_GROUP = 128, NEED_CAMEO = 256, NEED_BUZZER = 512, NEED_POINT = 1024, NEED_POINTS2 = 2048,
       NEED_CRUMBLE = 4096, NEED_DESTRUCTIBLE = 8192 };

/* The most platforms of one file that the check follows. The game's largest level has about 150. */
#define MAX_PLATFORMS 2048

typedef struct {
    /* The platform that PLATFORM or DUMMY made last, and what the tokens after it gave it. */
    int platform, points, orbit, orbitPause, orbitFlip, motion, spinPause, spinFlip, crumble, destructible;
    int puzzle, enemy, vent, land, group, cameo, buzzer;
    /* What the parser's "last thing made" switch stands at: T_PLATFORM, T_VENT, T_LANDSCAPE, T_BACKGROUND,
       T_CAMEO_OBJ, or 0. */
    int mode;
    /* Counts that the game keeps in lists of a fixed size: the level's enemies and rope bridges, the last enemy's
       three lists of instructions, the last vent's and buzzer's patterns, the last cameo's instructions. */
    int enemies, bridges, normal, conditional, attack, ventPatterns, buzzerPatterns, cameoSteps;
    /* Each platform's tag, for TELEPORT's lookup. tags[platforms - 1] is the current platform's. */
    int16_t tags[MAX_PLATFORMS];
    int platforms;
} walk;

static int be16(const uint8_t *p) {
    return (int16_t)(uint16_t)((p[0] << 8) | p[1]);
}

/* 1 when four bytes are a float that is zero (plus or minus). */
static int zero_float(const uint8_t *p) {
    return (p[0] & 0x7F) == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

static int needs(int token, const walk *w) {
    switch (token) {
    case 5: case 6: case T_PUZZLE_NUMTIMES: case T_PUZZLE_ANY: case T_CONDITION: case T_ACTION:
        return NEED_PUZZLE;
    case T_END_ENEMY: case T_NORMAL_INSTRUCTION: case T_CONDITIONAL_INSTRUCTION: case T_ATTACK_INSTRUCTION:
    case T_CONFINE_ENEMY: case T_DISABLE_REDIRECTION:
        return NEED_ENEMY;
    case T_VENT_PATTERN:
        return NEED_VENT;
    case T_LANDROT: case T_LANDSCALE: case T_CAMEO_CAM:
        return NEED_LAND;
    case T_GARIB:
        return NEED_GROUP;
    case T_CAMEO_INSTRUCTION:
        return NEED_CAMEO;
    case T_BUZZER_PATTERN:
        return NEED_BUZZER;
    case T_HEADSTART:
        /* A platform's or a vent's, by what was made last. After anything else the handler does nothing. */
        return w->mode == T_PLATFORM ? NEED_PLATFORM : w->mode == T_VENT ? NEED_VENT : 0;
    case T_TERRAIN: case T_MODGE:
        return w->mode == T_PLATFORM ? NEED_PLATFORM : NEED_LAND;
    case T_SPARKLY: case T_NOCAMCOLLIDE:
        return w->mode == T_PLATFORM ? NEED_PLATFORM : w->mode == T_LANDSCAPE ? NEED_LAND : 0;
    case T_ORBITPAUSE: case T_ORBITFLIP: case T_ORBIT_SOUND: case T_ORBIT_STOP_SOUND:
        return NEED_PLATFORM | NEED_ORBIT;
    case T_SYMMETRY: case T_SPINPAUSE: case T_SPINFLIP: case T_MAXROT: case T_SPIN_SOUND: case T_SPIN_STOP_SOUND:
        return NEED_PLATFORM | NEED_MOTION;
    case T_STARTPOINT: case T_STOP_SOUND:
        return NEED_PLATFORM | NEED_POINT;
    case T_LOOKATPOINT: case T_GOFORWARDS:
        return NEED_PLATFORM | NEED_POINTS2;
    case T_CRUMBLE_SOUND:
        return NEED_PLATFORM | NEED_CRUMBLE;
    case T_DESTROY_SOUND:
        return NEED_PLATFORM | NEED_DESTRUCTIBLE;
    default:
        break;
    }
    /* Every other token from SPINTOMOVE (88) to DESTROY_SOUND (200) that is a platform's. */
    if ((token >= 88 && token <= 92) || (token >= 94 && token <= 97) || (token >= 99 && token <= 105) ||
        (token >= 107 && token <= 130) || (token >= 137 && token <= 139) || token == 142 ||
        (token >= 157 && token <= 159) || token == 164 || (token >= 166 && token <= 168) || token == 184 ||
        (token >= 192 && token <= 200))
        return NEED_PLATFORM;
    return 0;
}

static const char *missing(int need, const walk *w) {
    if ((need & NEED_PLATFORM) && !w->platform)
        return "a platform's token comes before any platform";
    if ((need & NEED_ORBIT) && !w->orbit)
        return "an orbit's token comes before the platform's ORBIT";
    if ((need & NEED_MOTION) && !w->motion)
        return "a spin's token comes before the platform has a spin";
    if ((need & NEED_POINT) && w->points < 1)
        return "the token needs a point on the platform's path";
    if ((need & NEED_POINTS2) && w->points < 2)
        return "the token needs two points on the platform's path";
    if ((need & NEED_CRUMBLE) && !w->crumble)
        return "a crumble sound comes before the platform's CRUMBLE";
    if ((need & NEED_DESTRUCTIBLE) && !w->destructible)
        return "a destroy sound comes before the platform's DESTRUCTIBLE";
    if ((need & NEED_PUZZLE) && !w->puzzle)
        return "a puzzle's token comes before any PUZZLE";
    if ((need & NEED_ENEMY) && !w->enemy)
        return "an enemy's token comes before any ENEMY";
    if ((need & NEED_VENT) && !w->vent)
        return "a vent's token comes before any VENT";
    if ((need & NEED_LAND) && !w->land)
        return "a landscape token comes before any landscape piece";
    if ((need & NEED_GROUP) && !w->group)
        return "a GARIB comes before any GARIBGROUP";
    if ((need & NEED_CAMEO) && !w->cameo)
        return "a cameo instruction comes before any CAMEO";
    if ((need & NEED_BUZZER) && !w->buzzer)
        return "a buzzer pattern comes before any BUZZER";
    return NULL;
}

/* The bytes of a token's fields where they follow the fields themselves. d points at the fields, of which have
   bytes are in the file. Returns the count, or -1 with a message. */
static int fields_of(int token, const uint8_t *d, size_t have, const walk *w, const char **why) {
    int id;
    if (token == T_BALL_START)
        return 14;
    if (have < 2) {
        *why = "the file ends inside a token";
        return -1;
    }
    id = be16(d);
    switch (token) {
    case T_CONDITION:
        /* A camera's box or sphere for ids 34 to 41, a tag and a value for the rest. */
        return id == 34 ? 30 : id == 35 || id == 37 || id == 39 ? 26 : id == 36 || id == 38 || id == 40 ? 18
               : id == 41 ? 22 : 6;
    case T_ACTION:
        switch (id) {
        case 70: case 71: case 72: case 85:
            return 8;
        case 73: case 77:
            return 16;
        case 74:
            return 12;
        case 75: case 76:
            return 4;
        case 84:
            return 32;
        case 86:
            return 14;
        case 53: case 59: case 60: case 61: case 62: case 63: case 64:
            return 26;
        case 79:
            return 22;
        default:
            return 14;
        }
    case T_NORMAL_INSTRUCTION: case T_CONDITIONAL_INSTRUCTION: case T_ATTACK_INSTRUCTION:
        switch (id) {
        case 0: case 1: case 2: case 9: case 13: case 22: case 10: case 11: case 23:
            return 34;
        case 3:
            return 46;
        case 4: case 5: case 6: case 7: case 12: case 14: case 15: case 16: case 18: case 19: case 20: case 21:
        case 24: case 8: case 17:
            return 26;
        default:
            /* The parser reads only the type and the time of such an instruction, and the rest as tokens. */
            *why = "an enemy's instruction has a type the game does not know";
            return -1;
        }
    case T_CAMEO_INSTRUCTION:
        switch (id) {
        case 0: return 16;
        case 1: return 20;
        case 2: return 24;
        case 3: case 4: return 12;
        case 5: return 8;
        case 6: return 10;
        default:
            *why = "a cameo's instruction has a type the game does not know";
            return -1;
        }
    default:
        break;
    }
    (void)w;
    *why = "the check has no rule for a token";
    return -1;
}

/* A new platform's state. */
static const char *new_platform(walk *w) {
    if (w->platforms >= MAX_PLATFORMS)
        return "the level has more platforms than the check follows";
    w->tags[w->platforms++] = 0;
    w->platform = 1;
    w->points = w->orbit = w->orbitPause = w->orbitFlip = w->motion = w->spinPause = w->spinFlip = 0;
    w->crumble = w->destructible = 0;
    return NULL;
}

/* The rules that keep a token to its full form, and what the token leaves behind. d points at the token's fields,
   which are all inside the file. Returns NULL or a message. */
static const char *apply(int token, const uint8_t *d, walk *w) {
    int i;
    switch (token) {
    case T_BALL_START:
        /* In some states of the save the parser skips the place and reads its 12 bytes as six tokens. None of
           them may be a token: the parser then steps over them two bytes at a time, and both readings meet again. */
        for (i = 0; i < 6; i++) {
            int t = be16(d + 2 + 2 * i);
            if (t == T_END || (t >= 0 && t <= T_MAX && fieldBytes[t] != NONE))
                return "the ball's start has a number that the game can read as a token: move it a little";
        }
        break;
    case T_PLATFORM:
        w->mode = T_PLATFORM;
        return new_platform(w);
    case T_DUMMY:
        return new_platform(w);
    case T_PUZZLE:
        w->puzzle = 1;
        break;
    case T_ENEMY:
        /* The type picks an entry of the game's table of 59 enemy kinds. The level's list holds 30 enemies. */
        if (be16(d) < 0 || be16(d) > 58)
            return "an ENEMY has a type the game does not have";
        if (++w->enemies > 30)
            return "the level has more than 30 enemies";
        w->enemy = 1;
        w->normal = w->conditional = w->attack = 0;
        break;
    case T_NORMAL_INSTRUCTION:
        if (++w->normal > 30)
            return "an enemy has more than 30 instructions";
        break;
    case T_CONDITIONAL_INSTRUCTION:
        if (++w->conditional > 10)
            return "an enemy has more than 10 conditional instructions";
        break;
    case T_ATTACK_INSTRUCTION:
        if (++w->attack > 30)
            return "an enemy has more than 30 attack instructions";
        break;
    case T_VENT:
        w->vent = 1;
        w->mode = T_VENT;
        w->ventPatterns = 0;
        break;
    case T_VENT_PATTERN:
        if (++w->ventPatterns > 6)
            return "a vent has more than 6 patterns";
        break;
    case T_BUZZER_PATTERN:
        if (++w->buzzerPatterns > 6)
            return "a buzzer has more than 6 patterns";
        break;
    case T_CAMEO_INSTRUCTION:
        if (++w->cameoSteps > 20)
            return "a cameo has more than 20 instructions";
        break;
    case T_GARIB:
        if (be16(d + 12) < 0 || be16(d + 12) > 3)
            return "a GARIB has a type the game does not have";
        break;
    case 135: /* PICKUP */
        if (be16(d) < 0 || be16(d) > 14)
            return "a PICKUP has a type the game does not have";
        break;
    case 141: /* ROPEBRIDGE: its planks, the planks' inertia, the axis they turn on */
        if (be16(d) < 1 || be16(d) > 100)
            return "a rope bridge has 1 to 100 planks";
        if (be16(d + 4) < 0 || be16(d + 4) > 2)
            return "a rope bridge's axis is 0, 1 or 2";
        if (++w->bridges > 10)
            return "the level has more than 10 rope bridges";
        break;
    case 189: case 190: /* AMBIENT_SOUND, SOUND */
    case 193: case 194: case 195: case 196: case 197: case 198: case 199: case 200: /* a platform's sounds */
        /* The first field is a sound's number in the game's table of 420. */
        if (be16(d) < 0 || be16(d) > 419)
            return "a sound's number is 0 to 419";
        break;
    case 187: /* TIP: its place, then its number. The game has tips 0 to 32. */
        if (be16(d + 12) < 0 || be16(d + 12) > 32)
            return "a TIP has a number the game does not have";
        break;
    case T_STARTPOINT:
        if (be16(d) < 0 || be16(d) >= w->points)
            return "a STARTPOINT names a point the platform's path does not have";
        break;
    case T_CONDITION: {
        /* Conditions 27 to 30 compare a puzzle variable, of which the level has 100, with a number, or with
           another variable when the first one's number is 100 or more. */
        int id = be16(d), tag = be16(d + 2), value = be16(d + 4);
        /* The game has conditions 9 to 44. Another number, cut to a byte, becomes a camera's condition without a
           camera's box. Conditions 31 and 32 test a platform's confine against one of its 6 limits. */
        if (id < 9 || id > 44)
            return "a puzzle condition has a number the game does not have";
        if ((id == 31 || id == 32) && (value < 0 || value > 5))
            return "a puzzle condition names a confine limit that is not 0 to 5";
        if (id >= 27 && id <= 30) {
            if (tag >= 100 ? (tag > 199 || value < 0 || value > 99) : tag < 0)
                return "a puzzle condition names a variable the level does not have";
        }
        break;
    }
    case T_ACTION: {
        /* Actions 56 to 58 set a puzzle variable, add to it or take from it: from a number, or from another
           variable when the action's flag 0x80 is set. The fields are the id, the number (a float), the
           variable, a delay and the flags. */
        int id = be16(d);
        if (id < 45 || id > 87)
            return "a puzzle action has a number the game does not have";
        if (id >= 56 && id <= 58) {
            int tag = be16(d + 6);
            uint32_t bits = ((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | d[5];
            if (tag < 0 || tag > 99)
                return "a puzzle action names a variable the level does not have";
            if (d[13] & 0x80) {
                /* The number is then a variable's: a float from 0 up to, not including, 100. As bits, such a
                   float is zero, or positive and below 100.0 (0x42C80000). */
                if (bits != 0 && bits != 0x80000000u && (bits >= 0x42C80000u || bits < 0x00800000u))
                    return "a puzzle action reads a variable the level does not have";
            }
        }
        break;
    }
    case T_LANDSCAPE: case T_BACKGROUND: case T_CAMEO_OBJ:
        w->land = 1;
        w->mode = token;
        break;
    case T_GARIBGROUP:
        w->group = 1;
        break;
    case T_CAMEO:
        w->cameo = 1;
        w->cameoSteps = 0;
        break;
    case T_BUZZER:
        w->buzzer = 1;
        w->buzzerPatterns = 0;
        break;
    case T_TAG:
        w->tags[w->platforms - 1] = (int16_t)be16(d);
        break;
    case T_POINT:
        /* The game adds a point only to a platform without an orbit, and 20 at most. */
        if (!w->orbit && w->points < 20)
            w->points++;
        break;
    case T_ORBIT:
        if (be16(d) < 0 || be16(d) > 2)
            return "an ORBIT's axis is 0, 1 or 2";
        if (w->points >= 2)
            return "an ORBIT comes after two points of the platform's path: the game reads only its axis then";
        w->orbit = 1;
        w->orbitPause = w->orbitFlip = 0;
        break;
    case T_ORBITPAUSE:
        if (w->orbitFlip)
            return "an ORBITPAUSE comes after an ORBITFLIP: the game reads none of its fields then";
        w->orbitPause = be16(d + 2) != 0;
        break;
    case T_ORBITFLIP:
        if (w->orbitPause)
            return "an ORBITFLIP comes after an ORBITPAUSE: the game reads none of its fields then";
        w->orbitFlip = !zero_float(d + 2);
        break;
    case T_SPINPAUSE:
        /* The game keeps the pause only when no flip is set. */
        if (!w->spinFlip)
            w->spinPause = be16(d) != 0;
        break;
    case T_SPINFLIP:
        if (w->spinPause)
            return "a SPINFLIP comes after a SPINPAUSE: the game reads none of its fields then";
        w->spinFlip = !zero_float(d + 2);
        break;
    case T_SPIN: case T_SEESAW: case T_TOPPLE: case T_PENDULUM:
        /* The first field is the axis, which picks one of three numbers in the platform's spin. SPIN and
           PENDULUM write four of the file's bytes at the place it picks. */
        if (be16(d) < 0 || be16(d) > 2)
            return "a spin's axis is 0, 1 or 2";
        /* fall through */
    case T_TILTY: case T_TAKESPIN: case T_CATAPULT:
    case T_LOOKATPOINT: case T_GOFORWARDS: case T_LOOKATHAND: case T_LOOKATBALL:
        /* Each makes the platform's spin, or clears the one it has. */
        w->motion = 1;
        w->spinPause = w->spinFlip = 0;
        break;
    case T_SCALE:
        if (w->points > 0)
            return "a SCALE comes after a point of the platform's path: the game reads none of its fields then";
        break;
    case T_CRUMBLE:
        w->crumble = 1;
        break;
    case T_DESTRUCTIBLE:
        w->destructible = 1;
        break;
    case T_TELEPORT: {
        /* The target's tag must be 0 or a tag that a platform before has: the game reads the rest of the fields
           only then. */
        int tag = be16(d + 2), found = tag == 0;
        for (i = 0; i < w->platforms && !found; i++)
            found = w->tags[i] == tag;
        if (!found)
            return "a TELEPORT's target is a tag that no platform before it has";
        break;
    }
    default:
        break;
    }
    return NULL;
}

const char *g64_lev_check(const uint8_t *lev, size_t n, g64_lev_token_fn fn, void *user, size_t *at) {
    walk state;
    walk *w = &state;
    size_t pos, size;
    const char *why;
    *at = 0;
    if (lev == NULL || n < 8)
        return "the file is too short to be a level";
    size = ((size_t)lev[0] << 24) | ((size_t)lev[1] << 16) | ((size_t)lev[2] << 8) | lev[3];
    if (size != n)
        return "the file does not start with its own size";
    /* The name, with its zero byte. */
    for (pos = 4; pos < n && lev[pos] != 0; pos++) {
    }
    if (pos >= n) {
        *at = 4;
        return "the level's name has no end";
    }
    pos++;
    memset(w, 0, sizeof *w);
    for (;;) {
        int token, len, need;
        *at = pos;
        if (n - pos < 2)
            return "the file has no end token";
        token = be16(lev + pos);
        if (fn)
            fn(user, pos, token);
        if (token == T_END)
            return NULL;
        if (token < 0 || token > T_MAX || fieldBytes[token] == NONE)
            return "the file has a token the game does not know";
        need = needs(token, w);
        why = missing(need, w);
        if (why)
            return why;
        len = fieldBytes[token];
        if (len == VARIES) {
            len = fields_of(token, lev + pos + 2, n - pos - 2, w, &why);
            if (len < 0)
                return why;
        }
        if ((size_t)len > n - pos - 2)
            return "the file ends inside a token";
        why = apply(token, lev + pos + 2, w);
        if (why)
            return why;
        pos += 2 + (size_t)len;
    }
}
