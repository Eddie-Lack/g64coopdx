/* g64coopdx: a level pack's own models (the repository's docs/discovery/model-banks.md, "Loading").

   A pack may carry one texture bank and one object bank of its own. The client puts them after the ROM's own bytes
   in the ROM buffer, as it does the pack's level file, and has checked them in full (src/client/bankcheck.c). The
   game loads them into free slots of its five texture and five object banks, right after the level's own object
   bank. The level parser runs later, so the level file finds the pack's objects as it finds the level's own.

   The slots are freed with the level's own banks (FreeLevel2 in game.c, and frontend.c), and the heap is set up
   again after that. Nothing here keeps a pointer: the statics hold ROM offsets, sizes and counts only.

   The ball's and the hand's names: RestoreDrawList (src/port/loaders.c) points the game's ball and hand texture
   words at a display list of an object named gball, cball, GShinL, GShinR or gbl_bt. The bank check refuses those
   names in a pack's own bank, so the pack's load needs no flag to turn that off. Retail banks, which hold those
   names, come later. */
PORT_C_BEGIN
#include "common.h"
#include "game.h"
#include "jalloc.h"
#include "banks.h"
#include "g64players.h"

extern char message[];
void LoadTextureBankAt(u32 num, u32 start, u32 end);
void LoadObjectBankAt(u32 num, u32 start, u32 end);

/* The bank number the loaders get for a pack's bank. It is neither the font bank, which has its own buffer, nor
   object bank 0, which also sets up the fade overlay. */
#define PACK_BANK 0xFFFFFFFFu

/* Where the pack's two banks are: offsets from the start of the ROM buffer, and sizes. Both sizes are 0 when the
   pack has no models. Every machine of a session has the same pack, so these are the same in every game. */
u32 g64PackTexRom, g64PackTexSize, g64PackObjRom, g64PackObjSize;

/* The game's heap where the pack's banks would load, at the last level start: the free bytes as the allocator
   counts them, the free bytes between the lowest static block and the highest dynamic block, the blocks still free,
   and the last static block's number. For tests. */
s32 g64PackHeapFree, g64PackHeapGap, g64PackHeapBlocks, g64PackHeapLastStatic;
/* The slots the pack's banks took at the last level start, or -1. */
static s32 packTexSlot = -1, packObjSlot = -1;

void G64SetPackModels(u32 texRom, u32 texSize, u32 objRom, u32 objSize) {
    if (texSize == 0 || objSize == 0) {
        texRom = texSize = objRom = objSize = 0;
    }
    g64PackTexRom = texRom;
    g64PackTexSize = texSize;
    g64PackObjRom = objRom;
    g64PackObjSize = objSize;
}

/* The free bytes between the static blocks, which grow down from the heap's top, and the dynamic blocks, which grow
   up from its start. The allocator's own test counts freed blocks inside both, so it can say more. */
static s32 HeapGap(void) {
    JallocControl *jc = &jallocControl;
    u32 low = jc->heapStart + jc->heapSize;
    u32 high = jc->heapStart;

    if (jc->lastStatic >= 0) {
        low = jc->blocks[jc->lastStatic].addr;
    }
    if (jc->firstDynamic < JALLOC_NUM_BLOCKS) {
        high = JallocDynamicBlock(jc->firstDynamic)->addr + JallocDynamicBlock(jc->firstDynamic)->size;
    }
    return low > high ? (s32)(low - high) : 0;
}

static s32 FreeSlot(s32 objects) {
    s32 i;

    /* Texture slot 0 is the font's. The loader starts its search at slot 0 for every other bank too. */
    for (i = 0; i < 5; i++) {
        if (objects ? objectBanks[i].data == NULL : textureBanks[i].data == 0) {
            return i;
        }
    }
    return -1;
}

void G64LoadPackBanks(s32 num) {
    JallocControl *jc = &jallocControl;

    g64PackHeapFree = jc->heapSize - jc->staticUsed - jc->dynamicUsed;
    g64PackHeapGap = HeapGap();
    g64PackHeapBlocks = JallocFreeBlocks();
    g64PackHeapLastStatic = jc->lastStatic;
    packTexSlot = packObjSlot = -1;
    if (num != g64PackLevel || g64PackTexSize == 0 || g64PackObjSize == 0 || gameInfo.mode == 13) {
        return;
    }
    /* The loaders take a slot only after they have loaded the bank, and their search for one has no bound. */
    if (FreeSlot(0) < 0 || FreeSlot(1) < 0) {
        G64Unplayable("NO BANK SLOT");
        return;
    }
    packTexSlot = FreeSlot(0);
    packObjSlot = FreeSlot(1);
    sprintf(message, "%s", "PACK_TEX_BANK");
    LoadTextureBankAt(PACK_BANK, g64PackTexRom, g64PackTexRom + g64PackTexSize);
    sprintf(message, "%s", "PACK_OBJ_BANK");
    LoadObjectBankAt(PACK_BANK, g64PackObjRom, g64PackObjRom + g64PackObjSize);
}

/* For tests: the pack's loaded bank of a kind (0 textures, 1 objects) in this game's memory, and its address as the
   game stores it, or NULL when it is not loaded. */
const void *G64PackBankLoaded(s32 objects, u32 *word) {
    s32 slot = objects ? packObjSlot : packTexSlot;

    if (slot < 0) {
        return NULL;
    }
    if (objects) {
        *word = PORT_P2W(objectBanks[slot].data);
        return (const void *)objectBanks[slot].data;
    }
    *word = (u32)textureBanks[slot].data;
    return PORT_W2P(void, textureBanks[slot].data);
}

PORT_C_END
