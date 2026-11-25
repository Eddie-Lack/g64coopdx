/* The game's bank switch as a table, for programs that do not link the game (the pack tool, the pack tests in
 * tests/net and the pack fuzz target), and for the online program before the game has read its ROM (netplay.c).
 * tools/gen_bankranges.py writes bankranges_table.c from weemitt's src/game/banks.c and gen/romsyms.c.
 *
 * The game knows a bank by its kind (G64_BANK_TEXTURE or G64_BANK_OBJECT, rombanks.h) and number. Each bank's range
 * in the ROM runs from one ROM symbol to another. A symbol's address is in the game's code, as a pair of
 * instructions that weemitt reads from the ROM at start-up (port_set_rom_symbols in weemitt/src/port/loaders.c). A
 * ROM that Glovebox built has its moved banks' addresses there. The table holds each symbol's address in the game's own ROM and
 * the offsets of its two instructions. It holds no bytes of the ROM.
 *
 * A program that links the game can ask the game's own switch instead (G64BankRom, rombanks_game.c), once the game has
 * powered on. tests/rom/rombanks.c compares the two. */
#ifndef G64_BANKRANGES_H
#define G64_BANKRANGES_H

#include <stddef.h>
#include <stdint.h>

#define G64_BANK_RANGES_MAX 64

typedef struct {
    uint32_t original; /* the address in the game's own ROM */
    uint32_t hi, lo;   /* the ROM offsets of the "lui" and of the "addiu" or "ori", or 0 */
} G64RomSymbol;

typedef struct {
    unsigned num;
    G64RomSymbol start, end;
} G64BankRangeEntry;

extern const G64BankRangeEntry g64_bank_ranges[2][G64_BANK_RANGES_MAX];
extern const unsigned g64_bank_range_count[2];

/* A ROM for g64_bank_rom_range. */
typedef struct {
    const uint8_t *rom;
    size_t size;
} G64RomImage;

/* A g64_bank_range_fn (rombanks.h) that says only whether the game's switch has a bank: it returns 1 or 0, and the
 * range 0 to 0. user is not used. */
int g64_bank_exists(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end);
/* A g64_bank_range_fn that reads the range from the ROM's code as weemitt does. user is a G64RomImage. It returns 0
 * for a number with no bank, and for a ROM too short to hold the code. The range is ROM offsets, as
 * g64_rombanks_build wants them. */
int g64_bank_rom_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end);

/* The object banks that the game loads for level num (0 to 47) before a pack's, in the order it loads them: the
 * numbers of G64LevelBankNumbers (weemitt's g64packbanks.c), which reads them from the game's tables. Writes at
 * most max numbers and returns how many there are, or -1 for another level. The numbers are the retail game's.
 * tests/rom/levcheck.c compares the table with the game's on the player's ROM, and with the banks LoadLevel loads. */
int g64_level_object_banks(int num, unsigned *banks, int max);
/* The same from the game itself (G64LevelBankNumbers). Only a program that links the game has it
 * (rombanks_game.c). */
int g64_level_object_banks_game(int num, unsigned *banks, int max);
/* The texture banks that the game has loaded for level num (0 to 47) when it loads a pack's banks: the font's (10),
 * GENERIC (2), then the level's own, as G64LevelTextureBanks (weemitt's g64packbanks.c) gives them. Writes at most
 * max numbers and returns how many there are, or -1 for another level. The numbers are the retail game's. */
int g64_level_texture_banks(int num, unsigned *banks, int max);

#endif
