/* The index of the ROM's banks with the game's own bank ranges (rombanks.h). This file links the game. */
#include "rombanks.h"

/* weemitt/src/game/g64players.c: kind 0 is a texture bank and 1 an object bank. */
int G64BankRom(int kind, unsigned num, unsigned *start, unsigned *end);

/* The game's ranges are device addresses. The ROM's offset is in their low 28 bits, as port_dma reads them. */
int g64_rombanks_game_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    unsigned s, e;

    (void)user;
    if (!G64BankRom(kind, num, &s, &e))
        return 0;
    *start = s & 0x0FFFFFFF;
    *end = e & 0x0FFFFFFF;
    return 1;
}

G64RomBanks *g64_rombanks_build_game(const uint8_t *rom, size_t romSize) {
    return g64_rombanks_build(rom, romSize, g64_rombanks_game_range, NULL);
}
