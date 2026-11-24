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
