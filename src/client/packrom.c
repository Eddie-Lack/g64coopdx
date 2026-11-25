/* The rules for a pack's object ids and its retail object banks' sprites, with the committed tables (packrom.h). */
#include "packrom.h"

#include "bankranges.h"
#include "rombanks.h"

const char *g64_pack_check_retail_sprites(int level, const G64Models *info, const G64RomBanks *index, unsigned *bank) {
    unsigned tex[4 + G64_MODELS_RETAIL_MAX];
    uint32_t ids[4096];
    int nt, i, k;
    size_t n, j;
    *bank = 0;
    if (!info || info->numObj == 0)
        return NULL;
    nt = g64_level_texture_banks(level, tex, 4);
    if (nt < 1 || nt > 4)
        return "the table of the levels' texture banks has no row for the pack's level";
    for (i = 0; i < info->numTex && i < G64_MODELS_RETAIL_MAX; i++)
        tex[nt++] = info->tex[i];
    for (i = 0; i < info->numObj && i < G64_MODELS_RETAIL_MAX; i++) {
        n = g64_rombanks_sprite_textures(index, info->obj[i], ids, sizeof ids / sizeof ids[0]);
        if (n > sizeof ids / sizeof ids[0]) {
            *bank = info->obj[i];
            return "a retail object bank that the models name has more sprite textures than the check can hold";
        }
        for (j = 0; j < n; j++) {
            int found = 0;
            for (k = 0; k < nt && !found; k++)
                found = g64_rombanks_texture_bank_has(index, tex[k], ids[j]);
            if (!found) {
                *bank = info->obj[i];
                return "the models name a retail object bank without the texture banks of its sprites: a sprite's "
                       "texture is in no texture bank the level has, nor in the retail texture banks the models name";
            }
        }
    }
    return NULL;
}

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
        return "the ROM's banks couldn't all be read: its code doesn't say where they are, or a bank is damaged";
    }
    why = g64_pack_check_level_ids(p, &banks, g64_rombanks_bank_has, index, at);
    *rule = why != NULL;
    if (!why && p->models) {
        unsigned bank;
        why = g64_pack_check_retail_sprites(p->level, &p->modelsInfo, index, &bank);
        if (why) {
            *rule = 2;
            *at = bank;
        }
    }
    g64_rombanks_free(index);
    return why;
}
