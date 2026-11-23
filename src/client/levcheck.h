/* A check of a level file's structure (docs/discovery/level-file-safety.md).
 *
 * The game's level parser (weemitt/src/game/parse.c) is the original game's and trusts the file: it reads tokens
 * until an end token, and each token's handler reads its fields without a bound. This check walks a file the same
 * way and says whether the parser, given this file, reads every token where the file's maker meant it, and stays
 * inside the file.
 *
 * What it holds a file to:
 *   - the file starts with its own size, then a name that ends with a zero byte
 *   - every token is one the parser handles, and the end token comes inside the file
 *   - every token's fields lie inside the file
 *   - a token whose handler uses a platform, a puzzle, an enemy, a vent, a landscape piece, a group of garibs, a
 *     cameo or a buzzer comes after the token that makes one, in this file: the parser keeps those pointers from
 *     the level before
 *   - no token takes the short form that the parser has for it. The parser reads some tokens' fields only in some
 *     states (an ORBIT after two path points, a TELEPORT to a tag that no platform has yet, and others), and then
 *     reads the rest of the fields as tokens
 *   - a ball start's place, read as tokens, holds no token: the parser skips the place in some states of the save
 *   - the game's lists of a fixed size are not overrun: 30 enemies, 30, 10 and 30 instructions of one enemy, 6
 *     patterns of a vent or a buzzer, 20 instructions of a cameo, 10 rope bridges of 1 to 100 planks
 *   - an axis is 0, 1 or 2; an enemy's, a garib's and a pick-up's type, a sound's number, a path's start point and
 *     a puzzle variable's number are ones the game has
 *
 * What it does not do: check the other values (level-file-safety.md lists them), or whether the level's objects
 * are in the banks of the level whose place it takes. A file that passes can still make the game read or write
 * outside its memory. A level file that comes from a host must pass this check, and the player is asked first and
 * told to take a pack only from a host they trust.
 *
 * The check's token lengths are compared with the game's own parser on every level of the ROM
 * (tests/rom/levcheck.c). */
#ifndef G64_LEVCHECK_H
#define G64_LEVCHECK_H

#include <stddef.h>
#include <stdint.h>

/* Called for each token, the end token too, with its offset in the file. */
typedef void (*g64_lev_token_fn)(void *user, size_t offset, int token);

/* Walks a level file. Returns NULL when the structure is as above, or a message; at is then the offset of the
 * token that was refused (or of the fault in the file's start). fn may be NULL. Every byte of lev is untrusted. */
const char *g64_lev_check(const uint8_t *lev, size_t n, g64_lev_token_fn fn, void *user, size_t *at);

#endif
