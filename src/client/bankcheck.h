/* The check of a pack's models.bin (docs/discovery/model-banks.md, "The file" and "The check").
 *
 * models.bin holds a header, then the pack's own texture bank and object bank. A bank is untrusted. The game's
 * loaders (weemitt's LoadTextureBankAt, LoadObjectBankAt, RestoreObjectPointers and RestoreDrawList) trust every
 * field of a bank: they follow its offsets and counts with no bound. This check reads the big-endian bytes as those
 * loaders do, and refuses anything it does not understand. Every byte of the file is untrusted.
 *
 * What it holds the file to, in short:
 *   - the header's sizes, flags and retail bank numbers are as model-banks.md says. A retail number must be one
 *     the game's bank switch has, and not the font or generic texture bank or object bank 0 or 1
 *   - neither bank starts with "FLA2"
 *   - the texture bank holds 1 to 1024 records of 16-bit RGBA as Glovebox writes them, then 4 zero bytes. Every
 *     record is used by the object bank
 *   - no object's id is 0 or the id of an enemy or a pick-up that the game finds by its own name
 *   - the object bank's directory, instances, objects, meshes, keys and display lists lie inside the bank, are
 *     aligned, and share no bytes. Every byte that no structure reaches is 0
 *   - every value the game uses as a count, an index or a size is in range, and every float of a key or a vertex is
 *     finite and bounded
 *   - the display lists hold only the commands Glovebox writes, and every texture load is in Glovebox's form
 *
 * Some rules allow only what Glovebox writes today. tests/fixtures/cube.models.bin is Glovebox's real output, and
 * tests/net/bankcheck_test.c pins those rules on it.
 *
 * The rules that need the player's ROM are apart: g64_models_check_rom. */
#ifndef G64_BANKCHECK_H
#define G64_BANKCHECK_H

#include <stddef.h>
#include <stdint.h>

#include "rombanks.h"

/* The most bytes of the two banks together, and the most retail banks of each kind. */
#define G64_MODELS_BANKS_MAX (4u << 20)
#define G64_MODELS_RETAIL_MAX 4
/* The largest header: 12 bytes and 8 numbers, rounded up to a multiple of 8. */
#define G64_MODELS_HEADER_MAX 24

/* The ids that no object of the pack's own bank may have, beyond 0 and the ROM's own (bankcheck.c). */
#define G64_MODELS_GAME_IDS 6
extern const uint32_t g64_models_game_ids[G64_MODELS_GAME_IDS];

/* What the header says. The offsets and sizes are in bytes of models.bin. */
typedef struct {
    uint32_t texAt, texSize; /* the pack's texture bank */
    uint32_t objAt, objSize; /* the pack's object bank */
    uint8_t flags;           /* bit 0: search the retail texture banks before the level's own */
    uint8_t numTex, numObj;  /* the retail bank numbers */
    uint8_t tex[G64_MODELS_RETAIL_MAX], obj[G64_MODELS_RETAIL_MAX];
    /* What the pack's banks hold: the object bank's directory entries, every object in their trees, and the
       texture records. All 0 when the file has no banks of its own. */
    unsigned numEntries, numObjects, numTextures;
} G64Models;

/* Checks models.bin. exists says whether the game's bank switch has a bank of a kind (G64_BANK_TEXTURE or
 * G64_BANK_OBJECT) and number. It returns 1 or 0, as G64BankRom does; the range it gives is not used. With exists
 * NULL, no retail number is allowed. Returns NULL when the file passes, and fills out when it is not NULL. Otherwise
 * returns a message, and at is the offset in the file of the byte, field or pointer at fault. */
const char *g64_models_check(const uint8_t *file, size_t n, g64_bank_range_fn exists, void *user, G64Models *out,
                             size_t *at);

/* The rules that need the player's ROM, on a file that passed g64_models_check. It refuses:
 *   - an object id or a texture id of the pack that the ROM has
 *   - a texture whose texels equal a texture's in the ROM, unless all its texels have one color
 *   - a mesh's vertex array of 64 bytes or more that equals one in the ROM
 *   - a G_VTX block of 64 bytes or more that equals one in the ROM
 * The hashes are those of rombanks.h, over the file's big-endian bytes. Returns NULL, or a message and the offset in
 * the file of the id or the data at fault. */
const char *g64_models_check_rom(const uint8_t *file, size_t n, const G64RomBanks *rom, size_t *at);

#endif
