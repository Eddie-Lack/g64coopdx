/* g64coopdx: the models a player can choose for their glove and their ball (g64models.c). The choice only changes
   what's drawn. Every model comes from the game's GENERIC object bank, which every level loads: nothing more is loaded,
   and the game's memory stays as it is. */
#ifndef G64MODELS_H
#define G64MODELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The two lists. */
#define G64_MODEL_GLOVE 0
#define G64_MODEL_BALL 1
/* A player's colors (G64_COLOR_SET plus 0xRRGGBB) carry the model's number in bits 25 to 28: the glove's in the glove
   color, the ball's in the ball color. 0 is the game's own model. */
#define G64_MODEL_SHIFT 25
#define G64_MODEL_MASK (0xFu << G64_MODEL_SHIFT)
#define G64_MODEL_OF(c) (((c) >> G64_MODEL_SHIFT) & 0xFu)

/* How many models a list has, and a model's name, in capitals as the game writes its menu items. */
int G64ModelCount(int kind);
const char *G64ModelLabel(int kind, int i);
/* Playing alone (g64solo.c): the player's glove and ball models, from their color words. Their colors stay off, as
   before. */
void G64SoloModels(unsigned glove, unsigned ball);
/* Playing alone (g64solo.c): the game goes from its start straight to the file select, and the save's second file
   is a complete game, CHOSIS, while the game runs. The save on disk doesn't change. */
void G64SoloStart(void);

#ifdef __cplusplus
}
#endif

#endif
