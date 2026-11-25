/* An index of the ROM's banks (docs/discovery/model-banks.md, "The check").
 *
 * A bank is one block of the ROM: a texture bank holds pictures, and an object bank holds objects that use them.
 * The index is built once from the player's ROM. It holds:
 *   - every object id: the crc of each entry in an object bank's directory, and which banks have it
 *   - every texture id: the crc of each texture record in a texture bank, and which banks have it
 *   - the texture ids named by the sprites of each object bank's objects
 *   - a 16-byte BLAKE2b hash of each texture's texels, each mesh's vertex array and each G_VTX block
 * The bank check uses it to refuse a pack's model that has a ROM id or holds the ROM's own data. The index never
 * leaves the process.
 *
 * What each hash covers, in the ROM's big-endian bytes:
 *   - texels: width * height * bits per texel / 8 bytes from the record's byte 36. The bits per texel are 4, 8, 16
 *     or 32 for siz 0 to 3. A texture whose texels don't fit before its palette or the record's end isn't hashed.
 *   - a vertex array: numVerts * 12 bytes at the mesh's verts
 *   - a G_VTX block: n * 16 bytes at the command's second word, n = (w0 >> 10) & 0x3F
 *
 * The walk reads the banks as the game's loaders do (weemitt's LoadTextureBankAt, LoadObjectBankAt,
 * RestoreObjectPointers and RestoreDrawList), on the ROM's bytes, which it never writes. Every offset is checked. A
 * bank that would make the walk leave its bytes is skipped whole, and counted. So is an object bank whose hashes would
 * cover more than four times its size and 1 MiB. Each object and each mesh is walked once. FLA2 banks are
 * decompressed with fla2.h. */
#ifndef G64_ROMBANKS_H
#define G64_ROMBANKS_H

#include <stddef.h>
#include <stdint.h>

enum { G64_BANK_TEXTURE = 0, G64_BANK_OBJECT = 1 };

/* Gives the ROM range of bank num of a kind: returns 1 and the offsets of its first byte and of the byte past its
 * end, or 0 when the kind has no bank of that number. */
typedef int (*g64_bank_range_fn)(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end);

typedef struct G64RomBanks G64RomBanks;

typedef struct {
    unsigned banks[2];   /* the banks indexed, by kind */
    unsigned skipped[2]; /* the banks skipped: a range outside the ROM, a bad FLA2 stream, or a bad structure */
    unsigned compressed; /* the indexed banks that were FLA2 streams */
    unsigned objectIds, textureIds;
    unsigned texels, verts, vtx; /* the distinct hashes of each kind */
    unsigned unhashedTextures;   /* textures whose texels don't fit in their record */
} G64RomBanksStats;

/* Builds the index from a ROM buffer, asking range for bank numbers 0 to 255 of each kind. Returns NULL when out of
 * memory. */
G64RomBanks *g64_rombanks_build(const uint8_t *rom, size_t romSize, g64_bank_range_fn range, void *user);

/* The same with the game's own bank ranges (G64BankRom). Only a program that links the game has it
 * (rombanks_game.c). */
G64RomBanks *g64_rombanks_build_game(const uint8_t *rom, size_t romSize);
/* The game's own bank ranges as a g64_bank_range_fn: G64BankRom, with ROM offsets. user isn't used. Only a program
 * that links the game has it (rombanks_game.c). */
int g64_rombanks_game_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end);

void g64_rombanks_free(G64RomBanks *b);
void g64_rombanks_stats(const G64RomBanks *b, G64RomBanksStats *s);
/* 1 when the index can check a pack's models: it has banks of both kinds and skipped none. A bank it skipped holds
 * ids and data that the check wouldn't see. The pack tool and the online program both refuse an index for which
 * this is 0. */
int g64_rombanks_complete(const G64RomBanksStats *s);

int g64_rombanks_has_object_id(const G64RomBanks *b, uint32_t id);
int g64_rombanks_has_texture_id(const G64RomBanks *b, uint32_t id);
/* 1 when object bank num's directory has the id. A bank that was skipped has no ids. */
int g64_rombanks_bank_has_object(const G64RomBanks *b, unsigned num, uint32_t id);
/* The number of distinct ids in object bank num's directory. */
size_t g64_rombanks_bank_object_count(const G64RomBanks *b, unsigned num);
/* The same as a g64_bank_has_fn (pack.h): user is the index. */
int g64_rombanks_bank_has(void *user, unsigned num, uint32_t id);
/* 1 when texture bank num has a record with the id. */
int g64_rombanks_texture_bank_has(const G64RomBanks *b, unsigned num, uint32_t id);
/* The distinct texture ids named by the sprites of object bank num's objects, at any depth of their trees, sorted.
 * Writes at most max ids and returns how many there are. The game looks each one up when it loads the bank. */
size_t g64_rombanks_sprite_textures(const G64RomBanks *b, unsigned num, uint32_t *ids, size_t max);
/* hash is 16 bytes. */
int g64_rombanks_has_texels_hash(const G64RomBanks *b, const uint8_t *hash);
int g64_rombanks_has_verts_hash(const G64RomBanks *b, const uint8_t *hash);
int g64_rombanks_has_vtx_hash(const G64RomBanks *b, const uint8_t *hash);

/* The hash the index keeps: the 16-byte BLAKE2b of n bytes. */
void g64_rombanks_hash(uint8_t hash[16], const uint8_t *p, size_t n);

/* The ids in the index, sorted, with no repeats. The arrays belong to the index. */
const uint32_t *g64_rombanks_object_ids(const G64RomBanks *b, size_t *count);
const uint32_t *g64_rombanks_texture_ids(const G64RomBanks *b, size_t *count);

#endif
