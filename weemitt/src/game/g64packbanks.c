/* g64coopdx: a level pack's own models and the retail banks it names (the repository's
   docs/discovery/model-banks.md, "Loading").

   A pack may carry one texture bank and one object bank of its own. The client puts them after the ROM's own bytes
   in the ROM buffer, as it does the pack's level file, and has checked them in full (src/client/bankcheck.c). The
   game loads them into free slots of its five texture and five object banks, right after the level's own object
   bank. The level parser runs later, so the level file finds the pack's objects as it finds the level's own.

   A pack may also name up to 4 retail texture banks and 4 retail object banks by number. A retail bank is one of the
   ROM's own. They do not go into the slots. They go into one block on the game's heap, the table, with the swap of
   Glovebox's mod: the five slots are put aside and emptied, the game's own loader puts the bank in slot 0, the slot
   is copied into the table, and the five slots come back. FindTexture and FindObject (banks.c) search the table
   after the slots. With the header's first flag, FindTexture searches it before the slots too.

   The slots are freed with the level's own banks (FreeLevel2 in game.c, and frontend.c), and the heap is set up
   again after that. The table is dropped at the same places (G64DropPackBanks). The statics hold ROM offsets,
   sizes, bank numbers, the table's address as the game stores it, and the value of portHeapInits when the table
   was made. A lookup uses the table only while that value is current: JallocInit raises it, so a table on a heap
   that was set up again is never read.

   The ball's and the hand's names: RestoreDrawList (src/port/loaders.c) points the game's ball and hand texture
   words at a display list of an object named gball, cball, GShinL, GShinR or gbl_bt. The bank check refuses those
   names in a pack's own bank. Retail banks hold them: OUTRO has GShinL and GShinR, and FLYTHRU has gball. So
   RestoreDrawList skips those names while G64PackBankLoading says the banks here are loading. */
PORT_C_BEGIN
#include "common.h"
#include "game.h"
#include "jalloc.h"
#include "banks.h"
#include "g64players.h"
#include "bullet.h"
#include "texture.h"

extern char message[];
extern u32 portHeapInits;
extern s32 numCyclingTextures;
extern PTR32(Texture) cyclingTextures[20];
const char *TextureBankRange(u32 num, u32 *start, u32 *end);
const char *ObjectBankRange(u32 num, u32 *start, u32 *end);
void LoadTextureBank(u32 num);
void LoadObjectBank(u32 num);

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

/* The most retail banks of each kind that a pack may name (src/client/bankcheck.h, G64_MODELS_RETAIL_MAX). */
#define RETAIL_MAX 4

/* The retail banks the pack names, by number, and the header's first flag. */
static u32 retailTex[RETAIL_MAX], retailObj[RETAIL_MAX];
static s32 numRetailTex, numRetailObj, retailFirst;

/* The table on the game's heap. The records are copies of the slots the game's loaders filled. */
typedef struct G64BankTable {
    TextureBank tex[RETAIL_MAX];
    ObjectBank obj[RETAIL_MAX];
    s32 numTex, numObj, first;
} G64BankTable;

/* The table's address as the game stores it, or 0, and portHeapInits when it was made. */
static u32 tableWord, tableHeapInits;
/* 1 while G64LoadPackBanks runs. It is set and cleared within one call, so a saved state never holds it set. */
static s32 packBankLoading;
/* For tests: the texture and object lookups that the table answered in this game, and the texture lookups that found
   nothing while the banks here loaded at the last level start. */
s32 g64PackTextureHits, g64PackObjectHits, g64PackTextureMisses;
/* For tests: the lookups that reached a live table while all five object slots were empty, outside
   G64LoadPackBanks. That is only between the frees of a level's slots and JallocInit (FreeLevel2 and FreeScreen in
   game.c, and frontend.c), where G64DropPackBanks has dropped the table. In a level, object slot 0 holds bank 0. */
s32 g64PackFreedSlotReads;
/* For tests: the retail object banks that stopped the level at the last level start because a sprite's texture was
   missing. */
s32 g64PackSpriteStops;

