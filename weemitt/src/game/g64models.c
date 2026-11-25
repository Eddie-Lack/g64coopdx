/* g64coopdx: the models a player can choose for their glove and their ball (g64models.h). The game's glove and ball
   stay as they are: DrawActor asks G64ActorModel which object to draw in their place, at what time of its animation
   and at what size. Nothing here writes to the game's memory.

   In the online game the choice is drawn only in a player's view. The client loads the game's state again after a
   view (G64SwapPlayer). The frame the game draws for itself is part of the state's hash, and it keeps the game's own
   models whatever the players chose. */
PORT_C_BEGIN
#include "common.h"
#include "libc.h"
#include "maths.h"
#include "actor.h"
#include "banks.h"
#include "enums.h"
#include "g64players.h"
#include "g64models.h"

u32 UpdateCRC(u8 *ptr);
u32 G64TitleModelWord(Actor *a, s32 *kind);
extern s32 frameCount2;

/* One model. idle and move are the numbers of its own animations that stand for the glove's standing still and its
   walking or running. show pairs some of the glove's moves with one of its own (-1 ends the list). size multiplies
   the glove's or the ball's own scale. */
typedef struct {
    const char *label;
    const char *file;
    f32 size;
    s8 idle;
    s8 move;
    const s8 *show;
} G64Model;

/* Some of the glove's moves, each with one of Cross-Stitch's seven. Cross-Stitch stands in its move 3 and walks in
   its move 0. Its moves 4 and 5 gesture and laugh, and 6 casts a spell. Its moves 1 and 2 fly. */
static const s8 crossStitchShow[] = {
    HANDANIM_WAVE,      4, HANDANIM_THUMBSUP,  5, HANDANIM_OKAY,      5, HANDANIM_CARTWHEEL, 6,
    HANDANIM_IDLE2WAVE, 4, HANDANIM_DENIS,     4, HANDANIM_WHACK1,    6, HANDANIM_POINTCYCLE, 4,
    HANDANIM_NMESPELL,  6, HANDANIM_HANDSPELL, 6, HANDANIM_BALLSPELL, 6, -1,
};

/* The lists. Entry 0 is the game's own model, which is never replaced. The others are characters of the GENERIC
   bank: Cross-Stitch, and three of the enemies that every world has. */
static const G64Model gloves[] = {
    {"GLOVER", "goodglov.ndo", 1.0f, 0, 0, NULL},
    {"CROSS-STITCH", "evilglove.ndo", 0.75f, 3, 0, crossStitchShow},
    {"WEEVIL", "weevil.ndo", 1.3f, 0, 1, NULL},
    {"FISH", "gordon.ndo", 2.0f, 0, 1, NULL},
    {"BUTTERFLY", "butterfly.ndo", 3.0f, 0, 1, NULL},
};
static const G64Model balls[] = {
    {"BALL", "gball.ndo", 1.0f, -1, -1, NULL},
    {"WEEVIL", "weevil.ndo", 1.3f, 0, 0, NULL},
    {"FISH", "gordon.ndo", 2.0f, 0, 0, NULL},
    {"BUTTERFLY", "butterfly.ndo", 3.0f, 0, 0, NULL},
};

#define COUNT(a) ((s32)(sizeof(a) / sizeof((a)[0])))
/* The network takes a model's number below 8 (G64_MODELS, session.h). A longer list doesn't compile. */
typedef char G64GlovesFit[COUNT(gloves) <= 8 ? 1 : -1];
typedef char G64BallsFit[COUNT(balls) <= 8 ? 1 : -1];

int G64ModelCount(int kind) {
    return kind == G64_MODEL_BALL ? COUNT(balls) : COUNT(gloves);
}

static const G64Model *model(s32 kind, s32 i) {
    if (i < 0 || i >= G64ModelCount(kind)) {
        return NULL;
    }
    return kind == G64_MODEL_BALL ? &balls[i] : &gloves[i];
}

