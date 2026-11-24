/* A level pack: a folder with a custom level that takes the place of one of the game's levels for a session
 * (docs/discovery/level-pack.md). It holds no game data: the level file places objects that the player's own ROM
 * has, by their ids.
 *
 * Files:
 *   pack.txt    the manifest: lines of key=value that end with a line feed alone, in any order, each key once,
 *               nothing else:
 *                 format=1
 *                 name=NAME      1 to 32 printable ASCII characters
 *                 level=N        the level whose place the pack's level takes (weemitt's enum Level, 0 to 47)
 *   level.lev   the level, in the game's own format, as Glovebox writes it. It starts with its size (u32, big-endian).
 *   script.lua  a script, as Lua source text (weemitt/src/port/g64lua.c). A pack may have none.
 *   models.bin  the pack's own models and the numbers of retail banks (docs/discovery/model-banks.md, "The file").
 *               A pack may have none.
 *
 * The pack's id is the first 16 bytes of the BLAKE2b hash (32 bytes) of its canonical form: the text "g64pack 1",
 * a zero byte, then for pack.txt, level.lev and, if the pack has them, script.lua and models.bin, in that order: the
 * file's name, a zero byte, its length (u32, little-endian) and its bytes. Every machine of a session must have the
 * pack with the same id. A pack without models.bin has the id it had before models.bin was part of the format.
 *
 * Every byte of a pack is untrusted: the manifest's parser takes only what is listed above, and the level file's
 * size is checked against its own first word. The game's level parser is the original game's and does not check
 * what it reads, so a pack that comes from another player must also pass the check of the level file's structure
 * (levcheck.h) before it is used. A pack from a folder the player chose is not held to that check. Every
 * models.bin passes the bank check (bankcheck.h) before the pack is read, from a folder too: the game's loaders
 * would write outside a bad bank. */
#ifndef G64_PACK_H
#define G64_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "bankcheck.h"

#define G64_PACK_NAME 32
#define G64_PACK_ID 16
#define G64_PACK_LEVELS 48
/* The most bytes of a manifest and of a level file. The game's largest level file is about 15 KB. */
#define G64_PACK_MANIFEST_MAX 1024
#define G64_PACK_LEVEL_MAX (1024 * 1024)
#define G64_PACK_SCRIPT_MAX (256 * 1024)
/* The most bytes of models.bin: the two banks and the largest header. */
#define G64_PACK_MODELS_MAX (G64_MODELS_BANKS_MAX + G64_MODELS_HEADER_MAX)

typedef struct {
    char name[G64_PACK_NAME + 1];
    int level;
    uint8_t id[G64_PACK_ID];
    uint8_t *manifest; /* pack.txt's bytes as they were read, owned by the pack: the id is made from them */
    size_t manifestSize;
    uint8_t *lev; /* the level file, owned by the pack (g64_pack_free) */
    size_t levSize;
    uint8_t *script; /* the script's source, owned by the pack, or NULL */
    size_t scriptSize;
    uint8_t *models; /* models.bin, owned by the pack, or NULL */
    size_t modelsSize;
    G64Models modelsInfo; /* what models.bin's check found: its banks' places in models, and the retail numbers */
} g64_pack;

/* What the check of models.bin may ask about the player's game. exists says which retail bank numbers the game's
 * bank switch has (bankcheck.h): G64BankRom where the game is linked (g64_rombanks_game_range), the table of
 * bankranges.h elsewhere. With exists NULL, no retail number is allowed. rom is the index of the player's ROM's
 * banks, or NULL: with it, models.bin must also pass g64_models_check_rom. A NULL g64_pack_banks means both NULL. */
typedef struct {
    g64_bank_range_fn exists;
    void *user;
    const G64RomBanks *rom;
} g64_pack_banks;

/* Reads a manifest's bytes into name and level. Returns 0, or -1 for a manifest that is not exactly as pack.h
 * describes. The fuzz target feeds it random bytes. */
int g64_pack_parse_manifest(const uint8_t *d, size_t n, g64_pack *out);
/* 0 when lev is a level file by its outside: at least a size and a name, and its first word is its length. */
int g64_pack_check_level(const uint8_t *lev, size_t n);
/* 0 when script may be a script by its outside: 1 byte to G64_PACK_SCRIPT_MAX, with no zero byte, and not Lua's
 * compiled form, which starts with an escape character. The game takes source text only. */