void G64SetPackModels(u32 texRom, u32 texSize, u32 objRom, u32 objSize) {
    if (texSize == 0 || objSize == 0) {
        texRom = texSize = objRom = objSize = 0;
    }
    g64PackTexRom = texRom;
    g64PackTexSize = texSize;
    g64PackObjRom = objRom;
    g64PackObjSize = objSize;
    numRetailTex = numRetailObj = retailFirst = 0;
}

/* The retail banks the pack names, after G64SetPackModels, which clears them. first is the header's flag: search the
   retail texture banks before the level's own. At most RETAIL_MAX of each kind are kept. */
void G64SetPackRetail(s32 first, const u32 *tex, s32 numTex, const u32 *obj, s32 numObj) {
    s32 i;

    numRetailTex = numTex < 0 ? 0 : numTex > RETAIL_MAX ? RETAIL_MAX : numTex;
    numRetailObj = numObj < 0 ? 0 : numObj > RETAIL_MAX ? RETAIL_MAX : numObj;
    for (i = 0; i < numRetailTex; i++) {
        retailTex[i] = tex[i];
    }
    for (i = 0; i < numRetailObj; i++) {
        retailObj[i] = obj[i];
    }
    retailFirst = first != 0 && numRetailTex > 0;
}

s32 G64PackBankLoading(void) {
    return packBankLoading;
}

/* The table, or NULL when there is none on the heap as it is now. */
static G64BankTable *LiveTable(void) {
    s32 i;

    if (tableWord == 0 || tableHeapInits != portHeapInits) {
        return NULL;
    }
    for (i = 0; i < 5 && objectBanks[i].data == NULL; i++) {
    }
    if (i == 5 && !packBankLoading) {
        g64PackFreedSlotReads++;
    }
    return PORT_W2P(G64BankTable, tableWord);
}

/* The levels' slots are about to be freed, and the heap set up again after (FreeLevel2 in game.c, and frontend.c).
   The game looks up textures between the two, in LoadFont. */
void G64DropPackBanks(void) {
    tableWord = 0;
}

static Texture *SearchTextures(const TextureBank *bank, u32 id) {
    u8 *p = PORT_W2P(u8, bank->textures);
    Texture *tex;
    s32 j;

    for (j = 0; j < bank->count; j++) {
        tex = (Texture *)p;
        if (tex->crc == id) {
            return tex;
        }
        p += tex->size;
    }
    return NULL;
}

/* FindTexture's search of the table: early is 1 for the search before the slots, 0 for the one after. The table is
   searched once, before the slots with the first flag and after them without. */
Texture *G64PackTexture(u32 id, s32 early) {
    G64BankTable *t = LiveTable();
    Texture *tex;
    s32 i;

    if (t == NULL || (early != 0) != (t->first != 0)) {
        return NULL;
    }
    for (i = 0; i < t->numTex; i++) {
        if ((tex = SearchTextures(&t->tex[i], id)) != NULL) {
            g64PackTextureHits++;
            return tex;
        }
    }
    return NULL;
}

/* FindObject's search of the table, after the slots. */
ObjectInstance *G64PackObject(u32 id) {
    G64BankTable *t = LiveTable();
    s32 i, j;

    if (t == NULL) {
        return NULL;
    }
    for (i = 0; i < t->numObj; i++) {
        for (j = 0; j < t->obj[i].count; j++) {
            if (t->obj[i].entries[j].crc == id) {
                g64PackObjectHits++;
                return t->obj[i].entries[j].inst;
            }
        }
    }
    return NULL;
}

