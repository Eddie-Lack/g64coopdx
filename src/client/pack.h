/* A level pack: a folder with a custom level that takes the place of one of the game's levels for a session
 * (docs/discovery/level-pack.md). It holds no game data: the level file places objects that the player's own ROM
 * has, by their ids.
 *
 * Files:
 *   pack.txt    the manifest: lines of key=value, in any order, each key once, nothing else:
 *                 format=1
 *                 name=NAME      1 to 32 printable ASCII characters
 *                 level=N        the level whose place the pack's level takes (weemitt's enum Level, 0 to 47)
 *   level.lev   the level, in the game's own format, as Glovebox writes it. It starts with its size (u32, big-endian).
 *
 * The pack's id is the first 16 bytes of the BLAKE2b hash (32 bytes) of its canonical form: the text "g64pack 1",
 * a zero byte, then for pack.txt and level.lev in that order: the file's name, a zero byte, its length (u32,
 * little-endian) and its bytes. Every machine of a session must have the pack with the same id.
 *
 * Every byte of a pack is untrusted: the manifest's parser takes only what is listed above, and the level file's
 * size is checked against its own first word. The game's level parser is the original game's and does not check
 * what it reads, so a pack that comes from another player must also pass a check of the level file before it is
 * used; until that check exists, packs come only from a folder the player chose. */
#ifndef G64_PACK_H
#define G64_PACK_H

#include <stddef.h>
#include <stdint.h>

#define G64_PACK_NAME 32
#define G64_PACK_ID 16
#define G64_PACK_LEVELS 48
/* The most bytes of a manifest and of a level file. The game's largest level file is about 150 KB. */
#define G64_PACK_MANIFEST_MAX 1024
#define G64_PACK_LEVEL_MAX (1024 * 1024)

typedef struct {
    char name[G64_PACK_NAME + 1];
    int level;
    uint8_t id[G64_PACK_ID];
    uint8_t *lev; /* the level file, owned by the pack (g64_pack_free) */
    size_t levSize;
} g64_pack;

/* Reads a manifest's bytes into name and level. Returns 0, or -1 for a manifest that is not exactly as pack.h
 * describes. The fuzz target feeds it random bytes. */
int g64_pack_parse_manifest(const uint8_t *d, size_t n, g64_pack *out);
/* 0 when lev is a level file by its outside: at least a size and a name, and its first word is its length. */
int g64_pack_check_level(const uint8_t *lev, size_t n);
/* The pack's id from its two files' bytes. */
void g64_pack_make_id(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                      uint8_t id[G64_PACK_ID]);
/* Loads the pack in folder dir. Returns 0, or -1 with a message in err. */
int g64_pack_load(const char *dir, g64_pack *out, char *err, size_t errSize);
void g64_pack_free(g64_pack *p);

#endif
