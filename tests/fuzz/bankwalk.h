/* The shadow walk: a copy of the game's loaders for a texture bank and an object bank, which stops at any access
 * outside the bank (docs/discovery/model-banks.md, "The check").
 *
 * It copies the reads and writes of weemitt's port_convert_texture_bank, LoadTextureBankAt, FindTexture,
 * port_convert_object_bank, LoadObjectBankAt, RestoreObjectPointers and RestoreDrawList, on a copy of each bank. It
 * then reads what TransformAndDrawObject and the renderer read from the loaded banks. It shares no code with the bank
 * check (src/client/bankcheck.c).
 *
 * It also stops when the loaders would change one word twice (a word that two structures share), when the game would
 * write through a pointer the loaders do not fix, and when an object tree would overrun the game's matrix stack or
 * parentScaleStack.
 *
 * The bank fuzz target (tests/fuzz/bankcheck_fuzz.c) runs it on every bank the check accepts. The test
 * tests/net/bankcheck_test.c runs it on banks the check refuses and needs it to stop. */
#ifndef G64_BANKWALK_H
#define G64_BANKWALK_H

#include <stddef.h>
#include <stdint.h>

/* Walks the two banks as the game loads and draws them. Returns NULL, or what stopped the walk. at is then the
 * offset of the access in its bank, and bank is 0 for the texture bank and 1 for the object bank. Neither buffer is
 * changed. Not reentrant. */
const char *g64_bank_shadow(const uint8_t *tex, size_t tn, const uint8_t *obj, size_t on, size_t *at, int *bank);

/* Each bank's bytes after the port's converter and after the loaders, in the host's byte order. A fixed pointer in
 * the object bank is its offset in the bank plus G64_SHADOW_BASE, and a pointer into the texture bank is its offset
 * there plus G64_SHADOW_TEXBASE. Each buffer has room for its bank. */
#define G64_SHADOW_BASE 0x40000000u
#define G64_SHADOW_TEXBASE 0x20000000u
typedef struct {
    uint8_t *texConverted, *texLoaded, *objConverted, *objLoaded;
} G64BankImages;

/* The walk of g64_bank_shadow, which also fills out when it is not NULL. A buffer is filled only when the walk
 * reaches that point. The ROM test tests/rom/bankwalk.c compares the images with the game's own loaders. */
const char *g64_bank_shadow_images(const uint8_t *tex, size_t tn, const uint8_t *obj, size_t on, G64BankImages *out,
                                   size_t *at, int *bank);

#endif