/* FindTexture found nothing for an id. For the test of retail object banks loaded without their textures. */
void G64PackTextureMissed(void) {
    if (packBankLoading) {
        g64PackTextureMisses++;
    }
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

/* A retail number that the game may load into the table: a case of the game's bank switch, but not the font's or the
   generic texture bank, and not object bank 0 or 1. The client refuses the same numbers (bankcheck.c). */
static s32 RetailAllowed(s32 objects, u32 num) {
    u32 start, end;

    if (objects) {
        return num != 0 && num != 1 && ObjectBankRange(num, &start, &end) != NULL;
    }
    return num != FONT_TEX_BANK && num != GENERIC_TEX_BANK && TextureBankRange(num, &start, &end) != NULL;
}

/* 1 when an object of the tree has a sprite whose texture no loaded bank had. The game reads a sprite's texture with
   no test for NULL (AddObjectsSpritesToSpriteList, actor.c). A face's texture may be NULL: LoadTexture tests it. A
   display list's texture word keeps the id, and the renderer reads only inside the game's memory. The parser asks
   the same of each object a level file names (G64_USABLE in parse.c). */
s32 G64SpriteTextureMissing(Object *obj) {
    s32 i;

    for (; obj != NULL; obj = obj->sibling) {
        for (i = 0; obj->sprites != NULL && i < obj->numSprites; i++) {
            if (obj->sprites[i].tex == NULL) {
                return 1;
            }
        }
        if (G64SpriteTextureMissing(obj->child)) {
            return 1;
        }
    }
    return 0;
}

/* Loads retail bank num into the table with the swap of Glovebox's mod (mod/banks.c): the five slots of its kind go
   aside and are emptied, the game's loader puts the bank in slot 0, and slot 0 is copied into the table. A texture
   bank's animated textures are dropped: cyclingTextures is put back as it was. During an object bank's load the
   texture slots stay, so its objects find the level's textures and the table's. */
static void LoadRetail(G64BankTable *t, s32 objects, u32 num) {
    TextureBank texSlots[5];
    ObjectBank objSlots[5];
    PTR32(Texture) cycling[20];
    s32 numCycling = numCyclingTextures;

    s32 i;

    if (objects) {
        memcpy(objSlots, objectBanks, sizeof objSlots);
        memset(objectBanks, 0, sizeof objSlots);
        LoadObjectBank(num);
        if (objectBanks[0].data != NULL) {
            t->obj[t->numObj++] = objectBanks[0];
            for (i = 0; i < objectBanks[0].count; i++) {
                if (G64SpriteTextureMissing(objectBanks[0].entries[i].inst->obj)) {
                    /* The pack names an object bank without the texture bank of its sprites. The client refuses the
                       same pack (g64_pack_check_retail_sprites in src/client/packrom.c). */
                    g64PackSpriteStops++;
                    G64Unplayable("RETAIL TEXTURES");
                    break;
                }
            }
        }
        memcpy(objectBanks, objSlots, sizeof objSlots);
        return;
    }
    memcpy(cycling, cyclingTextures, sizeof cycling);
    memcpy(texSlots, textureBanks, sizeof texSlots);
    memset(textureBanks, 0, sizeof texSlots);
    numCyclingTextures = 0;
    LoadTextureBank(num);
    if (textureBanks[0].data != 0) {
        t->tex[t->numTex++] = textureBanks[0];
    }
    memcpy(textureBanks, texSlots, sizeof texSlots);
    memcpy(cyclingTextures, cycling, sizeof cycling);
    numCyclingTextures = numCycling;
}

/* The order: each retail texture bank, the pack's texture bank, each retail object bank, the pack's object bank. The
   texture banks come first: an object bank's load looks up its textures. */
void G64LoadPackBanks(s32 num) {
    JallocControl *jc = &jallocControl;
    G64BankTable *t = NULL;
    s32 own = g64PackTexSize != 0 && g64PackObjSize != 0;
    s32 i;

    g64PackHeapFree = jc->heapSize - jc->staticUsed - jc->dynamicUsed;
    g64PackHeapGap = HeapGap();
    g64PackHeapBlocks = JallocFreeBlocks();
    g64PackHeapLastStatic = jc->lastStatic;
    packTexSlot = packObjSlot = -1;
    tableWord = 0;
    g64PackTextureMisses = 0;
    g64PackSpriteStops = 0;
    if (num != g64PackLevel || (!own && numRetailTex == 0 && numRetailObj == 0) || gameInfo.mode == 13) {
        return;
    }
    /* The loaders take a slot only after they have loaded the bank, and their search for one has no bound. */
    if (own && (FreeSlot(0) < 0 || FreeSlot(1) < 0)) {
        G64Unplayable("NO BANK SLOT");
        return;
    }
    packBankLoading = 1;
    if (numRetailTex > 0 || numRetailObj > 0) {
        t = (G64BankTable *)JallocAlloc(sizeof(G64BankTable), 1, "G64Banks");
        t->first = retailFirst;
        tableWord = PORT_P2W(t);
        tableHeapInits = portHeapInits;
    }
    for (i = 0; i < numRetailTex; i++) {
        if (RetailAllowed(0, retailTex[i])) {
            LoadRetail(t, 0, retailTex[i]);
        }
    }
    if (own) {
        packTexSlot = FreeSlot(0);
        sprintf(message, "%s", "PACK_TEX_BANK");
        LoadTextureBankAt(PACK_BANK, g64PackTexRom, g64PackTexRom + g64PackTexSize);
    }
    for (i = 0; i < numRetailObj; i++) {
        if (RetailAllowed(1, retailObj[i])) {
            LoadRetail(t, 1, retailObj[i]);
        }
    }
    if (own) {
        packObjSlot = FreeSlot(1);
        sprintf(message, "%s", "PACK_OBJ_BANK");
        LoadObjectBankAt(PACK_BANK, g64PackObjRom, g64PackObjRom + g64PackObjSize);
    }
    packBankLoading = 0;
}

/* For tests: the table's counts of retail banks loaded, or -1 for each when there is no live table. */
void G64PackTableCounts(s32 *numTex, s32 *numObj) {
    G64BankTable *t = LiveTable();

    *numTex = t ? t->numTex : -1;
    *numObj = t ? t->numObj : -1;
}

/* For tests: the directory ids of the table's object bank i. Writes at most max ids and returns how many the bank
   has, or -1 when there is no such bank. */
s32 G64PackTableObjectIds(s32 i, u32 *ids, s32 max) {
    G64BankTable *t = LiveTable();
    s32 j;

    if (t == NULL || i < 0 || i >= t->numObj) {
        return -1;
    }
    for (j = 0; j < t->obj[i].count && j < max; j++) {
        ids[j] = t->obj[i].entries[j].crc;
    }
    return t->obj[i].count;
}

/* The game's names have at most 8 characters, and a shorter one ends with a 0. */
static s32 SameName(const Object *obj, const char *name) {
    s32 i;

    for (i = 0; i < (s32)sizeof obj->name; i++) {
        if (obj->name[i] != name[i]) {
            return 0;
        }
        if (name[i] == 0) {
            return 1;
        }
    }
    return name[i] == 0;
}

static s32 TreeHasName(Object *obj, const char *name) {
    for (; obj != NULL; obj = obj->sibling) {
        if (SameName(obj, name) || TreeHasName(obj->child, name)) {
            return 1;
        }
    }
    return 0;
}

/* For tests: 1 when an object of the table's banks, at any depth, has the name. */
s32 G64PackTableHasName(const char *name) {
    G64BankTable *t = LiveTable();
    s32 i, j;

    for (i = 0; t != NULL && i < t->numObj; i++) {
        for (j = 0; j < t->obj[i].count; j++) {
            if (TreeHasName(t->obj[i].entries[j].inst->obj, name)) {
                return 1;
            }
        }
    }
    return 0;
}

/* For tests: the level being played, as LoadLevel set it. */
s32 G64PackGameLevel(void) {
    return gameInfo.level;
}

/* For tests: the texture banks that LoadLevel (game.c) loads for level num besides the font and GENERIC, as an
   online game loads them (no demo, no cheat). Writes at most max numbers and returns how many there are, or -1 for a
   level that is not 0 to 47. */
s32 G64LevelTextureBanks(s32 num, u32 *banks, s32 max) {
    u32 list[3];
    s32 n = 0, i;

    if (num < 0 || num > OUTRO) {
        return -1;
    }
    if (num < INTRO) {
        list[n++] = (u32)(num == WAYROOM ? worldInfo[0].texBank
                          : num == PRESENT ? PRESENT_TEX_BANK : worldInfo[LevelToWorld((s8)num)].texBank);
    } else {
        list[n++] = CAMEO_TEX_BANK;
    }
    if (num == CARNIVALBONUS) {
        list[n++] = CKBONUS_TEX_BANK;
    }
    if (num == FLYTHRU) {
        list[n++] = FLYTHRU_TEX_BANK;
    }
    for (i = 0; i < n && i < max; i++) {
        banks[i] = list[i];
    }
    return n;
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
