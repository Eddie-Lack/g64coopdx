/* The rule for a pack's object ids with the committed tables (packrom.h). */
#include "packrom.h"

#include "bankranges.h"
#include "rombanks.h"

const char *g64_pack_check_ids_rom(const g64_pack *p, const uint8_t *rom, size_t romSize, int *rule, size_t *at) {
    G64RomImage img;
    G64RomBanks *index;
    G64RomBanksStats st;
    g64_level_banks banks;
    const char *why;
    int n = g64_level_object_banks(p->level, banks.num, G64_LEVEL_BANKS_MAX);
    *rule = 0;
    *at = 0;
    if (n < 1 || n > G64_LEVEL_BANKS_MAX)
        return "the table of the levels' banks has no row for the pack's level";
    banks.count = (unsigned)n;
    img.rom = rom;
    img.size = romSize;
    index = g64_rombanks_build(rom, romSize, g64_bank_rom_range, &img);
    if (!index)
        return "out of memory";
    g64_rombanks_stats(index, &st);
    /* An index that skipped a bank would refuse ids that the bank has. */
    if (!g64_rombanks_complete(&st)) {
        g64_rombanks_free(index);
        return "the ROM's banks could not all be read: its code does not say where they are, or a bank is damaged";
    }
    why = g64_pack_check_level_ids(p, &banks, g64_rombanks_bank_has, index, at);
    g64_rombanks_free(index);
    *rule = why != NULL;
    return why;
}