const char *G64ModelLabel(int kind, int i) {
    const G64Model *m = model(kind, i);
    return m ? m->label : "";
}

/* The loaded object with this file name's id, from the game's object banks, or NULL. Unlike FindObject, it only
   reads. */
static ObjectInstance *find_loaded(const char *file) {
    u32 id = UpdateCRC((u8 *)file);
    s32 x, y;
    for (x = 0; x < 5; x++) {
        if (!objectBanks[x].data) {
            continue;
        }
        for (y = 0; y < objectBanks[x].count; y++) {
            if (objectBanks[x].entries[y].crc == id) {
                return objectBanks[x].entries[y].inst;
            }
        }
    }
    return NULL;
}

/* 1 for the glove's moves that walk or run. */
static s32 moving(s32 anim) {
    switch (anim) {
        case HANDANIM_BALLWALK:
        case HANDANIM_BALLWALK2:
        case HANDANIM_WALK:
        case HANDANIM_TIPTOE:
        case HANDANIM_CRAWL:
        case HANDANIM_RUN:
        case HANDANIM_PEDDLE:
            return 1;
        default:
            return 0;
    }
}

/* The other model's animation for the glove's move. */
static s32 pick_anim(const G64Model *m, s32 anim) {
    s32 k;
    if (m->show != NULL) {
        for (k = 0; m->show[k] >= 0; k += 2) {
            if (m->show[k] == anim) {
                return m->show[k + 1];
            }
        }
    }
    return moving(anim) ? m->move : m->idle;
}

Object *G64ActorModel(Actor *a, f32 *time, f32 *size) {
    s32 kind = 0, owner, k;
    u32 word;
    const G64Model *m;
    ObjectInstance *own, *other;
    AnimState *st;

    if (a == NULL || a->inst == NULL || a->inst->obj == NULL) {
        return NULL;
    }
    word = G64TitleModelWord(a, &kind);
    if (word == 0) {
        owner = G64ActorOwner(a, &kind);
        if (owner < 0 || !G64ModelDrawn()) {
            return NULL;
        }
        word = g64PlayerColor[owner][kind];
    }
    m = model(kind, (s32)G64_MODEL_OF(word));
    if (m == NULL || m == (kind == G64_MODEL_BALL ? &balls[0] : &gloves[0])) {
        return NULL;
    }
    /* Only the game's own glove and ball change. The frog the glove becomes, or an enemy that a cheat made the ball,
       stays. */
    own = find_loaded(kind == G64_MODEL_BALL ? balls[0].file : gloves[0].file);
    if (own == NULL) {
        return NULL;
    }
    if (a->inst->obj != own->obj) {
        if (kind != G64_MODEL_BALL) {
            return NULL;
        }
        own = find_loaded("crystalball.ndo");
        if (own == NULL || a->inst->obj != own->obj) {
            return NULL;
        }
    }
    other = find_loaded(m->file);
    if (other == NULL || other->obj == NULL) {
        return NULL;
    }
    *size = m->size;
    st = other->anim;
    if (m->idle < 0 || st == NULL || st->anims == NULL || st->numAnims <= 0) {
        return other->obj;
    }
    /* The other model's animation, as far into it as the glove is into its own. A ball has none: its model's idle
       move runs on the frame count. */
    {
        AnimInfo *to;
        f32 into, length;
        k = m->idle;
        into = frameCount2 * 0.67f;
        if (kind == G64_MODEL_GLOVE && a->anim != NULL && a->anim->anims != NULL && a->anim->current >= 0) {
            k = pick_anim(m, a->anim->current);
            into = *time - a->anim->anims[a->anim->current].start;
        }
        if (k < 0 || k >= st->numAnims) {
            k = 0;
        }
        to = &st->anims[k];
        length = to->end - to->start;
        if (into < 0.0f || length <= 0.0f) {
            into = 0.0f;
        }
        if (length > 0.0f) {
            into -= (f32)(s32)(into / length) * length;
        }
        *time = to->start + into;
    }
    return other->obj;
}

PORT_C_END