int g64_pack_check_script(const uint8_t *script, size_t n);
/* Finds level number num's file in a ROM: the game's own, or one that Glovebox built with an edited level. The
 * level files lie one after another, each starting with its size; the game's code says where they start and end.
 * Returns NULL and sets at (the file's offset in rom) and levSize, or a message. Every byte of the ROM is untrusted
 * here: the tool that makes packs reads it from a file the user names (tools/g64pack.c). */
const char *g64_pack_level_from_rom(const uint8_t *rom, size_t size, int num, size_t *at, size_t *levSize);
/* The pack's id from its files' bytes. script and models are NULL for a pack without them. */
void g64_pack_make_id(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                      const uint8_t *script, size_t scriptSize, const uint8_t *models, size_t modelsSize,
                      uint8_t id[G64_PACK_ID]);
/* The check of models.bin as a pack reads it: g64_models_check, and with banks->rom also g64_models_check_rom.
 * Returns NULL and fills info, or a message with the offset in the file in at. */
const char *g64_pack_check_models(const uint8_t *models, size_t n, const g64_pack_banks *banks, G64Models *info,
                                  size_t *at);
/* A pack from its files' bytes, which it copies. script and models are NULL for a pack without them. models.bin
 * must pass g64_pack_check_models with banks. With strict, the level file must also pass the check of its structure
 * (levcheck.h): a pack that came from another player is read this way. Returns 0, or -1 with a message in err.
 * Every byte is untrusted. */
int g64_pack_from_bytes(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                        const uint8_t *script, size_t scriptSize, const uint8_t *models, size_t modelsSize,
                        const g64_pack_banks *banks, int strict, g64_pack *out, char *err, size_t errSize);
/* Loads the pack in folder dir, without the check of the level file's structure. Its models.bin, if it has one,
 * passes the bank check with banks all the same. Returns 0, or -1 with a message in err. */
int g64_pack_load(const char *dir, const g64_pack_banks *banks, g64_pack *out, char *err, size_t errSize);
/* The check of a pack that may come from another player: its level is an ordinary one, and the level file's
 * structure is as the game reads it (levcheck.h). NULL when it passes, or a message, with the place in at (at may
 * be NULL). */
const char *g64_pack_check_strict(const g64_pack *p, size_t *at);
/* The number of the level in rom whose file has the same bytes as the pack's, or -1. Such a pack holds game data: it
 * is a copy of a level of the ROM, and must not be sent to anyone. */
int g64_pack_rom_level(const g64_pack *p, const uint8_t *rom, size_t size);
/* Where the game reads a pack's files: in one buffer, after the ROM's romSize bytes. The level file comes first, then
 * the script, then models.bin's two banks, the texture bank first, from an offset that is a multiple of 8. A part
 * the pack lacks has size 0. size is the whole buffer's. */
typedef struct {
    size_t levAt, scriptAt, texAt, texSize, objAt, objSize, size;
} g64_pack_layout;
void g64_pack_layout_of(const g64_pack *p, size_t romSize, g64_pack_layout *l);
/* Writes the pack's parts into buf, which holds l->size bytes and the ROM's bytes before l->levAt. The bytes between
 * the parts become 0. */
void g64_pack_layout_fill(const g64_pack *p, const g64_pack_layout *l, uint8_t *buf);
/* Packs that came from a host are kept in base/packs/ID, where ID is the pack's id as 32 hexadecimal digits. No
 * text from the network is part of a path. g64_pack_cache_dir writes that folder's path to out; it returns -1 when
 * the path does not fit. g64_pack_cache_save writes a pack there, and removes a script.lua or models.bin that the
 * pack does not have. g64_pack_cache_load reads the pack with that id back: the files must have that id, pass the
 * strict check and the bank check with banks again, since anything may have changed them. */
int g64_pack_cache_dir(const char *base, const uint8_t id[G64_PACK_ID], char *out, size_t outSize);
int g64_pack_cache_save(const char *base, const g64_pack *p, char *err, size_t errSize);
int g64_pack_cache_load(const char *base, const uint8_t id[G64_PACK_ID], const g64_pack_banks *banks, g64_pack *out,
                        char *err, size_t errSize);
void g64_pack_free(g64_pack *p);

#endif
