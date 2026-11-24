/* The rule for a pack's object ids for a program that has not read the game's tables yet: the pack tool, and the
 * online host before it offers its pack (docs/discovery/model-banks.md, "The level's object ids"). */
#ifndef G64_PACKROM_H
#define G64_PACKROM_H

#include <stddef.h>
#include <stdint.h>

#include "pack.h"

/* g64_pack_check_level_ids on the player's ROM. The level's banks come from the table of bankranges.h. The ROM's
 * banks come from the ranges in its code (g64_bank_rom_range). The pack's level file must pass the strict check
 * (g64_pack_check_strict). Returns NULL when every id is in a bank the level will have. Otherwise it returns a
 * message. For a refused id, *rule is 1 and at holds the offset of its token in the level file. For a ROM whose banks
 * cannot all be read, a level outside the table, or no memory, *rule is 0. */
const char *g64_pack_check_ids_rom(const g64_pack *p, const uint8_t *rom, size_t romSize, int *rule, size_t *at);

#endif
