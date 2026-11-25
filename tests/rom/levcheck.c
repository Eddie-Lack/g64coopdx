/* The level file check (src/client/levcheck.c) against the game's own level parser, with the ROM.
 *
 * For each of the game's 48 levels the test starts the level, which makes the game's parser read the level's file
 * and record every token with its offset (weemitt's G64ParseTrace). The check then walks the same file from the
 * ROM. It must accept the file and name the same tokens at the same offsets, the end token too. A difference
 * means the check reads a token's length otherwise than the game does.
 *
 * The test also pins the rule for the level file's object ids (g64_pack_check_ids in src/client/pack.c):
 *   - the ids that g64_lev_object_ids reads are the ids FindObject is asked for while the parser reads those tokens
 *     (weemitt's G64FindObjectTrace), at the same tokens. Any other token that asks FindObject for an object asks
 *     for one of the game's own names: its id isn't a word of the token's fields
 *   - G64LevelBankNumbers names the object banks that LoadLevel put in the game's slots, slot by slot, and the
 *     table of bankranges.h has the same numbers
 *   - each level's own file passes g64_pack_check_ids with its own banks, and g64_pack_check_ids_rom (packrom.h),
 *     which the online host runs with the committed tables before it offers its pack
 *   - no shipped level has a ROPEBRIDGE, a SINEBRIDGE or an ORBITBRIDGE. The test adds one of each to a copy of
 *     level 13's file, with an object of the level's own banks, and compares the ids again. It also adds a
 *     LANDSCAPE with an id that no bank has: the rule refuses that file, g64_pack_check_ids_rom refuses it at the
 *     same token, and the game stops the level as unplayable
 *   - every object the game finds by a name of its own (G64GameObjectIds: the enemies', the pick-ups' and garibs',
 *     the standard bullets' without sprites, and the fixed names in the game's code) is in the ROM or in the bank
 *     check's list, so a pack's model can't be an enemy's, a bullet's or a pick-up's
 *
 * Usage: levcheck ROM. Without a ROM file it prints that it was skipped and exits with 0. make levcheck also runs
 * it under Wine and compares the outputs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankcheck.h"
#include "../../src/client/bankranges.h"
#include "../../src/client/levcheck.h"
#include "../../src/client/pack.h"
#include "../../src/client/packrom.h"
#include "../../src/client/rombanks.h"
#include "../../weemitt/bruteforce/engine/engine.h"

void port_game_start_level(int level);
extern int g64ParseLogOn, g64ParseLogCount;
extern int g64ParseLog[8192][2];
extern int g64FindLogCount;
extern unsigned g64FindLog[8192][2];
int G64LevelBankNumbers(int num, unsigned *banks, int max);
int G64ObjectSlotIds(int slot, unsigned *ids, int max);
int G64GameObjectIds(unsigned *ids, int max);
void G64SetPack(int level, unsigned rom, unsigned size);
int G64LevelUnplayable(void);

#define LEVELS 48

static struct {
    size_t offset;
    int token;
} seen[8192];
static int seenCount;

static void note(void *user, size_t offset, int token) {
    (void)user;
    if (seenCount < 8192) {
        seen[seenCount].offset = offset;
        seen[seenCount].token = token;
    }
    seenCount++;
}

/* The ids g64_lev_object_ids read. */
static struct {
    size_t offset;
    int token;
    uint32_t id;
} ids[8192];
static int idCount;
/* How many of each id token the check read. */
static int tokenUses[256];

