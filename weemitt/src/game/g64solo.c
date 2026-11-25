/* g64coopdx: playing alone from the start screen (g64models.h). The game starts at the file select, with no title
   menu before it. The save's second file is CHOSIS, a complete game: every level done, every garib, every crystal in
   the cave. It's the only file whose name has more than 3 letters. The save keeps 3 letters of a name: the file holds
   CHO, and the file select draws the whole name (G64SlotName).

   The online program keeps the save in memory and never writes it to disk (platform.c). The player's own second file
   on disk stays as it was. */
PORT_C_BEGIN
#include "common.h"
#include "libc.h"
#include "save.h"
#include "enums.h"
#include "game.h"
#include "g64players.h"
#include "g64models.h"

extern SaveSlot blankSlot;
extern s32 curSoundFXVol;
extern s32 curSoundMUSVol;

static s32 soloSave;
static s32 soloSkip;
static s32 soloArrived;

#define CHOSIS_SLOT 1
/* Five bits a world for the six worlds: three levels, the boss and the bonus level. */
#define ALL_LEVELS 0x3FFFFFFF

void G64SoloStart(void) {
    soloSave = 1;
    soloSkip = 1;
}

void G64SoloModels(unsigned glove, unsigned ball) {
    g64PlayerColor[0][0] = glove & G64_MODEL_MASK;
    g64PlayerColor[0][1] = ball & G64_MODEL_MASK;
}

/* RunGameInit, after the game has read the save: the second file becomes CHOSIS. */
void G64SoloFillSave(void) {
    SaveSlot *s = &saveSlot[CHOSIS_SLOT];
    s32 i;
    if (!soloSave) {
        return;
    }
    memcpy(s, &blankSlot, sizeof(SaveSlot));
    s->name[0] = 'C';
    s->name[1] = 'H';
    s->name[2] = 'O';
    /* The byte after the name ends it where the file select draws it. */
    s->unk03 = 0;
    s->levels = ALL_LEVELS;
    s->allGaribs = ALL_LEVELS;
    s->score = 250000;
    s->lives = 9;
    s->difficultyLevel = DIFFICULTY_MEDIUM;
    /* The cave has all seven crystals: one for the first visit, and one for each world. */
    s->hubTarget = 7;
    s->hubLevel = 7;
    s->visited = 0;
    for (i = 0; i < PRESENT; i++) {
        if (levelInfo[i].bit >= 0 && levelInfo[i].bit < 32) {
            s->visited |= 1 << levelInfo[i].bit;
        }
    }
    s->sfxVolume = curSoundFXVol;
    s->musicVolume = curSoundMUSVol;
}

/* The name the file select draws for a file. */
const char *G64SlotName(s32 slot) {
    const SaveSlot *s = &saveSlot[slot];
    if (soloSave && slot == CHOSIS_SLOT && s->name[0] == 'C' && s->name[1] == 'H' && s->name[2] == 'O' &&
        s->levels == ALL_LEVELS && s->allGaribs == ALL_LEVELS) {
        return "CHOSIS";
    }
    return (const char *)s->name;
}

/* 1 once, at the title menu's start, when the game should go to the file select in its place. */
s32 G64SoloSkipTitle(void) {
    if (!soloSkip) {
        return 0;
    }
    soloSkip = 0;
    soloArrived = 1;
    return 1;
}

/* 1 once the game has gone to the file select in place of the title menu. */
s32 G64SoloArrived(void) {
    return soloArrived;
}

PORT_C_END
