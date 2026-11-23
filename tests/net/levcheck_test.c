/* The level file's structure check (src/client/levcheck.h), without the game: small files made here, one for each
 * rule. The test with the ROM (tests/rom/levcheck.c) compares the check with the game's parser on real levels. */
#include <stdio.h>
#include <string.h>

#include "../../src/client/levcheck.h"

static uint8_t file[4096];
static size_t len;
static int failures;

static void begin(void) {
    memset(file, 0, sizeof file);
    memcpy(file + 4, "t.ln", 5);
    len = 9;
}

static void word(int v) {
    file[len++] = (uint8_t)(v >> 8);
    file[len++] = (uint8_t)v;
}

/* A token and count bytes of fields, all zero. */
static void token(int t, int count) {
    word(t);
    len += (size_t)count;
}

/* Ends the file, sets its size, and checks it. want: 1 when the check must accept the file. */
static void end(const char *what, int want) {
    size_t at = 0;
    const char *why;
    word(0x7D00);
    file[2] = (uint8_t)(len >> 8);
    file[3] = (uint8_t)len;
    why = g64_lev_check(file, len, NULL, NULL, &at);
    printf("%s: %s\n", what, why ? why : "accepted");
    if ((why == NULL) != want) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void) {
    size_t at = 0;
    begin();
    end("an empty level", 1);

    begin();
    token(98, 12);  /* PLATFORM */
    token(111, 2);  /* TAG */
    token(107, 14); /* POINT */
    token(4, 0);    /* PUZZLE */
    word(149); /* CONDITION 15: the glove is on the platform with tag 0 */
    word(15);
    len += 4;
    word(150); /* ACTION 45: start the move of the platform with tag 0 */
    word(45);
    len += 12;
    token(131, 20); /* ENEMY */
    token(154, 34); /* NORMAL_INSTRUCTION, type 0 */
    token(132, 0);  /* END_ENEMY */
    end("a platform, a puzzle and an enemy", 1);

    begin();
    token(9, 0);
    end("a token the game has no handler for", 0);

    begin();
    token(201, 0);
    end("a token past the game's last", 0);

    begin();
    token(111, 2);
    end("a TAG before any platform", 0);

    begin();
    token(149, 6);
    end("a CONDITION before any PUZZLE", 0);

    begin();
    token(154, 34);
    end("an instruction before any ENEMY", 0);

    begin();
    token(134, 16);
    end("a GARIB before any GARIBGROUP", 0);

    begin();
    token(98, 12);
    token(118, 4);
    end("an ORBITPAUSE before the platform's ORBIT", 0);

    begin();
    token(98, 12);
    token(107, 14);
    token(107, 14);
    token(117, 18);
    end("an ORBIT after two points", 0);

    begin();
    token(98, 12);
    token(107, 14);
    token(117, 18);
    end("an ORBIT after one point", 1);

    begin();
    token(98, 12);
    token(107, 14);
    token(121, 12);
    end("a SCALE after a point", 0);

    begin();
    token(98, 12);
    token(127, 10); /* SPIN */
    word(124);      /* SPINPAUSE with pause type 1 */
    word(1);
    word(0);
    token(125, 6);
    end("a SPINFLIP after a SPINPAUSE", 0);

    begin();
    token(98, 12);
    word(137); /* TELEPORT to tag 7 */
    word(0);
    word(7);
    len += 16;
    end("a TELEPORT to a tag no platform has", 0);

    begin();
    token(98, 12);
    word(111);
    word(7);
    token(98, 12);
    word(137);
    word(0);
    word(7);
    len += 16;
    end("a TELEPORT to a tag a platform before it has", 1);

    begin();
    token(131, 20);
    word(154);
    word(25);
    len += 2;
    end("an instruction of an unknown type", 0);

    begin();
    token(170, 0);
    word(171);
    word(7);
    len += 4;
    end("a cameo instruction of an unknown type", 0);

    begin();
    word(2); /* BALL_START with a place whose first half word is 128, the SEESAW token */
    word(0);
    word(0x4300);
    word(128);
    len += 8;
    end("a ball start that reads as a token", 0);

    begin();
    word(2);
    word(0);
    word(0x4300);
    word(0);
    len += 8;
    end("a ball start that does not", 1);

    /* Counts and values. */
    {
        int i;
        begin();
        for (i = 0; i < 31; i++)
            token(131, 20);
        end("31 enemies", 0);

        begin();
        token(131, 20);
        for (i = 0; i < 11; i++)
            token(155, 34);
        end("11 conditional instructions of one enemy", 0);

        begin();
        token(131, 20);
        for (i = 0; i < 10; i++)
            token(155, 34);
        token(131, 20);
        for (i = 0; i < 10; i++)
            token(155, 34);
        end("10 conditional instructions of each of two enemies", 1);

        begin();
        token(162, 30);
        for (i = 0; i < 7; i++)
            token(163, 4);
        end("7 patterns of one vent", 0);

        begin();
        token(170, 0);
        for (i = 0; i < 21; i++)
            token(171, 16);
        end("21 instructions of one cameo", 0);
    }

    begin();
    word(131);
    word(59);
    len += 18;
    end("an enemy of type 59", 0);

    begin();
    token(98, 12);
    word(127); /* SPIN on axis 3 */
    word(3);
    len += 8;
    end("a SPIN on axis 3", 0);

    begin();
    token(98, 12);
    word(112); /* PENDULUM on axis -1 */
    word(-1);
    len += 14;
    end("a PENDULUM on axis -1", 0);

    begin();
    token(133, 4);
    word(134);
    len += 12;
    word(4);
    len += 2;
    end("a GARIB of type 4", 0);

    begin();
    token(98, 12);
    token(107, 14);
    word(167);
    word(1);
    end("a STARTPOINT at a point the path does not have", 0);

    begin();
    token(4, 0);
    word(149); /* CONDITION 27: variable 100 equals 5 */
    word(27);
    word(100);
    word(5);
    end("a condition on variable 0 against variable 5", 1);

    begin();
    token(4, 0);
    word(149);
    word(27);
    word(200);
    word(5);
    end("a condition on a variable past the last", 0);

    begin();
    token(4, 0);
    word(150); /* ACTION 56: set variable 100 */
    word(56);
    len += 4;
    word(100);
    len += 6;
    end("an action that sets variable 100", 0);

    begin();
    token(4, 0);
    word(150); /* ACTION 57 with flag 0x80: add variable 100.0 to variable 3 */
    word(57);
    word(0x42C8);
    word(0);
    word(3);
    word(0);
    word(0);
    word(0x80);
    end("an action that reads variable 100", 0);

    begin();
    token(4, 0);
    word(150);
    word(57);
    word(0x4248); /* 50.0 */
    word(0);
    word(3);
    word(0);
    word(0);
    word(0x80);
    end("an action that reads variable 50", 1);

    begin();
    token(4, 0);
    word(149);
    word(8);
    len += 4;
    end("a condition with a number below the game's first", 0);

    begin();
    token(4, 0);
    word(150);
    word(88);
    len += 12;
    end("an action with a number past the game's last", 0);

    begin();
    word(189);
    word(420);
    len += 4;
    end("a sound past the game's last", 0);

    begin();
    word(141); /* ROPEBRIDGE with 101 planks */
    word(101);
    len += 68;
    end("a rope bridge of 101 planks", 0);

    begin();
    token(140, 52);
    len -= 2; /* the end token then overwrites the token's last two bytes */
    end("a token cut by the end of the file", 0);

    begin();
    token(98, 12);
    file[2] = (uint8_t)(len >> 8);
    file[3] = (uint8_t)len;
    printf("a file without an end token: %s\n", g64_lev_check(file, len, NULL, NULL, &at));
    failures += g64_lev_check(file, len, NULL, NULL, &at) == NULL;

    begin();
    word(0x7D00);
    file[3] = (uint8_t)(len + 1);
    printf("a file with another size in its first word: %s\n", g64_lev_check(file, len, NULL, NULL, &at));
    failures += g64_lev_check(file, len, NULL, NULL, &at) == NULL;

    memset(file, 'x', 16);
    file[0] = file[1] = file[2] = 0;
    file[3] = 16;
    printf("a name without an end: %s\n", g64_lev_check(file, 16, NULL, NULL, &at));
    failures += g64_lev_check(file, 16, NULL, NULL, &at) == NULL;

    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
