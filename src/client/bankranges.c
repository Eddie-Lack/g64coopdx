/* The game's bank switch as a table (bankranges.h). */
#include "bankranges.h"

#include "rombanks.h"

static const G64BankRangeEntry *find(int kind, unsigned num) {
    unsigned i;
    if (kind != G64_BANK_TEXTURE && kind != G64_BANK_OBJECT)
        return NULL;
    for (i = 0; i < g64_bank_range_count[kind]; i++)
        if (g64_bank_ranges[kind][i].num == num)
            return &g64_bank_ranges[kind][i];
    return NULL;
}

int g64_bank_exists(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    *start = *end = 0;
    return find(kind, num) != NULL;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A symbol's address as port_set_rom_symbols reads it. "ori" puts in an unsigned low half, "addiu" a signed one.
 * Returns 0 when the code is past the ROM's end. */
static int symbol(const G64RomImage *r, const G64RomSymbol *s, uint32_t *value) {
    uint32_t hi, lo, imm;
    if (!s->hi) {
        *value = s->original;
        return 1;
    }
    if (r->size < 4 || s->hi > r->size - 4 || s->lo > r->size - 4)
        return 0;
    hi = be32(r->rom + s->hi);
    lo = be32(r->rom + s->lo);
    imm = lo & 0xFFFF;
    *value = ((hi & 0xFFFF) << 16) + ((lo >> 26) == 0x0D ? imm : (uint32_t)(int32_t)(int16_t)imm);
    return 1;
}

int g64_bank_rom_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    const G64RomImage *r = user;
    const G64BankRangeEntry *e = find(kind, num);
    uint32_t s, t;
    *start = *end = 0;
    if (!e || !r || !r->rom || !symbol(r, &e->start, &s) || !symbol(r, &e->end, &t))
        return 0;
    /* The addresses are the cartridge's device addresses. The ROM's offset is in their low 28 bits, as port_dma
       reads them. */
    *start = s & 0x0FFFFFFF;
    *end = t & 0x0FFFFFFF;
    return 1;
}

/* Each level's object banks, as LoadLevel (weemitt's game.c) loads them: 0, the world's bank before INTRO (level
   46), 1 (69 in FLYTHRU, level 44), and the level's own. 0 ends a row early. tests/rom/levcheck.c pins every row. */
static const uint8_t levelBanks[48][4] = {
    {0, 64, 1, 2}, {0, 64, 1, 3}, {0, 64, 1, 4}, {0, 64, 1, 5}, {0, 64, 1, 6}, {0, 64, 1, 7},
    {0, 64, 1, 8}, {0, 64, 1, 9}, {0, 64, 1, 10}, {0, 64, 1, 11}, {0, 12, 1, 13}, {0, 12, 1, 14},
    {0, 12, 1, 15}, {0, 12, 1, 17}, {0, 12, 1, 18}, {0, 19, 1, 20}, {0, 19, 1, 21}, {0, 19, 1, 23},
    {0, 19, 1, 25}, {0, 19, 1, 26}, {0, 38, 1, 39}, {0, 38, 1, 40}, {0, 38, 1, 43}, {0, 38, 1, 44},
    {0, 38, 1, 45}, {0, 46, 1, 47}, {0, 46, 1, 49}, {0, 46, 1, 52}, {0, 46, 1, 53}, {0, 46, 1, 54},
    {0, 55, 1, 56}, {0, 55, 1, 58}, {0, 55, 1, 61}, {0, 55, 1, 62}, {0, 55, 1, 63}, {0, 27, 1, 29},
    {0, 27, 1, 30}, {0, 27, 1, 31}, {0, 27, 1, 33}, {0, 27, 1, 71}, {0, 27, 1, 33}, {0, 27, 1, 35},
    {0, 64, 1, 67}, {0, 27, 1, 68}, {0, 64, 69, 9}, {0, 64, 1, 9}, {0, 1, 65}, {0, 1, 66},
};

int g64_level_object_banks(int num, unsigned *banks, int max) {
    int n = 0, k;
    if (num < 0 || num >= 48)
        return -1;
    /* Bank 0 comes first in every row. */
    for (k = 0; k < 4 && (k == 0 || levelBanks[num][k] != 0); k++) {
        if (n < max)
            banks[n] = levelBanks[num][k];
        n++;
    }
    return n;
}

/* Each level's texture banks besides the font's (10) and GENERIC (2), as LoadLevel loads them in an online game: the
   world's bank (12 in PRESENT, level 43), the cameo bank (11) from INTRO (level 46) on, CKBONUS (15) in the carnival's
   bonus level (19), and FLYTHRU's (13) in FLYTHRU (level 44). 0 ends a row early. tests/rom/packbanks.c pins every
   row against G64LevelTextureBanks, and the rule that uses them against the game. */
static const uint8_t levelTextures[48][2] = {
    {9}, {9}, {9}, {9}, {9}, {9}, {9}, {9}, {9}, {9}, {3}, {3}, {3}, {3}, {3}, {4},
    {4}, {4}, {4}, {4, 15}, {6}, {6}, {6}, {6}, {6}, {7}, {7}, {7}, {7}, {7}, {8}, {8},
    {8}, {8}, {8}, {5}, {5}, {5}, {5}, {5}, {5}, {5}, {9}, {12}, {9, 13}, {9}, {11}, {11},
};

int g64_level_texture_banks(int num, unsigned *banks, int max) {
    unsigned list[4];
    int n = 0, k;
    if (num < 0 || num >= 48)
        return -1;
    list[n++] = 10;
    list[n++] = 2;
    for (k = 0; k < 2 && levelTextures[num][k] != 0; k++)
        list[n++] = levelTextures[num][k];
    for (k = 0; k < n && k < max; k++)
        banks[k] = list[k];
    return n;
}
