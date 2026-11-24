/* The rules for a pack's object ids and for its retail object banks' sprites, for a program that has not read the
 * game's tables yet: the pack tool, and the online host before it offers its pack (docs/discovery/model-banks.md,
 * "The level's object ids" and "Retail banks"). */
#ifndef G64_PACKROM_H
#define G64_PACKROM_H

#include <stddef.h>
#include <stdint.h>

#include "pack.h"
#include "rombanks.h"

/* The rule for the retail object banks that a pack's models.bin names. The game looks up each sprite's texture when it
 * loads such a bank, and it stops the level as unplayable when one is not found (G64LoadPackBanks in weemitt's
 * g64packbanks.c). So every texture id that a sprite of the bank names, at any depth and in every object of the bank,
 * must be in one of these texture banks: the font's, GENERIC, the level's own (g64_level_texture_banks), and the
 * retail texture banks that models.bin names. The rule covers objects that the level never places too, as the game
 * does: the game can find an object of the bank by a name of its own. Returns NULL, or a message and the number of
 * the object bank at fault in bank. */
const char *g64_pack_check_retail_sprites(int level, const G64Models *info, const G64RomBanks *index, unsigned *bank);

/* g64_pack_check_level_ids on the player's ROM, then g64_pack_check_retail_sprites. The level's banks come from the table of bankranges.h. The ROM's
 * banks come from the ranges in its code (g64_bank_rom_range). The pack's level file must pass the strict check
 * (g64_pack_check_strict). Returns NULL when every id is in a bank the level will have and the retail object banks
 * have their sprites' textures. Otherwise it returns a message. For a refused id, *rule is 1 and at holds the offset
 * of its token in the level file. For a retail object bank without its sprites' textures, *rule is 2 and at holds
 * the bank's number. For a ROM whose banks cannot all be read, a level outside the table, or no memory, *rule is 0. */
const char *g64_pack_check_ids_rom(const g64_pack *p, const uint8_t *rom, size_t romSize, int *rule, size_t *at);

#endif