static void note_id(void *user, size_t offset, int token, uint32_t id) {
    (void)user;
    if (idCount < 8192) {
        ids[idCount].offset = offset;
        ids[idCount].token = token;
        ids[idCount].id = id;
    }
    idCount++;
    if (token >= 0 && token < 256)
        tokenUses[token]++;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* 1 when id is a big-endian word at any offset of the token's fields, from offset to the next token. */
static int id_in_fields(const uint8_t *lev, size_t from, size_t to, uint32_t id) {
    size_t k;
    for (k = from; k + 4 <= to; k++)
        if (be32(lev + k) == id)
            return 1;
    return 0;
}

/* The ids of the game's FindObject log, against the ids the check read. Returns 1 when they agree. */
static int same_ids(int level, const uint8_t *lev, size_t levSize) {
    int k, j;
    /* Every FindObject call of an id token is one of the check's ids, at the same token. */
    for (k = 0; k < g64FindLogCount && k < 8192; k++) {
        int t = (int)g64FindLog[k][0];
        size_t offset = (size_t)g64ParseLog[t][0];
        int token = g64ParseLog[t][1], found = 0;
        for (j = 0; j < idCount && j < 8192 && !found; j++)
            found = ids[j].offset == offset && ids[j].id == g64FindLog[k][1];
        if (found)
            continue;
        for (j = 0; j < idCount && j < 8192; j++)
            if (ids[j].offset == offset)
                break;
        if (j < idCount) {
            printf("FAIL level %d: at byte %lu, token %d, FindObject was asked for %08x, and the check read "
                   "%08x\n", level, (unsigned long)offset, token, g64FindLog[k][1], ids[j].id);
            return 0;
        }
        {
            size_t next = t + 1 < g64ParseLogCount ? (size_t)g64ParseLog[t + 1][0] : levSize;
            if (id_in_fields(lev, offset + 2, next, g64FindLog[k][1])) {
                printf("FAIL level %d: token %d at byte %lu asks FindObject for %08x, a word of its fields, and the "
                       "check reads no id there\n", level, token, (unsigned long)offset, g64FindLog[k][1]);
                return 0;
            }
        }
    }
    /* Every id the check read was asked for. */
    for (j = 0; j < idCount && j < 8192; j++) {
        int found = 0;
        for (k = 0; k < g64FindLogCount && k < 8192 && !found; k++)
            found = (size_t)g64ParseLog[g64FindLog[k][0]][0] == ids[j].offset && g64FindLog[k][1] == ids[j].id;
        if (!found) {
            printf("FAIL level %d: the check read id %08x at byte %lu, token %d; FindObject wasn't asked for it "
                   "there\n", level, ids[j].id, (unsigned long)ids[j].offset, ids[j].token);
            return 0;
        }
    }
    return 1;
}

/* The game's object slots against G64LevelBankNumbers and the table, after the level started. */
static int same_banks(int level, const G64RomBanks *index) {
    unsigned game[G64_LEVEL_BANKS_MAX], table[G64_LEVEL_BANKS_MAX];
    static unsigned slotIds[4096];
    int n = G64LevelBankNumbers(level, game, G64_LEVEL_BANKS_MAX);
    int m = g64_level_object_banks(level, table, G64_LEVEL_BANKS_MAX);
    int slot, k;
    if (n < 1 || n > G64_LEVEL_BANKS_MAX) {
        printf("FAIL level %d: G64LevelBankNumbers gives %d banks\n", level, n);
        return 0;
    }
    if (m != n || memcmp(game, table, (size_t)n * sizeof game[0]) != 0) {
        printf("FAIL level %d: the table of bankranges.h differs from G64LevelBankNumbers:", level);
        for (k = 0; k < n; k++)
            printf(" %u", game[k]);
        printf("\n");
        return 0;
    }
    for (slot = 0; slot < 5; slot++) {
        int count = G64ObjectSlotIds(slot, slotIds, 4096);
        if (slot >= n) {
            if (count >= 0) {
                printf("FAIL level %d: object slot %d holds a bank, and G64LevelBankNumbers names %d\n", level, slot,
                       n);
                return 0;
            }
            continue;
        }
        if (count < 1 || count > 4096) {
            printf("FAIL level %d: object slot %d holds %d objects\n", level, slot, count);
            return 0;
        }
        /* The slot's ids are bank game[slot]'s: each is in it, and the bank has no more distinct ids. */
        for (k = 0; k < count; k++)
            if (!g64_rombanks_bank_has_object(index, game[slot], slotIds[k])) {
                printf("FAIL level %d: object slot %d has id %08x, which bank %u lacks\n", level, slot, slotIds[k],
                       game[slot]);
                return 0;
            }
        if (g64_rombanks_bank_object_count(index, game[slot]) > (size_t)count) {
            printf("FAIL level %d: bank %u has more ids than object slot %d\n", level, game[slot], slot);
            return 0;
        }
    }
    return 1;
}

/* ---- Copies of a level's file with tokens added before its end token. */

#define EDIT_LEVEL 13
static uint8_t added[512];
static size_t addedN;

static void put16(int v) {
    added[addedN++] = (uint8_t)((unsigned)v >> 8);
    added[addedN++] = (uint8_t)v;
}

static void put32(uint32_t v) {
    put16((int)(v >> 16));
    put16((int)(v & 0xFFFF));
}

static void putf(float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    put32(v);
}

/* An object's id and its 8 bytes of name. */
static void put_object(uint32_t id) {
    put32(id);
    memcpy(added + addedN, "g64test", 8);
    addedN += 8;
}

static void put_floats(int n, float f) {
    while (n-- > 0)
        putf(f);
}

/* The three bridges of tests/rom/models.c, with object id. */
static void add_bridges(uint32_t id) {
    addedN = 0;
    /* ROPEBRIDGE: planks, inertia, axis, the object, 7 numbers, the first plank, the step. */
    put16(141);
    put16(6);
    put16(1);
    put16(0);
    put_object(id);
    put_floats(7, 0.1f);
    put_floats(3, 0.0f);
    putf(20.0f);
    put_floats(2, 0.0f);
    /* SINEBRIDGE: planks, head start, the object, the first plank, the step, the swing, speed, acceleration. */
    put16(144);
    put16(5);
    put16(4);
    put_object(id);
    put_floats(3, 0.0f);
    putf(20.0f);
    put_floats(3, 0.0f);
    putf(30.0f);
    putf(0.0f);
    putf(1.0f);
    putf(0.1f);
    /* ORBITBRIDGE: the axis (Y), planks, head start, -1, the object, the center, the step, the offset, speed. */
    put16(143);
    put16(0x59);
    put16(4);
    put16(5);
    put16(-1);
    put_object(id);
    put_floats(3, 0.0f);
    putf(25.0f);
    put_floats(2, 0.0f);
    putf(30.0f);
    put_floats(2, 0.0f);
    putf(0.05f);
}

/* A LANDSCAPE with an object id and a place. */
static void add_landscape(uint32_t id) {
    addedN = 0;
    put16(146);
    put_object(id);
    put_floats(3, 0.0f);
}

static size_t endAt;
static void find_end(void *user, size_t offset, int token) {
    (void)user;
    if (token == 0x7D00)
        endAt = offset;
}

/* The level's file with the added tokens, in a new buffer of n bytes, or NULL. */
static uint8_t *edited(const uint8_t *lev, size_t size, size_t *n) {
    size_t bad = 0;
    uint8_t *out;
    endAt = 0;
    if (g64_lev_check(lev, size, find_end, NULL, &bad) != NULL || endAt == 0)
        return NULL;
    out = malloc(size + addedN);
    if (!out)
        return NULL;
    memcpy(out, lev, endAt);
    memcpy(out + endAt, added, addedN);
    memcpy(out + endAt + addedN, lev + endAt, size - endAt);
    *n = size + addedN;
    out[0] = (uint8_t)(*n >> 24);
    out[1] = (uint8_t)(*n >> 16);
    out[2] = (uint8_t)(*n >> 8);
    out[3] = (uint8_t)*n;
    return out;
}

/* Starts EDIT_LEVEL from the boot state with lev in the pack's place. buf receives the ROM and the file. */
static void start_edited(Engine *e, const void *boot, const uint8_t *rom, unsigned long romSize, const uint8_t *lev,
                         size_t n, uint8_t **buf) {
    int k;
    free(*buf);
    *buf = malloc(romSize + n);
    if (!*buf) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    memcpy(*buf, rom, romSize);
    memcpy(*buf + romSize, lev, n);
    engine_load(e, boot);
    engine_set_rom(e, *buf, romSize + n);
    G64SetPack(EDIT_LEVEL, (unsigned)romSize, (unsigned)n);
    g64ParseLogOn = 1;
    g64ParseLogCount = g64FindLogCount = 0;
    port_game_start_level(EDIT_LEVEL);
    for (k = 0; k < 5; k++) {
        EngineInput in;
        memset(&in, 0, sizeof in);
        engine_step(e, &in);
    }
}

/* g64_pack_check_ids_rom (packrom.h), as the online host runs it before the game has read its tables, on lev as
 * the file of level. Sets *rule and *at as that function does. */
static const char *rule_from_tables(int level, const uint8_t *lev, size_t n, const uint8_t *rom, size_t romSize,
                                    int *rule, size_t *at) {
    g64_pack p;
    memset(&p, 0, sizeof p);
    p.level = level;
    p.lev = (uint8_t *)lev;
    p.levSize = n;
    return g64_pack_check_ids_rom(&p, rom, romSize, rule, at);
}

/* The edited levels. Returns the number of failures. */
static int edited_levels(Engine *e, const void *boot, const uint8_t *rom, unsigned long romSize,
                         const G64RomBanks *index) {
    size_t at = 0, size = 0, n = 0, bad = 0;
    uint8_t *lev, *buf = NULL;
    uint32_t id = 0;
    const char *why;
    int k, failed = 0;
    g64_level_banks banks;
    unsigned nums[G64_LEVEL_BANKS_MAX];
    banks.count = (unsigned)G64LevelBankNumbers(EDIT_LEVEL, nums, G64_LEVEL_BANKS_MAX);
    memcpy(banks.num, nums, sizeof nums);
    if (g64_pack_level_from_rom(rom, romSize, EDIT_LEVEL, &at, &size) != NULL)
        return 1;
    /* The level's first PLATFORM's object. */
    idCount = 0;
    if (g64_lev_object_ids(rom + at, size, note_id, NULL, &bad) != NULL)
        return 1;
    for (k = 0; k < idCount && k < 8192 && !id; k++)
        if (ids[k].token == 98)
            id = ids[k].id;
    if (!id) {
        printf("FAIL level %d has no PLATFORM\n", EDIT_LEVEL);
        return 1;
    }
    add_bridges(id);
    lev = edited(rom + at, size, &n);
    if (!lev) {
        printf("FAIL the level file with three bridges\n");
        return 1;
    }
    why = g64_pack_check_ids(lev, n, NULL, NULL, &banks, g64_rombanks_bank_has, (void *)index, &bad);
    start_edited(e, boot, rom, romSize, lev, n, &buf);
    idCount = 0;
    if (why || g64_lev_object_ids(lev, n, note_id, NULL, &bad) != NULL || G64LevelUnplayable() ||
        !same_ids(EDIT_LEVEL, lev, n)) {
        printf("FAIL level %d with three bridges: %s\n", EDIT_LEVEL, why ? why : "the ids differ, or unplayable");
        failed++;
    } else {
        printf("ok   level %d with a ROPEBRIDGE, a SINEBRIDGE and an ORBITBRIDGE: %d object ids, the same as "
               "FindObject was asked for\n", EDIT_LEVEL, idCount);
    }
    free(lev);
    /* An id that no bank has. */
    add_landscape(0x12345678u);
    lev = edited(rom + at, size, &n);
    if (!lev) {
        printf("FAIL the level file with a LANDSCAPE\n");
        free(buf);
        return failed + 1;
    }
    why = g64_pack_check_ids(lev, n, NULL, NULL, &banks, g64_rombanks_bank_has, (void *)index, &bad);
    {
        /* The host's check before its offer must refuse the file at the same token. */
        int rule = 0;
        size_t tableAt = 0;
        const char *tableWhy = rule_from_tables(EDIT_LEVEL, lev, n, rom, romSize, &rule, &tableAt);
        if (!tableWhy || !rule || tableAt != endAt) {
            printf("FAIL level %d with an object that no bank has: the rule with the committed tables says %s\n",
                   EDIT_LEVEL, tableWhy ? tableWhy : "nothing");
            failed++;
        } else {
            printf("ok   level %d with an object that no bank has: the rule with the committed tables refuses it at "
                   "byte %lu\n", EDIT_LEVEL, (unsigned long)tableAt);
        }
    }
    start_edited(e, boot, rom, romSize, lev, n, &buf);
    if (!why || bad != endAt || !G64LevelUnplayable()) {
        printf("FAIL level %d with an object that no bank has: the rule says %s, the game %s\n", EDIT_LEVEL,
               why ? why : "nothing", G64LevelUnplayable() ? "stops the level" : "plays it");
        failed++;
    } else {
        printf("ok   level %d with an object that no bank has: the rule refuses it at byte %lu, and the game stops "
               "the level\n", EDIT_LEVEL, (unsigned long)bad);
    }
    free(lev);
    G64SetPack(-1, 0, 0);
    engine_set_rom(e, rom, romSize);
    free(buf);
    return failed;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    void *boot;
    const uint8_t *rom;
    unsigned long romSize = 0;
    int level, failed = 0;
    G64RomBanks *index;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (levcheck ROM)\n");
        return 0;
    }
    fclose(f);
    engine_default_options(&opt);
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine didn't start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(e));
    if (!boot) {
        printf("FAIL out of memory\n");
        return 1;
    }
    engine_save(e, boot);
    rom = engine_rom(e, &romSize);
    index = g64_rombanks_build_game(rom, romSize);
    if (!index) {
        printf("FAIL out of memory\n");
        return 1;
    }
    {
        G64RomBanksStats st;
        static unsigned own[256];
        int count = G64GameObjectIds(own, 256), k, missing = 0, absent = 0;
        g64_rombanks_stats(index, &st);
        if (!g64_rombanks_complete(&st)) {
            printf("FAIL the index of the ROM's banks isn't complete\n");
            failed++;
        }
        /* An id that the ROM lacks must be 0 or in the bank check's list, and every id of the list must be one. */
        for (k = 0; k < count && k < 256; k++)
            if (!g64_rombanks_has_object_id(index, own[k])) {
                int j, listed = own[k] == 0;
                for (j = 0; j < G64_MODELS_GAME_IDS; j++)
                    listed |= g64_models_game_ids[j] == own[k];
                if (!listed) {
                    printf("FAIL the game finds object %08x (name %d) by its own name, and neither the ROM nor the "
                           "bank check's list has it\n", own[k], k);
                    missing++;
                }
                absent++;
            }
        for (k = 0; k < G64_MODELS_GAME_IDS; k++) {
            int j, named = 0;
            for (j = 0; j < count && j < 256; j++)
                named |= own[j] == g64_models_game_ids[k];
            if (!named || g64_rombanks_has_object_id(index, g64_models_game_ids[k])) {
                printf("FAIL the bank check's list has %08x, which isn't a name of the game's that the ROM lacks\n",
                       g64_models_game_ids[k]);
                missing++;
            }
        }
        failed += missing;
        printf("%s the game finds %d objects by its own names; the %d that the ROM lacks are 0 or in the bank check's "
               "list\n", missing ? "FAIL" : "ok  ", count, absent);
    }
    /* The online program asks G64LevelBankNumbers right after power-on, before any level (netplay.c). */
    {
        int same = 1;
        engine_load(e, boot);
        for (level = 0; level < LEVELS; level++) {
            unsigned game[G64_LEVEL_BANKS_MAX], table[G64_LEVEL_BANKS_MAX];
            int n = G64LevelBankNumbers(level, game, G64_LEVEL_BANKS_MAX);
            same &= n > 0 && n == g64_level_object_banks(level, table, G64_LEVEL_BANKS_MAX) &&
                    memcmp(game, table, (size_t)n * sizeof game[0]) == 0;
        }
        printf("%s right after power-on, G64LevelBankNumbers gives the table's banks for every level\n",
               same ? "ok  " : "FAIL");
        failed += !same;
    }
    for (level = 0; level < LEVELS; level++) {
        size_t at = 0, size = 0, bad = 0;
        const char *why;
        int k, same = 1;
        engine_load(e, boot);
        g64ParseLogOn = 1;
        g64ParseLogCount = 0;
        port_game_start_level(level);
        for (k = 0; k < 5; k++) {
            EngineInput in;
            memset(&in, 0, sizeof in);
            engine_step(e, &in);
        }
        why = g64_pack_level_from_rom(rom, romSize, level, &at, &size);
        if (why) {
            printf("FAIL level %d: %s\n", level, why);
            failed++;
            continue;
        }
        seenCount = 0;
        why = g64_lev_check(rom + at, size, note, NULL, &bad);
        if (why) {
            printf("FAIL level %d: the check refuses the game's own level at offset %lu: %s\n", level,
                   (unsigned long)bad, why);
            failed++;
            continue;
        }
        if (seenCount != g64ParseLogCount || seenCount > 8192)
            same = 0;
        for (k = 0; same && k < seenCount; k++)
            same = seen[k].offset == (size_t)g64ParseLog[k][0] && seen[k].token == g64ParseLog[k][1];
        if (!same) {
            for (k = 0; k < seenCount && k < g64ParseLogCount && k < 8192; k++)
                if (seen[k].offset != (size_t)g64ParseLog[k][0] || seen[k].token != g64ParseLog[k][1])
                    break;
            printf("FAIL level %d: the game read %d tokens and the check %d; they part at token %d\n", level,
                   g64ParseLogCount, seenCount, k);
            failed++;
            continue;
        }
        idCount = 0;
        why = g64_lev_object_ids(rom + at, size, note_id, NULL, &bad);
        if (why || idCount > 8192) {
            printf("FAIL level %d: the id walk: %s\n", level, why ? why : "too many ids");
            failed++;
            continue;
        }
        if (g64FindLogCount >= 8192 || !same_ids(level, rom + at, size) || !same_banks(level, index)) {
            failed++;
            continue;
        }
        {
            unsigned nums[G64_LEVEL_BANKS_MAX];
            g64_level_banks banks;
            int n = G64LevelBankNumbers(level, nums, G64_LEVEL_BANKS_MAX);
            banks.count = (unsigned)n;
            memcpy(banks.num, nums, sizeof nums);
            why = g64_pack_check_ids(rom + at, size, NULL, NULL, &banks, g64_rombanks_bank_has, index, &bad);
            if (why) {
                printf("FAIL level %d: the level's own file fails the id rule at byte %lu: %s\n", level,
                       (unsigned long)bad, why);
                failed++;
                continue;
            }
            {
                int rule = 0;
                why = rule_from_tables(level, rom + at, size, rom, romSize, &rule, &bad);
                if (why) {
                    printf("FAIL level %d: the level's own file fails the id rule with the committed tables: %s\n",
                           level, why);
                    failed++;
                    continue;
                }
            }
            printf("ok   level %d: %d tokens, the same as the game's parser read; %d object ids, the same as "
                   "FindObject was asked for; banks", level, seenCount, idCount);
            for (k = 0; k < n; k++)
                printf(" %u", nums[k]);
            printf("\n");
        }
    }
    /* How many of each id token the ROM's levels have. */
    printf("the id tokens in the ROM's levels:");
    {
        static const int idTokens[] = {98, 101, 141, 143, 144, 145, 146, 160, 188};
        int k;
        for (k = 0; k < 9; k++)
            printf(" %d:%d", idTokens[k], tokenUses[idTokens[k]]);
    }
    printf("\n");
    failed += edited_levels(e, boot, rom, romSize, index);
    g64_rombanks_free(index);
    printf("%s the level file check reads every level of the ROM as the game does\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(e);
    return failed != 0;
}
