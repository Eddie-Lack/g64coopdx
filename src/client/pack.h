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
 *
 * The pack's id is the first 16 bytes of the BLAKE2b hash (32 bytes) of its canonical form: the text "g64pack 1",
 * a zero byte, then for pack.txt, level.lev and, if the pack has one, script.lua, in that order: the file's name, a
 * zero byte, its length (u32, little-endian) and its bytes. Every machine of a session must have the pack with the same id.
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
/* The most bytes of a manifest and of a level file. The game's largest level file is about 15 KB. */
#define G64_PACK_MANIFEST_MAX 1024
#define G64_PACK_LEVEL_MAX (1024 * 1024)
#define G64_PACK_SCRIPT_MAX (256 * 1024)

typedef struct {
    char name[G64_PACK_NAME + 1];
    int level;
    uint8_t id[G64_PACK_ID];
    uint8_t *lev; /* the level file, owned by the pack (g64_pack_free) */
    size_t levSize;
    uint8_t *script; /* the script's source, owned by the pack, or NULL */
    size_t scriptSize;
} g64_pack;

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
/* The pack's id from its files' bytes. script is NULL for a pack without one. */
void g64_pack_make_id(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                      const uint8_t *script, size_t scriptSize, uint8_t id[G64_PACK_ID]);
/* Loads the pack in folder dir. Returns 0, or -1 with a message in err. */
int g64_pack_load(const char *dir, g64_pack *out, char *err, size_t errSize);
void g64_pack_free(g64_pack *p);

#endif
