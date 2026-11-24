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
#include "bullet.h"

extern char message[];
const char *ObjectBankRange(u32 num, u32 *start, u32 *end);

/* game.c's type of the game's table of worlds. */
typedef struct WorldInfo {
    /* 0x00 */ s8 objBank;
    /* 0x01 */ s8 texBank;
    /* 0x02 */ s8 firstLevel;
    /* 0x03 */ u8 secondLevel;
    /* 0x04 */ s8 lastLevel;
    /* 0x05 */ u8 extraLevels[0x3];
    /* 0x08 */ char backdrop[0x10];
} WorldInfo; /* size = 0x18 */
extern WorldInfo worldInfo[];
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

/* The object banks that LoadLevel (game.c) loads for level num before the pack's, in the order it loads them, and so
   in the order of the slots they take: bank 0, which FreeLevel2 loads before every level, the world's bank for a
   level before INTRO, bank 1 (69 in FLYTHRU), and the level's own bank. The level parser finds an object only in
   these and the pack's. The numbers come from the game's tables levelInfo and worldInfo. Writes at most max numbers
   to banks and returns how many there are, or -1 for a level that is not 0 to 47 or a number that the game's bank
   switch does not have. The client checks a level file's object ids with these (src/client/pack.c). */
s32 G64LevelBankNumbers(s32 num, u32 *banks, s32 max) {
    u32 list[4];
    u32 start, end;
    s32 n = 0, i;

    if (num < 0 || num > OUTRO) {
        return -1;
    }
    list[n++] = 0;
    if (num < INTRO) {
        list[n++] = (u32)worldInfo[LevelToWorld((s8)num)].objBank;
    }
    list[n++] = num == FLYTHRU ? 69 : 1;
    list[n++] = (u32)levelInfo[num].objBank;
    for (i = 0; i < n; i++) {
        if (ObjectBankRange(list[i], &start, &end) == NULL) {
            return -1;
        }
        if (i < max) {
            banks[i] = list[i];
        }
    }
    return n;
}

/* For tests: the ids of objects in object slot slot, as FindObject reads them. Writes at most max ids and returns how
   many the slot has, or -1 for an empty slot. */
s32 G64ObjectSlotIds(s32 slot, u32 *ids, s32 max) {
    s32 i;

    if (slot < 0 || slot >= 5 || objectBanks[slot].data == NULL) {
        return -1;
    }
    for (i = 0; i < objectBanks[slot].count && i < max; i++) {
        ids[i] = objectBanks[slot].entries[i].crc;
    }
    return objectBanks[slot].count;
}

extern PTR32(char) nmeObjectName[];
extern PTR32(char) collectableFileName[];
u32 UpdateCRC(char *ptr);

/* For tests: the ids of the objects the game finds by a name of its own, not by an id from the level file: each
   enemy's and bullet's (nmeObjectName, with the hub chicken's three names), each pick-up's and garib's
   (collectableFileName), each standard bullet's without sprites (standardBullets, as InitObjects reads them), and the
   names in the game's code (InitActor, FindObject and initFGObject with a fixed name). Writes at most max ids and
   returns how many there are. */
s32 G64GameObjectIds(u32 *ids, s32 max) {
    static const char *const more[] = {
        "gball.ndo",      "boxingg.ndo",  "goodglov.ndo",  "hoop.ndo",      "robot.ndo",     "crystalball.ndo",
        "ballbit.ndo",    "snowbit.ndo",  "boulderbit.ndo", "frag.ndo",     "explosion.ndo", "nmefrog.ndo",
        "plaster.ndo",    "klospie.ndo",  "spring.ndo",    "elecplat.ndo",  "panelback.ndo", "fadeplane.ndo",
        "fadestar.ndo",   "bomb.ndo",     "newwave.ndo",   "titleboxb.ndo", "titleboxbf.ndo",
    };
    char name[64];
    s32 n = 0, k, c;

    for (k = 0; k < 59; k++) {
        for (c = 0; c < (k == HUBCHICKEN ? 3 : 1); c++) {
            snprintf(name, sizeof name, "%s", (char *)nmeObjectName[k]);
            if (k == HUBCHICKEN && strlen(name) > 10) {
                name[10] = (char)('1' + c);
            }
            if (n < max) {
                ids[n] = UpdateCRC(name);
            }
            n++;
        }
    }
    for (k = 0; k < 15; k++) {
        if (n < max) {
            ids[n] = UpdateCRC((char *)collectableFileName[k]);
        }
        n++;
    }
    for (k = 0; k < 30; k++) {
        if (standardBullets[k].numSprites == 0) {
            snprintf(name, sizeof name, "%.16s", standardBullets[k].name);
            if (n < max) {
                ids[n] = UpdateCRC(name);
            }
            n++;
        }
    }
    for (k = 0; k < (s32)(sizeof more / sizeof more[0]); k++) {
        snprintf(name, sizeof name, "%s", more[k]);
        if (n < max) {
            ids[n] = UpdateCRC(name);
        }
        n++;
    }
    return n;
}

PORT_C_END
