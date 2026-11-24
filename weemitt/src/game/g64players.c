/* g64coopdx: more than one player in one game, by swapping. A prototype (docs/discovery/state-inventory.md, "Two
   ways to make the game multi-player").

   The game has one glove, one ball and one camera, and its code tests for them by address (actor == &hand). So the
   other players do not get globals of their own. Each extra player has a slot on the game's heap that holds their
   copy of the per-player globals. In each frame, after the first player's part of the frame, the slot is swapped
   into the globals, that player's part of the frame runs, and the slot is swapped back out.

   What a swap leaves alone: the list links of every swapped actor (the first 0x18 bytes: the actor lists hold the
   globals' addresses), and every world system. Enemies and bullets swap in the player they look at ("Enemies and
   bullets", below). Collectables, puzzles and platforms still see only the first player. With one player nothing here
   runs, and the game is unchanged. */
PORT_C_BEGIN
#include "common.h"
#include "actor.h"
#include "ball.h"
#include "hand.h"
#include "camera.h"
#include "controller.h"
#include "game.h"
#include "jalloc.h"
#include "delta.h"
#include "platform.h"
#include "maths.h"
#include "bullet.h"
#include "puzzle.h"
#include "sound.h"
#include "collision.h"
#include "debris.h"
#include "save.h"
#include "wind.h"
#include "water.h"
#include "spike.h"
#include "buzzer.h"
#include "collect.h"
#include "footprint.h"

PORT_C_END

#include <string.h>

PORT_C_BEGIN
#include "g64players.h"
#include "tip.h"
#include "soundtable.h"

extern u8 ActiveController;
extern u8 gamePaused;
extern OSContPad controllerdata[];
extern Vec3 oldHandPos;
extern s32 ballWithHand;
extern s32 ballLeftBehind;
extern f32 handInertia;
extern Vec3 ballFrameVel;
extern u8 disableZoom;
extern s16 currentSaveSlot;

void ReadController(s32 cntlr);
void ProcessInput(void);
void UpdateActorPositionDependencies(void);
void MoveBall(void);
void MoveHand(void);
void MoveCam(void);
void PlayerSpinCamera(void);
void RestartPlayer(void);
void UpdateActor(Actor *actor);

s32 g64Players = 1;
s32 g64Online;
G64ExitRecord g64Exits[G64_MAX_PLAYERS];
s32 g64ExitCount;
s32 g64Member[G64_MAX_PLAYERS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
u16 g64Buttons[G64_MAX_PLAYERS];
s8 g64StickX[G64_MAX_PLAYERS];
s8 g64StickY[G64_MAX_PLAYERS];

/* The bytes of handControl and player: player starts inside handControl's last word (gen/ram.S), so they are one
   block. */
#define HAND_BLOCK_MAX 0x640
/* An actor's list links: next, prev, collNext, collPrev, distNext, distPrev. */
#define LINKS 0x18

/* One player's part of the cave's scene (game.c, DoCaveStuff): the frame they came into the level, whether a crystal
   was due then, and their flight's pedestal. Their stage is gameInfo.caveStage while they are in the globals. */
typedef struct G64Cave {
    s32 arrivedAt;
    s32 due;
    s32 target;
} G64Cave;

extern s16 garibTimer;
extern s16 garibCount;

/* The words in the glove's and the ball's display lists that name their textures and palettes. The game writes
   them when a power-up or a spell changes the look (ball.c, collect.c). They are each player's own: a swap
   exchanges them, and another player's glove and ball are drawn with that player's words (G64ActorTextures). */
#define G64_TEX_WORDS 10
extern PTR32(u32) ballTextureData;
extern PTR32(u32) ballTexturePalette;
extern PTR32(u32) ballTextureData2;
extern PTR32(u32) ballTexturePalette2;
extern PTR32(u32) ballBitTextureData;
extern PTR32(u32) ballBitTexturePalette;
extern PTR32(u32) handTextureData[2];
extern PTR32(u32) handTexturePalette[2];
/* The ball's other model: the crystal ball's, or the usual one while the ball is crystal (ChangeBallInto). */
extern PTR32(ObjectInstance) crystalBall;

/* The words that this level's models have (a bit each). The loader sets a bit when it finds the word
   (src/port/loaders.c), and a level load starts from none (G64ExtraPlayersFree). The game's own pointer to a word
   that this level lacks is left over from an earlier level, and nothing may be read or written through it. */
static u32 texLoaded;

void G64TexWordLoaded(s32 k) {
    texLoaded |= 1u << k;
}

/* Word k's place in the game's memory, or NULL when the level has none. 0 to 5 are the ball's, 6 to 9 the glove's. */
static u32 *tex_word(s32 k) {
    if (!(texLoaded & (1u << k)))
        return NULL;
    switch (k) {
        case 0: return ballTextureData;
        case 1: return ballTexturePalette;
        case 2: return ballTextureData2;
        case 3: return ballTexturePalette2;
        case 4: return ballBitTextureData;
        case 5: return ballBitTexturePalette;
        case 6: return handTextureData[0];
        case 7: return handTextureData[1];
        case 8: return handTexturePalette[0];
        default: return handTexturePalette[1];
    }
}

typedef struct G64Slot {
    u32 texWords[G64_TEX_WORDS];
    PTR32(ObjectInstance) crystalBall;
    /* The run of garibs taken in quick succession: the frames left to add to it, and its length (collect.c). */
    s16 garibTimer;
    s16 garibCount;
    Actor hand;
    Actor ball;
    Actor camera;
    CameraInfo cameraInfo;
    u8 handBlock[HAND_BLOCK_MAX];
    BallControl ballControl;
    Pad pad;
    Vec3 oldHandPos;
    s32 ballWithHand;
    s32 ballStatic;
    s32 ballLeftBehind;
    f32 handInertia;
    s32 handFallLifted;
    Vec3 cameraUpVect;
    Vec3 cameraUpVectAim;
    Vec3 ballFrameVel;
    CameraRequests tickCameraRequests;
    Vec3 ballStart;
    /* The point a boss level's camera looks at (boss.c). */
    Vec3 bossFocalPoint;
    /* The frames this player's glove and ball have been near the charged Fortress boss (boss.c). */
    u8 handPain;
    u8 ballPain;
    u8 painPad[2];
    /* Who has the controls: the camera, the glove or the ball (controller.c, bonus.c). */
    s32 controlMethod;
    /* The frames this player's glove has swum in the Atlantis bonus level (bonus.c). */
    s32 atlantisInWater;
    /* Where this player's camera looked when they took up the look-around mode (controller.c). */
    f32 camOrigRotY;
    /* The height this player's camera keeps above its target (camera.c). */
    s32 levelCamHeight;
    /* This player's turret in the last space boss's level (boss.c): where it points, its shield and reload count,
       the crosshair's aim and place, and the camera's points. */
    Vec3 spaceBossRot;
    Vec3 spaceBossCamVect;
    Vec3 spaceBossCamVect2;
    Vec3 spaceBossTempVect2;
    Vec3 crosshairPos;
    s32 spaceBossShield;
    s32 spaceBossReload;
    u8 spbCrosshairAlpha;
    u8 turretPad[3];
    G64Cave cave;
    u8 caveStage;
    u8 disableZoom;
    u8 caveStagePad[2];
    /* The state of this player's camera zones (puzzle.c, PuzzleSwapCameraState), made the first time they run. */
    PTR32(u8) puzzleState;
} G64Slot;

/* Each extra player's slot, on the game's heap. Slot 0 is unused: player 0 lives in the globals. */
static PTR32(G64Slot) slots[G64_MAX_PLAYERS];
static s32 slotCount;
/* Actors that only show what a slot holds: its glove and its ball. They are in the actor list, so the game draws
   them, and their contents are copied from the slot before each draw. A swapped slot holds another player's state,
   so the proxies always show the players who are not in the globals. */
static PTR32(Actor) proxyHand[G64_MAX_PLAYERS];
static PTR32(Actor) proxyBall[G64_MAX_PLAYERS];
/* Online: the cave's part of the player in the globals (on the heap, in the hashed memory), and a copy of the first
   player as the level set them up, taken when the opening camera gives way. A player who comes later starts from that
   copy, at the level's start: not from wherever the player in the globals is now, with their checkpoint and locks. */
static PTR32(G64Cave) globalCave;
static PTR32(G64Slot) startSlot;
/* The level has given its opening camera way to the normal one (G64ExtraPlayersStep). */
static s32 ready;
/* The players, by number, who came into this level after it started (G64AddPlayer). */
static u32 cameLate;
/* Online: where the first player's glove was on the level's first played frame, the level's start. A level's opening
   can move the glove before its camera gives way, so the start copy may be elsewhere. */
static Vec3 startPos;
static s32 startPosSet;
/* The slot whose player is in the globals during the logic: 0, or the slot of G64ExtraPlayersStep's player or of
   G64EnemyBegin's or G64BulletBegin's target. */
static s32 logicSlot;
static void note_enemy_hits(void);
static void forget_restart(s32 player);
static void forget_restarts(void);
static void copy_restart(s32 to, s32 from);
static void move_restart(s32 toPlayer, s32 fromPlayer);
/* The game has room for 30 enemies (puzzle.c). */
#define G64_ENEMIES 30
/* Each enemy's hitter: the number plus 1 of the first player whose glove or ball hit it since its hit flags were last
   cleared (UpdateEnemies), or 0. */
static s32 enemyHitBy[G64_ENEMIES];
extern Vec3 ballStart;
extern Vec3 bossFocalPoint;
extern s32 controlMethod;
extern s32 atlantisInWater;
extern f32 camOrigRotY;
extern s32 levelCamHeight;
extern Vec3 spaceBossRot, spaceBossCamVect, spaceBossCamVect2, spaceBossTempVect2;
extern s32 spaceBossShield, spaceBossReload;
extern u8 spbCrosshairAlpha;
extern GameSprite spbCrosshair;
extern u8 handPain;
extern u8 ballPain;
extern s32 frameCount;
void AddActor(Actor *actor);
void SubActor(Actor *actor);

/* Flags a proxy must not have: collisions (8) and the wind (0x400, 0x800). 4 keeps it off the collision list. */
#define PROXY_CLEAR (8 | 0x400 | 0x800)

static void copy_to_proxy(Actor *proxy, const Actor *from) {
    memcpy((u8 *)proxy + LINKS, (const u8 *)from + LINKS, sizeof(Actor) - LINKS);
    proxy->flags = (proxy->flags | 4) & ~PROXY_CLEAR;
    /* A held ball points at the global glove; the proxy ball is drawn where it is. */
    proxy->link = NULL;
}

static Actor *new_proxy(const Actor *from) {
    Actor *a = (Actor *)JallocAlloc(sizeof(Actor), 1, "G64Proxy");
    copy_to_proxy(a, from);
    AddActor(a);
    /* AddActor puts an actor at the front of the list, and the outro's code takes the first actor for its own
       (DoOutroEffects). A proxy goes to the back. */
    if (a->next && a->prev == &actorList.head) {
        a->prev->next = a->next;
        a->next->prev = a->prev;
        a->next = &actorList.head;
        a->prev = actorList.head.prev;
        actorList.head.prev->next = a;
        actorList.head.prev = a;
    }
    return a;
}

/* A power-up's orbiters (collect.c, CreateOrbiter) are actors inside handControl and ballControl, which a swap
   exchanges. With more than one player the game's own orbiters stay out of the actor list, and each player's
   orbiters are drawn as proxies: proxyOrbiter[i][k] exists while the player of slot i has orbiter k. */
#define G64_ORBITERS 4
static PTR32(Actor) proxyOrbiter[G64_MAX_PLAYERS][G64_ORBITERS];
/* sync_orbiters took one of the game's own orbiters out of the actor list. */
static s32 orbitersUnlinked;
/* The slot swapped into the globals for a view (G64SwapPlayer), or 0. */
static s32 swapped;
/* The pick-ups taken and not yet scored: the pick-up and the number, plus 1, of the player who took it. */
#define G64_TAKEN 16
static PTR32(Collectable) takenPickup[G64_TAKEN];
static s32 takenBy[G64_TAKEN];
static void sync_orbiters(void);
static void free_orbiter_proxies(void);
static Player *slot_player(s32 i);

/* handControl.orbiter3 is not always a player's: the first space boss makes its rotor in it, and a cameo can link it
   to an enemy. Then it belongs to the world: it stays in the globals through every swap and in the actor list, and
   no slot has a copy of it. A player's own orbiter3 is linked to the glove (collect.c, ActivateCollectable). */
static s32 world_orbiter3(void) {
    return handControl.orbiter3.inst && (Actor *)handControl.orbiter3.link != &hand;
}

static Actor *slot_orbiter3(G64Slot *s) {
    return (Actor *)(s->handBlock + ((u8 *)&handControl.orbiter3 - (u8 *)&handControl));
}

static void swap_bytes(void *a, void *b, size_t n) {
    u8 tmp[256];
    u8 *pa = (u8 *)a, *pb = (u8 *)b;
    while (n) {
        size_t k = n < sizeof tmp ? n : sizeof tmp;
        memcpy(tmp, pa, k);
        memcpy(pa, pb, k);
        memcpy(pb, tmp, k);
        pa += k, pb += k, n -= k;
    }
}

/* Swaps an actor without its list links. */
static void swap_actor(Actor *live, Actor *saved) {
    swap_bytes((u8 *)live + LINKS, (u8 *)saved + LINKS, sizeof(Actor) - LINKS);
}

/* Swaps a block that holds actors, then gives the live actors their links back. */
static void swap_with_actors(void *live, void *saved, size_t n, Actor **inside, s32 count) {
    s32 i;
    swap_bytes(live, saved, n);
    for (i = 0; i < count; i++) {
        size_t off = (size_t)((u8 *)inside[i] - (u8 *)live);
        swap_bytes((u8 *)live + off, (u8 *)saved + off, LINKS);
    }
}

static size_t hand_block_size(void) {
    return (size_t)(((u8 *)&player + sizeof(Player)) - (u8 *)&handControl);
}

/* Exchanges the globals with a slot. Doing it twice restores both. */
static void swap_slot(G64Slot *s) {
    Actor *handActors[5];
    Actor *ballActors[1];
    Actor *camActors[1];
    s32 world3;
    handActors[0] = &handControl.edgeProbe;
    handActors[1] = &handControl.frontProbe;
    handActors[2] = &handControl.orbiter;
    handActors[3] = &handControl.orbiter2;
    handActors[4] = &handControl.orbiter3;
    ballActors[0] = &ballControl.orbiter;
    camActors[0] = &cameraInfo.actor;
    swap_actor(&hand, &s->hand);
    swap_actor(&ball, &s->ball);
    swap_actor(&camera, &s->camera);
    swap_with_actors(&cameraInfo, &s->cameraInfo, sizeof(CameraInfo), camActors, 1);
    world3 = world_orbiter3();
    swap_with_actors(&handControl, s->handBlock, hand_block_size(), handActors, 5);
    if (world3)
        swap_bytes((u8 *)&handControl.orbiter3 + LINKS, (u8 *)slot_orbiter3(s) + LINKS, sizeof(Actor) - LINKS);
    swap_with_actors(&ballControl, &s->ballControl, sizeof(BallControl), ballActors, 1);
    swap_bytes(&pad[ActiveController], &s->pad, sizeof(Pad));
    swap_bytes(&oldHandPos, &s->oldHandPos, sizeof(Vec3));
    swap_bytes(&ballWithHand, &s->ballWithHand, sizeof(s32));
    swap_bytes(&ballStatic, &s->ballStatic, sizeof(s32));
    swap_bytes(&ballLeftBehind, &s->ballLeftBehind, sizeof(s32));
    swap_bytes(&handInertia, &s->handInertia, sizeof(f32));
    swap_bytes(&handFallLifted, &s->handFallLifted, sizeof(s32));
    swap_bytes(&cameraUpVect, &s->cameraUpVect, sizeof(Vec3));
    swap_bytes(&cameraUpVectAim, &s->cameraUpVectAim, sizeof(Vec3));
    swap_bytes(&ballFrameVel, &s->ballFrameVel, sizeof(Vec3));
    swap_bytes(&tickCameraRequests, &s->tickCameraRequests, sizeof(CameraRequests));
    swap_bytes(&ballStart, &s->ballStart, sizeof(Vec3));
    swap_bytes(&bossFocalPoint, &s->bossFocalPoint, sizeof(Vec3));
    swap_bytes(&handPain, &s->handPain, 1);
    swap_bytes(&controlMethod, &s->controlMethod, sizeof(s32));
    swap_bytes(&atlantisInWater, &s->atlantisInWater, sizeof(s32));
    swap_bytes(&camOrigRotY, &s->camOrigRotY, sizeof(f32));
    swap_bytes(&levelCamHeight, &s->levelCamHeight, sizeof(s32));
    swap_bytes(&spaceBossRot, &s->spaceBossRot, sizeof(Vec3));
    swap_bytes(&spaceBossCamVect, &s->spaceBossCamVect, sizeof(Vec3));
    swap_bytes(&spaceBossCamVect2, &s->spaceBossCamVect2, sizeof(Vec3));
    swap_bytes(&spaceBossTempVect2, &s->spaceBossTempVect2, sizeof(Vec3));
    swap_bytes(&spbCrosshair.pos, &s->crosshairPos, sizeof(Vec3));
    swap_bytes(&spaceBossShield, &s->spaceBossShield, sizeof(s32));
    swap_bytes(&spaceBossReload, &s->spaceBossReload, sizeof(s32));
    swap_bytes(&spbCrosshairAlpha, &s->spbCrosshairAlpha, 1);
    swap_bytes(&ballPain, &s->ballPain, 1);
    {
        s32 k;
        for (k = 0; k < G64_TEX_WORDS; k++) {
            u32 *w = tex_word(k);
            if (w)
                swap_bytes(w, &s->texWords[k], sizeof(u32));
        }
        swap_bytes(&crystalBall, &s->crystalBall, sizeof(crystalBall));
    }
    swap_bytes(&garibTimer, &s->garibTimer, sizeof(s16));
    swap_bytes(&garibCount, &s->garibCount, sizeof(s16));
    swap_bytes(&gameInfo.caveStage, &s->caveStage, 1);
    swap_bytes(&disableZoom, &s->disableZoom, 1);
    /* Online the globals' part of the cave exists before the first slot does (global_cave). */
    if (globalCave)
        swap_bytes((G64Cave *)globalCave, &s->cave, sizeof(G64Cave));
}

/* The globals' part of the cave, made the first time it is needed. Online only. */
static G64Cave *global_cave(void) {
    if (!globalCave) {
        G64Cave *c = (G64Cave *)JallocAlloc(sizeof(G64Cave), 0, "G64Cave");
        memset(c, 0, sizeof *c);
        globalCave = c;
    }
    return globalCave;
}

/* Gives an actor in a slot its own heap blocks: copies of the ones it shares with the first player's actor. */
static void clone_blocks(Actor *a) {
    if (a->phys) {
        ActorPhysics *p = (ActorPhysics *)JallocAlloc(sizeof(ActorPhysics), 0, "G64Phys");
        memcpy(p, (ActorPhysics *)a->phys, sizeof(ActorPhysics));
        a->phys = p;
    }
    if (a->obj) {
        ActorObject *o = (ActorObject *)JallocAlloc(sizeof(ActorObject), 0, "G64Obj");
        memcpy(o, (ActorObject *)a->obj, sizeof(ActorObject));
        a->obj = o;
    }
    if (a->anim) {
        AnimState *n = (AnimState *)JallocAlloc(sizeof(AnimState), 0, "G64Anim");
        memcpy(n, (AnimState *)a->anim, sizeof(AnimState));
        a->anim = n;
    }
    if (a->shadow) {
        ActorShadow *sh = (ActorShadow *)JallocAlloc(sizeof(ActorShadow), 0, "G64Shad");
        memcpy(sh, (ActorShadow *)a->shadow, sizeof(ActorShadow));
        a->shadow = sh;
    }
    if (a->state) {
        ActorState *st = (ActorState *)JallocAlloc(sizeof(ActorState), 0, "G64Stat");
        memcpy(st, (ActorState *)a->state, sizeof(ActorState));
        a->state = st;
    }
}

static void free_blocks(Actor *a) {
    JallocFree(&a->phys);
    JallocFree(&a->obj);
    JallocFree(&a->anim);
    JallocFree(&a->shadow);
    JallocFree(&a->state);
}

/* An embedded actor of a slot's handControl, found at the same offset as the global's. */
static Actor *slot_hand_actor(G64Slot *s, Actor *global) {
    return (Actor *)(s->handBlock + ((u8 *)global - (u8 *)&handControl));
}

/* Gives every actor of a slot its own heap blocks. */
static void clone_slot_blocks(G64Slot *s) {
    clone_blocks(&s->hand);
    clone_blocks(&s->ball);
    clone_blocks(&s->camera);
    clone_blocks(&s->cameraInfo.actor);
    clone_blocks(slot_hand_actor(s, &handControl.edgeProbe));
    clone_blocks(slot_hand_actor(s, &handControl.frontProbe));
}

static void free_slot_blocks(G64Slot *s) {
    free_blocks(&s->hand);
    free_blocks(&s->ball);
    free_blocks(&s->camera);
    free_blocks(&s->cameraInfo.actor);
    free_blocks(slot_hand_actor(s, &handControl.edgeProbe));
    free_blocks(slot_hand_actor(s, &handControl.frontProbe));
}

/* Puts slot s in place i, with proxies that show it. */
static void add_slot(s32 i, G64Slot *s) {
    /* A copied slot must not share its template's camera zones. */
    s->puzzleState = NULL;
    slots[i] = s;
    proxyHand[i] = new_proxy(&s->hand);
    proxyBall[i] = new_proxy(&s->ball);
}

/* A new slot that is a copy of slot from (the start copy), with heap blocks of its own. */
static G64Slot *copy_slot(const G64Slot *from) {
    G64Slot *s = (G64Slot *)JallocAlloc(sizeof(G64Slot), 1, "G64Slot");
    memcpy(s, from, sizeof(G64Slot));
    clone_slot_blocks(s);
    return s;
}

/* A new slot that is a copy of the player in the globals, with heap blocks of its own. */
static G64Slot *copy_globals(void) {
    G64Slot *s = (G64Slot *)JallocAlloc(sizeof(G64Slot), 1, "G64Slot");
    memcpy(&s->hand, &hand, sizeof(Actor));
    memcpy(&s->ball, &ball, sizeof(Actor));
    memcpy(&s->camera, &camera, sizeof(Actor));
    memcpy(&s->cameraInfo, &cameraInfo, sizeof(CameraInfo));
    memcpy(s->handBlock, &handControl, hand_block_size());
    /* The first player's slam ring is not the new player's. */
    ((HandControl *)s->handBlock)->footprint = NULL;
    if (world_orbiter3())
        slot_orbiter3(s)->inst = NULL;
    memcpy(&s->ballControl, &ballControl, sizeof(BallControl));
    {
        s32 k;
        for (k = 0; k < G64_TEX_WORDS; k++) {
            u32 *w = tex_word(k);
            s->texWords[k] = w ? *w : 0;
        }
        s->crystalBall = crystalBall;
    }
    /* A new player starts no run of garibs. */
    s->garibTimer = s->garibCount = 0;
    memcpy(&s->pad, &pad[ActiveController], sizeof(Pad));
    s->oldHandPos = oldHandPos;
    s->ballWithHand = ballWithHand;
    s->ballStatic = ballStatic;
    s->ballLeftBehind = ballLeftBehind;
    s->handInertia = handInertia;
    s->handFallLifted = handFallLifted;
    s->cameraUpVect = cameraUpVect;
    s->cameraUpVectAim = cameraUpVectAim;
    s->ballFrameVel = ballFrameVel;
    s->tickCameraRequests = tickCameraRequests;
    s->ballStart = ballStart;
    s->bossFocalPoint = bossFocalPoint;
    s->handPain = handPain;
    s->controlMethod = controlMethod;
    /* A new player has not swum yet. The level sets the first player's count to 0 only after this copy. */
    s->atlantisInWater = 0;
    s->camOrigRotY = camOrigRotY;
    s->levelCamHeight = levelCamHeight;
    s->spaceBossRot = spaceBossRot;
    s->spaceBossCamVect = spaceBossCamVect;
    s->spaceBossCamVect2 = spaceBossCamVect2;
    s->spaceBossTempVect2 = spaceBossTempVect2;
    s->crosshairPos = spbCrosshair.pos;
    s->spaceBossShield = spaceBossShield;
    s->spaceBossReload = spaceBossReload;
    s->spbCrosshairAlpha = spbCrosshairAlpha;
    s->ballPain = ballPain;
    s->caveStage = gameInfo.caveStage;
    if (globalCave)
        s->cave = *(G64Cave *)globalCave;
    clone_slot_blocks(s);
    {
        /* The first player's own things are not the new player's: the two sprites of the pointing glove
           (HAND_LOCATE), which the first player frees, and the fake ball. A copy taken while the first player
           points starts as a glove that stands. */
        HandControl *hc = (HandControl *)s->handBlock;
        hc->locate[0] = NULL;
        hc->locate[1] = NULL;
        hc->fakeball = NULL;
        if (s->hand.state && s->hand.state->mode == HAND_LOCATE)
            s->hand.state->mode = HAND_IDLE;
    }
    return s;
}

/* Makes slot i, for a player who starts with the level as a copy of the player in the globals. */
static void new_slot(s32 i) {
    G64Slot *s = copy_globals();
    /* Side by side, not inside each other. */
    s->hand.pos.x += 40.0f * i;
    s->ball.pos.x += 40.0f * i;
    add_slot(i, s);
    /* A copy taken during the level's opening respawn goes on with it (the level may start the glove below the
       floor). */
    copy_restart(i, 0);
}

static void arrive(s32 i, const Vec3 *to);
extern Vec3 handStart;

/* Makes slot i for a player who came after the level started: from the start copy online, as the level set the first
   player up, moved to the level's first glove position; or else from the player in the globals, moved to handStart.
   Then afresh (arrive). */
static void new_late_slot(s32 i) {
    if (startSlot) {
        add_slot(i, copy_slot(startSlot));
        copy_restart(i, -1);
        arrive(i, startPosSet ? &startPos : NULL);
        /* The count of garibs is the team's (G64GaribGained). */
        slot_player(i)->garibs = player.garibs;
    } else {
        add_slot(i, copy_globals());
        copy_restart(i, 0);
        arrive(i, &handStart);
    }
}

/* Frees slot i and its proxies. blocks: also the heap blocks of its actors, which a slot made by new_slot owns. */
static void free_slot(s32 i, s32 blocks) {
    G64Slot *s = slots[i];
    if (proxyHand[i]) {
        SubActor(proxyHand[i]);
        JallocFree(&proxyHand[i]);
    }
    if (proxyBall[i]) {
        SubActor(proxyBall[i]);
        JallocFree(&proxyBall[i]);
    }
    if (!s)
        return;
    if (s->puzzleState)
        JallocFree(&s->puzzleState);
    if (!blocks) {
        JallocFree(&slots[i]);
        return;
    }
    free_slot_blocks(s);
    JallocFree(&slots[i]);
}


static void free_slots(void) {
    s32 i;
    free_orbiter_proxies();
    orbitersUnlinked = 0;
    for (i = 0; i < G64_TAKEN; i++) {
        takenBy[i] = 0;
        takenPickup[i] = NULL;
    }
    for (i = 1; i < G64_MAX_PLAYERS; i++)
        free_slot(i, 1);
    slotCount = 0;
    memset(enemyHitBy, 0, sizeof enemyHitBy);
}

void G64ExtraPlayersStart(void) {
    s32 i;
    free_slots();
    if (g64Players < 2 || hand_block_size() > HAND_BLOCK_MAX)
        return;
    slotCount = g64Players > G64_MAX_PLAYERS ? G64_MAX_PLAYERS : g64Players;
    for (i = 1; i < slotCount; i++) {
        if ((cameLate >> g64Member[i]) & 1)
            new_late_slot(i);
        else
            new_slot(i);
    }
}

/* Orbiter k of the player whose state is in the globals (store 0) or in slot `store`. */
static Actor *orbiter_in(s32 store, s32 k) {
    Actor *live[G64_ORBITERS];
    G64Slot *s = slots[store];
    live[0] = &handControl.orbiter;
    live[1] = &handControl.orbiter2;
    live[2] = &handControl.orbiter3;
    live[3] = &ballControl.orbiter;
    if (store == 0)
        return live[k];
    if (k == 3)
        return &s->ballControl.orbiter;
    return (Actor *)(s->handBlock + ((u8 *)live[k] - (u8 *)&handControl));
}

static void free_orbiter_proxy(s32 i, s32 k) {
    if (proxyOrbiter[i][k]) {
        SubActor(proxyOrbiter[i][k]);
        JallocFree(&proxyOrbiter[i][k]);
    }
}

static void free_orbiter_proxies(void) {
    s32 i, k;
    for (i = 0; i < G64_MAX_PLAYERS; i++)
        for (k = 0; k < G64_ORBITERS; k++)
            free_orbiter_proxy(i, k);
}

/* Makes and frees the orbiters' proxies. It runs in the logic with no slot swapped in, so the actor list is the
   same on every machine. */
static void sync_orbiters(void) {
    s32 i, k, world3;
    if (swapped || logicSlot)
        return;
    if (slotCount < 2) {
        /* One player again: the game's own orbiters go back into the actor list. */
        if (orbitersUnlinked) {
            free_orbiter_proxies();
            for (k = 0; k < G64_ORBITERS; k++) {
                Actor *a = orbiter_in(0, k);
                if (a->inst && !a->next)
                    AddActor(a);
            }
            orbitersUnlinked = 0;
        }
        return;
    }
    world3 = world_orbiter3();
    for (k = 0; k < G64_ORBITERS; k++) {
        Actor *a = orbiter_in(0, k);
        if (k == 2 && world3) {
            /* The world's orbiter is drawn as itself. */
            if (!a->next)
                AddActor(a);
        } else if (a->next) {
            SubActor(a);
            orbitersUnlinked = 1;
        }
    }
    for (i = 0; i < G64_MAX_PLAYERS; i++) {
        for (k = 0; k < G64_ORBITERS; k++) {
            Actor *a = i < slotCount && (i == 0 || slots[i]) && !(k == 2 && world3) ? orbiter_in(i, k) : NULL;
            Actor *p = proxyOrbiter[i][k];
            if (p && (!a || !a->inst || (ObjectInstance *)p->inst != (ObjectInstance *)a->inst)) {
                free_orbiter_proxy(i, k);
                p = NULL;
            }
            if (!p && a && a->inst) {
                proxyOrbiter[i][k] = new_proxy(a);
                orbitersUnlinked = 1;
            }
        }
    }
}

s32 G64OrbitersByProxy(void) {
    if (slotCount < 2)
        return 0;
    /* An orbiter made now is not in the actor list: with one player again, sync_orbiters puts it there. */
    orbitersUnlinked = 1;
    return 1;
}

/* 1 after G64SwapPlayer: the frame that is drawn next is a player's view, and the client loads the state again
   after it. What the draw changes in the globals then does not reach the game. */
static s32 viewDraw;

static void forget_cameo(void);
static s32 cameo_slot(void);

/* An actor that is not drawn: no size, no shadow. */
static void hide_actor(Actor *a) {
    ZERO_VECTOR(&a->scale);
    a->flags &= ~0x100;
}

void G64UpdateProxies(void) {
    s32 i, k;
    /* While a cameo plays, only its player's glove and ball are drawn. cameoOwn is that player's slot. */
    s32 hide = gameInfo.cameoActive && slotCount > 1;
    s32 cameoOwn = hide ? cameo_slot() : 0;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (s && proxyHand[i] && proxyBall[i]) {
            copy_to_proxy(proxyHand[i], &s->hand);
            copy_to_proxy(proxyBall[i], &s->ball);
            /* With slot `swapped` in the globals for a view, that slot holds the first player. */
            if (hide && (i == swapped ? 0 : i) != cameoOwn) {
                hide_actor(proxyHand[i]);
                hide_actor(proxyBall[i]);
            }
        }
    }
    /* The viewer's own glove and ball are the globals' in their view. Only a view may hide them: the frame the
       game draws for itself must leave the globals as they are. */
    if (hide && viewDraw && swapped != cameoOwn) {
        hide_actor(&hand);
        hide_actor(&ball);
    }
    sync_orbiters();
    for (i = 0; i < slotCount; i++) {
        /* With slot `swapped` in the globals for a view, its player is there and the first player is in that slot. */
        s32 store = !swapped ? i : i == 0 ? swapped : i == swapped ? 0 : i;
        for (k = 0; k < G64_ORBITERS; k++) {
            Actor *p = proxyOrbiter[i][k];
            if (p && !(k == 2 && world_orbiter3()) && (store == 0 || slots[store]) && orbiter_in(store, k)->inst) {
                copy_to_proxy(p, orbiter_in(store, k));
                /* A hidden player's orbiters are hidden too. Slot i's orbiters are player i's in every view. */
                if (hide && i != cameoOwn)
                    hide_actor(p);
            }
        }
    }
}

static void forget_tips(void);

/* Cameos (below): cameoStarter is the number plus 1 of the player who set the cameo off, or 0 for the first player.
   cameoMet is the player, plus 1, who last met a puzzle's condition on a player in the puzzle that is being run.
   cameoPending is that player for a puzzle whose conditions are met, until its cameo starts. cameoUsed: a cameo
   that a puzzle started plays. cameoHolding: the cameo held its player in this frame. */
static s32 cameoStarter, cameoMet, cameoUsed, cameoPending, cameoHolding;

static void forget_ball_owners(void);
/* Any ball (below): the real owner of the ball in each player's slot, and the players who die at the frame's end
   for a ball of theirs that another player lost (a bit each). */
static s8 ballOwner[G64_MAX_PLAYERS];
static u32 ballKill;

void G64ExtraPlayersFree(void) {
    texLoaded = 0;
    forget_ball_owners();
    forget_tips();
    forget_cameo();
    free_slots();
    if (startSlot) {
        free_slot_blocks((G64Slot *)startSlot);
        JallocFree(&startSlot);
    }
    if (globalCave)
        JallocFree(&globalCave);
    ready = 0;
    cameLate = 0;
    startPosSet = 0;
    forget_restarts();
}

/* Online: the start copy, once the level has set the first player up. */
static void take_start(void) {
    global_cave();
    if (!startSlot && hand_block_size() <= HAND_BLOCK_MAX) {
        startSlot = copy_globals();
        copy_restart(-1, 0);
    }
}

/* One extra player's part of the frame, in the order MainGameLoop runs the first player's (main.c). */
static void player_frame(s32 p) {
    Vec3 tempVect;
    if (deltaNewFrame) {
        OSContPad saved = controllerdata[ActiveController];
        controllerdata[ActiveController].button = g64Buttons[g64Member[p]];
        controllerdata[ActiveController].stick_x = g64StickX[g64Member[p]];
        controllerdata[ActiveController].stick_y = g64StickY[g64Member[p]];
        ReadController(ActiveController);
        controllerdata[ActiveController] = saved;
    }
    RestartPlayer();
    if (deltaNewFrame) {
        handControl.touchedActor = 0;
        ballControl.hitFlags &= ~8;
        ballFrameVel = ball.vel;
    }
    tempVect = ball.vel;
    oldHandPos = hand.pos;
    ball.flags &= ~0x40000000;
    UpdateActor(&hand);
    UpdateActor(&ball);
    ball.prevVel = tempVect;
    UpdateActor(&camera);
    handControl.flags &= ~1;
    if (gameInfo.mode != 5) {
        ProcessInput();
        UpdateActorPositionDependencies();
        MoveBall();
        MoveHand();
        hand.flags &= ~0x1000;
        handControl.prevRot = hand.rot;
    }
    if (deltaTick) {
        MoveCam();
        PlayerSpinCamera();
    }
    /* A quake rolls the camera, and the roll goes back to its aim. The game does this for the first player in
       CheckCameraForWater (water.c). */
    AddScaledVector(&cameraUpVect, &cameraUpVect, DeltaPow(0.49f), &cameraUpVectAim, DeltaTake(0.49f, 0.51f));
    MakeUnit(&cameraUpVect);
    /* In the look-around mode the camera looks 100 ahead of itself. The game does this for the first player in
       ProcessPersonalLoop (personal.c), after their part of the frame. */
    if (!cameraInfo.mode && !gamePaused) {
        tempVect.x = 0.0f;
        tempVect.y = 0.0f;
        tempVect.z = 100.0f;
        RotateVector2DXYZ(&tempVect, &tempVect, -camera.rot.x, 0);
        RotateVector2D(&tempVect, &tempVect, camera.rot.y);
        ADD_VECTORS(&cameraInfo.actor.pos, &camera.pos, &tempVect);
    }
}

void G64ExtraPlayersStep(void) {
    s32 i;
    /* The first player's own part of the frame ends here (main.c). */
    soundtable_set_owner(-1);
    if ((g64Players < 2 && !g64Online) || gamePaused || !GameBeingPlayed())
        return;
    /* The control room's scene plays from a recording of the first player (game.c, autoPlayback). The other players
       have no glove of their own there: they watch through the first player's camera. */
    if (gameInfo.level == SPACEBOSS_TWEEN)
        return;
    if (g64Online && !startPosSet) {
        startPos = hand.pos;
        startPosSet = 1;
    }
    /* The other players start as copies of the first, once the level has set the first player up (its start position,
       its first checkpoint) and its opening camera has given way to the normal one, which follows the glove or the
       ball. A copy taken earlier would keep the opening camera's fixed target. Online the start copy is taken then,
       even by a player alone, for the players who come later. */
    if (!ready) {
        /* A boss level's camera looks at its own point (boss.c). During a cameo the camera's mode is not 1: a copy
           taken then would keep the cameo's camera for good. */
        if (gameInfo.level == SPACEBOSS3 || gameInfo.level == CARNIVALBONUS) {
            /* The last space boss's level and the Carnival bonus level hold the camera with an override in every
               frame (boss.c, bonus.c): the level is ready when its opening cameo has ended. */
            if (gameInfo.cameoActive || frameCount <= 10 || cameraInfo.mode != 1)
                return;
        } else if ((cameraInfo.focus != &hand.pos && cameraInfo.focus != &ball.pos
                    && cameraInfo.focus != &bossFocalPoint)
                   || cameraInfo.overrideTimer || cameraInfo.mode != 1 || gameInfo.cameoActive)
            return;
        ready = 1;
        if (g64Online)
            take_start();
    }
    if (g64Players < 2)
        return;
    if (slotCount == 0)
        G64ExtraPlayersStart();
    /* The first player's part of the frame has run. */
    note_enemy_hits();
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s)
            continue;
        swap_slot(s);
        /* Sounds made in this player's own update are theirs (the sound table, soundtable.h). */
        soundtable_set_owner(g64Member[i]);
        logicSlot = i;
        player_frame(i);
        note_enemy_hits();
        logicSlot = 0;
        soundtable_set_owner(-1);
        swap_slot(s);
    }
}

/* The slot of the player with this number, or -1. */
static s32 slot_of(s32 player) {
    s32 i;
    for (i = 0; i < g64Players && i < G64_MAX_PLAYERS; i++)
        if (g64Member[i] == player)
            return i;
    return -1;
}

void G64SwapPlayer(s32 player) {
    s32 i = slot_of(player);
    /* The client draws this player's view next, and loads the state again after it (G64UpdateProxies). */
    viewDraw = 1;
    if (i >= 1 && i < slotCount && slots[i]) {
        swap_slot(slots[i]);
        swapped = swapped == i ? 0 : i;
    }
}

s32 G64IsProxy(Actor *a) {
    s32 i;
    for (i = 1; i < slotCount; i++)
        if (a == proxyHand[i] || a == proxyBall[i])
            return 1;
    return 0;
}

/* For tests: the length of a player's run of garibs taken in quick succession, or -1. */
s32 G64PlayerGaribRun(s32 player) {
    s32 i = slot_of(player);
    if (i < 0 || (i > 0 && (i >= slotCount || !slots[i])))
        return -1;
    return i ? ((G64Slot *)slots[i])->garibCount : garibCount;
}

/* collect.c counts the first player's run of garibs down once a frame. This does the same for the other players,
   each of whom has a run of their own. */
void G64GaribRuns(void) {
    s32 i;
    if (swapped || logicSlot)
        return;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (s && (s->garibTimer == 0 || --s->garibTimer == 0))
            s->garibCount = 0;
    }
}

/* For the draw of another player's glove or ball (a proxy): the texture words that differ from the ones in the
   game's memory now, which are the viewer's. from[] gets the words the display list holds, to[] that player's own.
   It returns how many, at most max. The renderer puts one for the other while the actor is drawn (object.c). */
s32 G64ActorTextures(Actor *a, u32 *from, u32 *to, s32 max) {
    s32 i, k, first, last, n = 0;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s || (a != proxyHand[i] && a != proxyBall[i]))
            continue;
        /* The ball has two models with a word pair each: the crystal ball's (2 and 3) and the usual one's (0 and
           1). Only the words of the model that is drawn are passed: the two pairs can hold the same value. */
        if (a == proxyBall[i]) {
            first = s->ballControl.type == BALL_MODE_CRYSTAL ? 2 : 0;
            last = first + 1;
        } else {
            first = 6;
            last = 9;
        }
        for (k = first; k <= last && n < max; k++) {
            u32 *w = tex_word(k);
            if (w && *w && s->texWords[k] && *w != s->texWords[k]) {
                from[n] = *w;
                to[n] = s->texWords[k];
                n++;
            }
        }
        return n;
    }
    return 0;
}

/* For tests: a player's texture word k (tex_word), and the type of their ball. -1 when the player has no glove. */
s32 G64PlayerTexture(s32 player, s32 k) {
    s32 i = slot_of(player);
    u32 *w;
    if (k < 0 || k >= G64_TEX_WORDS || i < 0 || (i > 0 && (i >= slotCount || !slots[i])))
        return -1;
    w = tex_word(k);
    return (s32)((i ? ((G64Slot *)slots[i])->texWords[k] : w ? *w : 0) & 0x7FFFFFFF);
}

/* For tests: the player's ball starts to change into the next kind, as the R button makes it (HandTransformBall),
   with that player in the globals. */
void HandTransformBall(void);
void G64TestChangeBall(s32 player) {
    s32 i = slot_of(player);
    if (i == 0) {
        HandTransformBall();
    } else if (i > 0 && G64HazardBegin(i)) {
        HandTransformBall();
        G64HazardEnd(i);
    }
}

s32 G64PlayerBallType(s32 player) {
    s32 i = slot_of(player);
    if (i < 0 || (i > 0 && (i >= slotCount || !slots[i])))
        return -1;
    return i ? ((G64Slot *)slots[i])->ballControl.type : ballControl.type;
}

/* Each player's color as 0xRRGGBB, multiplied into the glove's and ball's colors. Player 1 keeps the game's own. */
static const u32 playerColor[8] = {0, 0x60A0FF, 0x70FF70, 0xFFE040, 0xFF6060, 0xC070FF, 0xFFA040, 0x60FFFF};

u32 g64PlayerColor[G64_MAX_PLAYERS][2];

u32 G64TitleTint(Actor *a);

u32 G64ActorTint(Actor *a) {
    s32 i, slot = -1, p, isBall = 0;
    u32 c;
    /* The online program's start screen shows the glove in the player's colors (g64title.c). */
    c = G64TitleTint(a);
    if (c)
        return c;
    if (a == &hand || a == &ball) {
        slot = swapped;
        isBall = a == &ball;
    } else {
        for (i = 1; i < slotCount; i++)
            if (a == proxyHand[i] || a == proxyBall[i]) {
                slot = i == swapped ? 0 : i;
                isBall = a == proxyBall[i];
            }
    }
    if (slot < 0)
        return 0;
    p = g64Member[slot];
    if (p < 0 || p >= G64_MAX_PLAYERS)
        return 0;
    c = g64PlayerColor[p][isBall];
    if (c & G64_COLOR_SET) {
        /* A chosen black stays black: 0 means no tint. */
        c &= 0xFFFFFF;
        return c ? c : 0x010101;
    }
    if (p == 0 || (g64Players < 2 && g64Member[0] == 0))
        return 0;
    return playerColor[p % 8 ? p % 8 : 1];
}

s32 G64PlayerPosition(s32 player, f32 *x, f32 *y, f32 *z) {
    const Actor *a;
    s32 i = slot_of(player);
    if (i == 0) {
        a = &hand;
    } else if (i > 0 && i < slotCount && slots[i]) {
        a = &((G64Slot *)slots[i])->hand;
    } else {
        return 0;
    }
    *x = a->pos.x;
    *y = a->pos.y;
    *z = a->pos.z;
    return 1;
}

Platform *TagToFirstPlatform(s32 tag);

s32 G64PlatformPosition(s32 tag, f32 *x, f32 *y, f32 *z) {
    Platform *platform = TagToFirstPlatform(tag);
    if (platform == NULL)
        return 0;
    *x = platform->actor.pos.x;
    *y = platform->actor.pos.y;
    *z = platform->actor.pos.z;
    return 1;
}

s32 G64BallPosition(s32 player, f32 *x, f32 *y, f32 *z) {
    const Actor *a;
    s32 i = slot_of(player);
    if (i == 0) {
        a = &ball;
    } else if (i > 0 && i < slotCount && slots[i]) {
        a = &((G64Slot *)slots[i])->ball;
    } else {
        return 0;
    }
    *x = a->pos.x;
    *y = a->pos.y;
    *z = a->pos.z;
    return 1;
}

void G64NudgePlayer(s32 player, f32 dx, f32 dy, f32 dz) {
    s32 i = slot_of(player);
    Actor *h, *b;
    if (i == 0) {
        h = &hand;
        b = &ball;
    } else if (i > 0 && i < slotCount && slots[i]) {
        h = &((G64Slot *)slots[i])->hand;
        b = &((G64Slot *)slots[i])->ball;
    } else {
        return;
    }
    h->pos.x += dx, h->pos.y += dy, h->pos.z += dz;
    b->pos.x += dx, b->pos.y += dy, b->pos.z += dz;
}

s32 G64CaveFramesHere(void) {
    return frameCount - global_cave()->arrivedAt;
}

s32 G64CaveDue(void) {
    return global_cave()->due;
}

s32 G64CaveTarget(void) {
    s32 t = global_cave()->target;
    return t >= 1 && t <= 7 ? t : 1;
}

s32 G64CaveTargetsInAir(void) {
    s32 i, top = 0;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        /* A swapped slot holds the player in the globals' part, which is the other player now. */
        if (s && s->caveStage != 0 && s->cave.target > top)
            top = s->cave.target;
    }
    return top;
}

void G64SetCaveTarget(s32 tag) {
    global_cave()->target = tag;
}

void G64SetCaveArrival(s32 frame, s32 due) {
    G64Cave *c = global_cave();
    c->arrivedAt = frame;
    c->due = due;
}

void G64CavePlayers(void) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s)
            continue;
        swap_slot(s);
        soundtable_set_owner(g64Member[i]);
        G64CavePlayer();
        soundtable_set_owner(-1);
        swap_slot(s);
    }
}

extern s32 MAX_SFX_DIST;

s32 G64SoundListener(s32 player, f32 *pos, f32 *yaw, s32 *maxDist) {
    s32 i = slot_of(player);
    const Actor *c;
    if (i < 0)
        return 0;
    /* A player whose slot does not exist yet (the level's opening) sees and hears through the globals' camera. */
    if (i > 0 && i < slotCount && slots[i])
        c = &((G64Slot *)slots[i])->camera;
    else
        c = &camera;
    pos[0] = c->pos.x;
    pos[1] = c->pos.y;
    pos[2] = c->pos.z;
    *yaw = c->rot.y;
    *maxDist = MAX_SFX_DIST;
    return 1;
}

s32 G64CameraTarget(s32 player, f32 *x, f32 *y, f32 *z) {
    s32 i = slot_of(player);
    const Actor *a;
    if (i < 0 || (i > 0 && (i >= slotCount || !slots[i])))
        return 0;
    a = i == 0 ? &cameraInfo.actor : &((G64Slot *)slots[i])->cameraInfo.actor;
    *x = a->pos.x;
    *y = a->pos.y;
    *z = a->pos.z;
    return 1;
}

s32 G64HasPlayer(s32 player) {
    return slot_of(player) >= 0;
}

extern Vec3 handStart;

/* Moves an actor by d, and stops it. */
static void shift_actor(Actor *a, const Vec3 *d) {
    a->pos.x += d->x, a->pos.y += d->y, a->pos.z += d->z;
    a->prevPos.x += d->x, a->prevPos.y += d->y, a->prevPos.z += d->z;
    a->vel.x = a->vel.y = a->vel.z = 0.0f;
}

static void player_exits(s32 player, s32 type, s32 complete);
s32 IsHandHoldingBall(void);
/* How far above the checkpoint a ball left behind is put. */
#define G64_BALL_DROP 30.0f

void G64PlayerAction(s32 player, s32 action) {
    s32 i = slot_of(player);
    Actor *h, *b, *c, *ci;
    HandControl *hc;
    Vec3 target, d;
    if (i < 0 || (i > 0 && (i >= slotCount || !slots[i])))
        return;
    if (action == G64_ACTION_HUB) {
        player_exits(player, HUB_EXIT, 0);
        return;
    }
    if (action != G64_ACTION_CHECKPOINT)
        return;
    if (i == 0) {
        h = &hand, b = &ball, c = &camera, ci = &cameraInfo.actor;
        hc = &handControl;
    } else {
        G64Slot *s = slots[i];
        h = &s->hand, b = &s->ball, c = &s->camera, ci = &s->cameraInfo.actor;
        hc = (HandControl *)s->handBlock;
    }
    /* The player's own last checkpoint, where a respawn puts the glove; the level's start before the first. */
    if (hc->checkpoint != NULL)
        target = hc->checkpoint->actor.pos;
    else
        target = handStart;
    d.x = target.x - h->pos.x;
    d.y = target.y - h->pos.y;
    d.z = target.z - h->pos.z;
    shift_actor(h, &d);
    shift_actor(c, &d);
    shift_actor(ci, &d);
    /* A slot's ball holds the globals' address as its link while its own glove holds it (G64CheckExit). */
    if (i == 0 ? IsHandHoldingBall() || ball.link == &hand : b->link == &hand) {
        shift_actor(b, &d);
    } else {
        /* A ball left behind comes to the checkpoint too, as in the game's own restart, just above the glove. */
        d.x = target.x - b->pos.x;
        d.y = target.y + G64_BALL_DROP - b->pos.y;
        d.z = target.z - b->pos.z;
        shift_actor(b, &d);
    }
}


/* A player who came after the level started, in slot i: moved with their ball and camera to the position to (or left
   where they are for NULL), and afresh (G64Arrive, game.c). */
static void arrive(s32 i, const Vec3 *to) {
    G64Slot *n = slots[i];
    Vec3 d;
    if (to) {
        d.x = to->x - n->hand.pos.x;
        d.y = to->y - n->hand.pos.y;
        d.z = to->z - n->hand.pos.z;
        shift_actor(&n->hand, &d);
        shift_actor(&n->ball, &d);
        shift_actor(&n->camera, &d);
        shift_actor(&n->cameraInfo.actor, &d);
    }
    if (g64Online) {
        swap_slot(n);
        G64Arrive();
        swap_slot(n);
    }
}

void G64AddPlayer(s32 player) {
    s32 i;
    if (player < 0 || player >= G64_MAX_PLAYERS || slot_of(player) >= 0 || g64Players >= G64_MAX_PLAYERS)
        return;
    i = g64Players;
    g64Member[i] = player;
    g64Players++;
    cameLate |= 1u << player;
    /* Before the slots exist, G64ExtraPlayersStart makes one for every member, and one for a player who came late as
       here. After, or online once the start copy exists, the new player gets one now. */
    if (slotCount > 0 || (startSlot && hand_block_size() <= HAND_BLOCK_MAX)) {
        if (slotCount == 0)
            slotCount = 1;
        new_late_slot(i);
        slotCount = g64Players;
    }
}

/* The players who were present in the last frame's inputs, one bit each (G64SetPresent). Part of the game's state. */
static u32 lastPresent = 1;
/* Players who took an exit: they leave at the start of the next frame (leaving), and take no other exit (gone). */
static u32 leaving, gone;

s32 G64ExitNeedsBall(Platform *platform, s32 *useHand);
void G64ExitTarget(s32 type, s32 *level, s32 *world);
void G64ExitSave(s32 type, s32 complete, G64Save *save);
void G64ReadSave(G64Save *save);
void G64WriteSave(s32 world, const G64Save *save);

/* A level pack (src/client/pack.h): the level that the pack's level file takes the place of, or -1, and where that
   file is: its offset from the start of the ROM buffer, where the client put it after the ROM's own bytes, and its
   size. Every machine of a session has the same pack, so these are the same in every game. The level's loader
   reads the file from there (LoadLandscapeFile, src/port/loaders.c). */
s32 g64PackLevel = -1;
u32 g64PackRom, g64PackSize;

void G64SetPack(s32 level, u32 rom, u32 size) {
    g64PackLevel = size ? level : -1;
    g64PackRom = rom;
    g64PackSize = size;
}

/* One world at a time (the host's rule for the session, g64OneWorld). The session is in the world where players
   are: g64SessionWorld, or 0 while everyone is in the hub. The host works it out and sends it to every game as an
   event on a confirmed frame (G64SetSessionWorld). A portal from the hub to another world is shut: a player who
   walks into it stays in the hub, and portalShut holds the session's world for them for that frame, which the
   client reads to tell them why (G64PortalShut). */
s32 g64OneWorld;
static u8 g64SessionWorld;
static u8 portalShut[G64_MAX_PLAYERS];

void G64SetSessionWorld(s32 world) {
    if (world >= 0 && world < NUM_WORLDS)
        g64SessionWorld = (u8)world;
}

s32 G64SessionWorld(void) {
    return g64SessionWorld;
}

s32 G64PortalShut(s32 player) {
    return player >= 0 && player < G64_MAX_PLAYERS ? portalShut[player] : 0;
}

/* A player takes an exit: it is recorded with where it leads, and the player leaves the game at the next frame. */
static void player_exits(s32 player, s32 type, s32 complete) {
    u32 bit = 1u << player;
    G64ExitRecord *r;
    if ((gone & bit) || g64ExitCount >= G64_MAX_PLAYERS)
        return;
    if (g64OneWorld && type >= ATLANTIS_EXIT && type <= SPACE_EXIT && g64SessionWorld != HUB &&
        g64SessionWorld != ATLANTIS + (type - ATLANTIS_EXIT)) {
        portalShut[player] = g64SessionWorld;
        return;
    }
    gone |= bit;
    leaving |= bit;
    r = &g64Exits[g64ExitCount++];
    r->player = player;
    r->type = type;
    r->complete = complete;
    G64ExitTarget(type, &r->level, &r->world);
    G64ExitSave(type, complete, &r->save);
}

/* A bonus level ends for one player: they won it or lost it. Online the player in the globals takes the exit alone
   and the function returns 1. Otherwise it returns 0, and the caller ends the level for everyone as the game does. */
s32 G64PlayerExits(s32 type, s32 complete) {
    if (!g64Online || swapped)
        return 0;
    player_exits(g64Member[logicSlot], type, complete);
    return 1;
}

void G64CheckExit(Platform *platform) {
    s32 i, useHand, needBall;
    if (!deltaTick)
        return;
    needBall = G64ExitNeedsBall(platform, &useHand);
    for (i = 0; i < g64Players && i < G64_MAX_PLAYERS; i++) {
        const Actor *h, *b;
        s32 holding;
        if (i == 0) {
            h = &hand;
            b = &ball;
            holding = IsHandHoldingBall() || ball.link == &hand;
        } else if (i < slotCount && slots[i]) {
            /* A slot's ball holds the globals' address as its link while its own glove holds it. */
            h = &((G64Slot *)slots[i])->hand;
            b = &((G64Slot *)slots[i])->ball;
            holding = b->link == &hand;
        } else {
            continue;
        }
        if (needBall && !holding)
            continue;
        if (DistanceBetweenPoints((Vec3 *)(useHand ? &h->pos : &b->pos), &platform->actor.pos) < platform->actor.radius)
            player_exits(g64Member[i], platform->exitType, 1);
    }
}

s32 G64FindExit(s32 type, f32 *x, f32 *y, f32 *z) {
    Platform *platform;
    for (platform = platformList.head.next; platform != &platformList.head; platform = platform->next) {
        if (platform->exitType == type) {
            *x = platform->actor.pos.x;
            *y = platform->actor.pos.y;
            *z = platform->actor.pos.z;
            return 1;
        }
    }
    return 0;
}

void G64PlacePlayer(s32 player, f32 x, f32 y, f32 z) {
    s32 i = slot_of(player);
    Actor *h;
    if (i == 0) {
        h = &hand;
    } else if (i > 0 && i < slotCount && slots[i]) {
        h = &((G64Slot *)slots[i])->hand;
    } else {
        return;
    }
    h->pos.x = x;
    h->pos.y = y;
    h->pos.z = z;
}

void G64EveryoneExits(s32 type, s32 complete) {
    s32 i;
    for (i = 0; i < g64Players && i < G64_MAX_PLAYERS; i++)
        player_exits(g64Member[i], type, complete);
}

/* The start record, until a level start applies it. */
static s32 recordSet, recordWorld;
static G64Save recordSave;

void G64SetStartRecord(s32 world, const G64Save *save) {
    recordSet = 1;
    recordWorld = world;
    recordSave = *save;
}

void G64ApplyStartRecord(void) {
    if (!recordSet)
        return;
    recordSet = 0;
    G64WriteSave(recordWorld, &recordSave);
}

G64TestExit g64TestExits[G64_TEST_EXITS] = {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0},
                                            {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}};
/* Frames this game has run since its level started (G64SetPresent counts them). */
static u32 framesRun;

/* For scripts (src/port/g64lua.c). */
void StopDrawing(char *name);

/* Marks the level as one that cannot be played: the game's own state for a level without its file (mode 13). The
   game then shows its error screen and runs none of the level's logic. The level parser calls this where a level
   file names something the game does not have. */
void G64Unplayable(const char *what) {
    if (gameInfo.mode == 13)
        return;
    sprintf(gameInfo.message, "LEVEL UNPLAYABLE - %.24s", what);
    StopDrawing("unplayable");
    gameInfo.mode = 13;
}

/* For the program that draws the picture: 1 while the level cannot be played. */
s32 G64LevelUnplayable(void) {
    return gameInfo.mode == 13;
}

s32 g64ParseLogOn, g64ParseLogCount;
s32 g64ParseLog[G64_PARSE_LOG][2];

void G64ParseTrace(s32 offset, s32 token) {
    if (!g64ParseLogOn)
        return;
    /* The level's name comes right after the file's size: the first token is the first thing after it. A record
       that starts again is one whose offset went back. */
    if (g64ParseLogCount > 0 && offset <= g64ParseLog[g64ParseLogCount - 1][0])
        g64ParseLogCount = 0;
    if (g64ParseLogCount < G64_PARSE_LOG) {
        g64ParseLog[g64ParseLogCount][0] = offset;
        g64ParseLog[g64ParseLogCount][1] = token;
        g64ParseLogCount++;
    }
}

void G64LuaEvent(int kind, int player);
void HurtHand(s32 type, s16 damage);
static s32 slot_used(s32 i);

/* For scripts: hurts a player's glove by one point, as an enemy's touch does, by the game's own rules (HurtHand).
   A glove that was just hurt, or that reads a tip, is not hurt again. The player's state is in the game's
   variables for the call. */
void G64HurtPlayer(s32 who) {
    s32 i = slot_of(who);
    if (i == 0) {
        HurtHand(HURT_FALL, 1);
    } else if (i > 0 && G64HazardBegin(i)) {
        HurtHand(HURT_FALL, 1);
        G64HazardEnd(i);
    }
}

/* For scripts: the level's puzzle variables (puzzle.c, puzzleVal), 100 whole numbers that the level's puzzles test
   and set. A script reads and sets them, and so starts what the level's maker tied to a variable: a group of garibs,
   an enemy, a platform's move. -1 and nothing for a number out of range. */
s32 G64PuzzleVar(s32 n) {
    return n >= 0 && n < 100 ? puzzleVal[n] : -1;
}

void G64SetPuzzleVar(s32 n, s32 value) {
    if (n >= 0 && n < 100)
        puzzleVal[n] = value;
}

/* For scripts: sets a player's health, 1 to 3. */
void G64SetPlayerHealth(s32 who, s32 health) {
    s32 i = slot_of(who);
    if (i < 0 || !slot_used(i) || health < 1 || health > 3)
        return;
    slot_player(i)->health = health;
}

/* For scripts: a life for player who, by the game's own extra life (GainExtraLife), with its sound. A player keeps at
   most 99 lives. */
void GainExtraLife(void);
void G64GivePlayerLife(s32 who) {
    s32 i = slot_of(who);
    if (i == 0) {
        if (player.lives < 99)
            GainExtraLife();
    } else if (i > 0 && G64HazardBegin(i)) {
        if (player.lives < 99)
            GainExtraLife();
        G64HazardEnd(i);
    }
}

/* For scripts: one of the game's pick-ups at a place, as a puzzle's action makes one (CreateNewPickup). kind is the
   game's number for it, 0 to 14. The level reader leaves out kinds 7 and 9, and so does this. The pick-up's object
   must be in the level's banks: without it there is no pick-up, and the level plays on. duration is how long its
   effect lasts, in frames. A level holds at most 128 pick-ups. Returns 1 when the pick-up is there. */
struct Collectable *CreateNewPickup(u8 type, Vec3 *pos, s16 time, s16 duration);
extern PTR32(char) collectableFileName[];
void FindObject(PTR32(ObjectInstance) *objCPtr, u32 objID, char *name, s32 vital);
u32 UpdateCRC(char *ptr);
s32 G64SpawnPickup(s32 kind, f32 x, f32 y, f32 z, s32 duration) {
    PTR32(ObjectInstance) inst;
    Vec3 pos;
    if (kind < 0 || kind > 14 || kind == 7 || kind == 9 || duration < 0 || duration > 30000 ||
        collectableList.count >= 128)
        return 0;
    FindObject(&inst, UpdateCRC(collectableFileName[kind]), NULL, 0);
    if (inst == NULL)
        return 0;
    pos.x = x;
    pos.y = y;
    pos.z = z;
    return CreateNewPickup((u8)kind, &pos, 0, (s16)duration) != NULL;
}

/* Tells the script that something happened to the player whose state is in the game's variables now
   (src/port/g64lua.c: 1 a garib taken, 2 a death). Without a script it does nothing. */
void G64ScriptEvent(s32 kind) {
    G64LuaEvent(kind, g64Member[logicSlot]);
}

/* 1 when the player has a glove in this level. A player who has just come has none for a frame (G64HasPlayer is
   already 1 then), and a script that asked for their place would get nothing. */
s32 G64PlayerInLevel(s32 player) {
    s32 i = slot_of(player);
    return i >= 0 && slot_used(i);
}

s32 G64FramesRun(void) {
    return (s32)framesRun;
}

s32 G64Level(void) {
    return gameInfo.level;
}

void G64LuaFrame(void);

void G64SetPresent(u32 present) {
    s32 p, i;
    g64ExitCount = 0;
    memset(portalShut, 0, sizeof portalShut);
    /* A test's exits at set frames (g64TestExits): the same on every machine, since every copy holds them. */
    for (i = 0; i < G64_TEST_EXITS; i++) {
        G64TestExit *t = &g64TestExits[i];
        if (t->level == gameInfo.level && (u32)t->frame == framesRun && slot_of(t->player) >= 0)
            player_exits(t->player, t->type, 1);
    }
    framesRun++;
    /* A level that cannot be played sends its players back to the hub, online. A hub level that cannot be played
       keeps them: the hub is where the exit leads. */
    if (gameInfo.mode == 13 && g64Online && gameInfo.level > HUB8)
        G64EveryoneExits(HUB_EXIT, 0);
    for (p = 0; p < G64_MAX_PLAYERS; p++)
        if (leaving & (1u << p))
            G64RemovePlayer(p);
    leaving = 0;
    for (p = 0; p < G64_MAX_PLAYERS; p++) {
        u32 bit = 1u << p;
        if ((present & bit) && !(lastPresent & bit)) {
            gone &= ~bit;
            G64AddPlayer(p);
        } else if (!(present & bit) && (lastPresent & bit)) {
            G64RemovePlayer(p);
        }
    }
    lastPresent = present;
    /* A level pack's script runs once a frame, here: after the players of this frame are in, before the game's
       own step (src/port/g64lua.c). Without a script it does nothing. */
    G64LuaFrame();
}

static void forget_tips(void);
static void forget_tip(s32 player);

void G64StartPlayers(u32 present) {
    s32 p;
    forget_restarts();
    forget_ball_owners();
    forget_tips();
    cameLate = 0;
    g64Players = 0;
    for (p = 0; p < G64_MAX_PLAYERS; p++)
        if (present & (1u << p))
            g64Member[g64Players++] = p;
    if (g64Players == 0) {
        g64Member[0] = 0;
        g64Players = 1;
    }
    for (p = g64Players; p < G64_MAX_PLAYERS; p++)
        g64Member[p] = 0;
    lastPresent = present;
    leaving = gone = 0;
    g64ExitCount = 0;
    framesRun = 0;
}

void G64RemovePlayer(s32 player) {
    s32 i = slot_of(player), j, blocks = 1;
    forget_tip(player);
    if (cameoStarter == player + 1)
        cameoStarter = 0;
    {
        /* Any ball: the ball in the leaving player's slot goes with them. If it was another player's, the ball
           the leaving player owned, which someone else has, becomes that player's. */
        s32 q, p2 = player & (G64_MAX_PLAYERS - 1);
        for (q = 0; q < G64_MAX_PLAYERS; q++)
            if (q != p2 && ballOwner[q] == p2)
                ballOwner[q] = ballOwner[p2];
        ballOwner[p2] = (s8)p2;
        ballKill &= ~(1u << p2);
    }
    if (i < 0 || g64Players < 2)
        return;
    if (i == 0 && slotCount <= 1) {
        /* The globals, and a respawn running in them, stay with the next player. */
        move_restart(g64Member[1], player);
    }
    forget_restart(player);
    /* The players' slots move: the orbiters' proxies are made again (sync_orbiters). */
    free_orbiter_proxies();
    if (i == 0 && slotCount > 1) {
        /* The player in slot 1 moves into the globals, and slot 1 now holds the leaving player. Its actors' blocks are
           the level's own, and some are not on the heap (a ball's phys can point to defaultBehaviour): they stay. */
        swap_slot(slots[1]);
        /* Their camera zones come with them, and the leaving player's go into the slot that is freed. */
        G64PuzzleCameraState(1);
        g64Member[0] = g64Member[1];
        i = 1;
        blocks = 0;
    }
    if (slotCount > i) {
        /* The leaving player's slam ring would be nobody's and hurt the others: it ends now. */
        if (slots[i] && ((HandControl *)((G64Slot *)slots[i])->handBlock)->footprint)
            KillFootprint(((HandControl *)((G64Slot *)slots[i])->handBlock)->footprint);
        free_slot(i, blocks);
        for (j = i; j + 1 < slotCount; j++) {
            slots[j] = slots[j + 1];
            proxyHand[j] = proxyHand[j + 1];
            proxyBall[j] = proxyBall[j + 1];
        }
        slots[slotCount - 1] = NULL;
        proxyHand[slotCount - 1] = NULL;
        proxyBall[slotCount - 1] = NULL;
        slotCount--;
    }
    for (j = i; j + 1 < g64Players; j++)
        g64Member[j] = g64Member[j + 1];
    g64Players--;
    g64Member[g64Players] = 0;
}
/* Enemies and bullets (phase 3). The game's enemies and bullets read the glove, the ball and the camera in the
   globals. Before an enemy's or a bullet's update, the player it should look at is swapped into the globals, and
   swapped out after it. That player is, in order:
   - the player the enemy or bullet holds, or who holds it (a glove in a bubble, a ball in a mouth). For a Baby Spank
     this also counts the ball its partner holds: the two pass one ball between them;
   - for an enemy or a bullet that a glove or a ball hit, that player (the hit's effect comes in its next update).
     An enemy is hit in the players' parts of the frame; a bullet only in its own update;
   - for a bullet that homes in on a glove or a ball, the player whose glove or ball it is. A bullet aimed before
     there was a second player has no record, and keeps the player in the globals;
   - the nearest player, by the nearer of their glove and their ball. A player who is dying counts only when every
     player is. A tie goes to the lower slot.
   With one player nothing here runs. */

/* For each bullet: the player it homes in on, and the player whose glove or ball hit it since its last update (-1:
   none), by player number. A bullet is added when it is made, and taken out when it goes (bullet.c). */
#define G64_BULLETS 256
/* The bullet flags that a collision with a glove or a ball sets (collision.c), and its next update reads. Bullets
   are not in the actor list, so only the bullet's own update sets them, against the player in the globals then. */
#define BULLET_HIT_FLAGS 0x44000000
typedef struct G64BulletOwner {
    PTR32(struct Bullet) bullet;
    s32 player;
    s32 hitBy;
} G64BulletOwner;
static G64BulletOwner bulletOwner[G64_BULLETS];
/* The bullet that G64BulletGone took out during the current update. */
static PTR32(struct Bullet) bulletGone;

static HandControl *slot_control(G64Slot *s) {
    return (HandControl *)s->handBlock;
}

/* The glove, the ball and their control blocks of slot i, or of the globals for 0. */
static Actor *slot_glove(s32 i) {
    return i ? &((G64Slot *)slots[i])->hand : &hand;
}
static Actor *slot_ball(s32 i) {
    return i ? &((G64Slot *)slots[i])->ball : &ball;
}
static HandControl *slot_hand_control(s32 i) {
    return i ? slot_control(slots[i]) : &handControl;
}
static BallControl *slot_ball_control(s32 i) {
    return i ? &((G64Slot *)slots[i])->ballControl : &ballControl;
}

static s32 slot_used(s32 i) {
    return i == 0 || (i < slotCount && slots[i]);
}

/* The slot of a recorded player, or -1 when they are not in this game. */
static s32 recorded_slot(s32 player) {
    s32 i;
    if (player < 0)
        return -1;
    i = slot_of(player);
    return i >= 0 && slot_used(i) ? i : -1;
}

/* 1 when the player in slot i holds the actor or is held by it. */
static s32 slot_holds(s32 i, Actor *a) {
    return slot_glove(i)->link == a || slot_ball(i)->link == a || slot_hand_control(i)->attachedTo == a ||
           slot_ball_control(i)->enemy == a;
}

/* The squared distance from a point to the nearer of a slot's glove and ball. */
static f32 slot_distance(s32 i, const Vec3 *p) {
    f32 h = DistanceBetweenPointsSquared(&slot_glove(i)->pos, (Vec3 *)p);
    f32 b = DistanceBetweenPointsSquared(&slot_ball(i)->pos, (Vec3 *)p);
    return b < h ? b : h;
}

/* The Baby Spank that passes the ball with this one (NMEProcessBabySpank: the pair is enemies 1 and 2), or NULL. */
static Actor *spank_partner(Actor *a) {
    s32 j;
    if (a->type != BABY_SPANK || !a->state)
        return NULL;
    j = 3 - a->state->index;
    if (j < 0 || j >= numberOfEnemies || enemies[j].actor.type != BABY_SPANK)
        return NULL;
    return &enemies[j].actor;
}

/* The slot an actor's update should see in the globals, by the rules above. preferred: the slot of the player that
   hit a bullet or that it homes in on, or -1. */
static s32 choose_target(Actor *a, s32 preferred) {
    s32 i, best = -1, alive;
    f32 bestDist = 0.0f;
    Actor *partner = spank_partner(a);
    for (i = 0; i < slotCount; i++)
        if (slot_used(i) && slot_holds(i, a))
            return i;
    if (partner)
        for (i = 0; i < slotCount; i++)
            if (slot_used(i) && slot_holds(i, partner))
                return i;
    if (preferred >= 0)
        return preferred;
    /* In the last space boss's level every glove is held at one place (boss.c), and the nearest would always be the
       first player. The boss takes the living players in turn, 64 frames each. */
    if (gameInfo.level == SPACEBOSS3) {
        for (alive = 0; alive < slotCount; alive++) {
            i = (frameCount / 64 + alive) % slotCount;
            if (slot_used(i) && !slot_hand_control(i)->deathTimer)
                return i;
        }
    }
    for (alive = 1; alive >= 0 && best < 0; alive--) {
        for (i = 0; i < slotCount; i++) {
            f32 d;
            if (!slot_used(i) || (alive && slot_hand_control(i)->deathTimer))
                continue;
            d = slot_distance(i, &a->pos);
            if (best < 0 || d < bestDist) {
                best = i;
                bestDist = d;
            }
        }
    }
    return best < 0 ? 0 : best;
}

static s32 target_begin(Actor *a, s32 preferred) {
    s32 i;
    if (slotCount < 2 || swapped || logicSlot)
        return 0;
    i = choose_target(a, preferred);
    if (i > 0) {
        swap_slot(slots[i]);
        logicSlot = i;
    }
    return i;
}

void G64TargetEnd(s32 i) {
    if (i > 0) {
        swap_slot(slots[i]);
        logicSlot = 0;
    }
}

/* 1 while an enemy's hit flags are set (collision.c sets them; UpdateEnemies clears them in each tick). */
static s32 enemy_hit(s32 x) {
    return (enemies[x].brain.flags & 8) || (enemies[x].actor.flags & 0x4000000);
}

static void note_enemy_hits(void) {
    s32 x;
    if (slotCount < 2)
        return;
    for (x = 0; x < numberOfEnemies && x < G64_ENEMIES; x++)
        if (enemy_hit(x) && !enemyHitBy[x])
            enemyHitBy[x] = g64Member[logicSlot] + 1;
}

s32 G64EnemyBegin(s32 x) {
    s32 preferred = -1;
    if (slotCount < 2 || x < 0 || x >= G64_ENEMIES)
        return 0;
    if (enemy_hit(x) && enemyHitBy[x])
        preferred = recorded_slot(enemyHitBy[x] - 1);
    return target_begin(&enemies[x].actor, preferred);
}

void G64EnemyEnd(s32 x, s32 i) {
    /* A hit in the enemy's own update is the swapped-in player's. */
    if (slotCount >= 2 && x >= 0 && x < G64_ENEMIES && enemy_hit(x) && !enemyHitBy[x])
        enemyHitBy[x] = g64Member[i] + 1;
    G64TargetEnd(i);
}

/* Inside an enemy's update the enemy's target is in the globals (G64EnemyBegin). Some of an enemy's code is for
   every player: a boss's camera, a foot that squashes whoever is under it. G64OtherPlayers gives the number of
   players to go through (0 with one player). G64OtherBegin(k) puts player k in the globals in place of the target
   and returns 1; it returns 0 for the target, whose turn the enemy's own code is. G64OtherEnd(k) puts the target
   back. */
static s32 otherTarget;

s32 G64OtherPlayers(void) {
    return slotCount >= 2 && !swapped ? slotCount : 0;
}

s32 G64OtherBegin(s32 k) {
    if (slotCount < 2 || swapped || k < 0 || k >= slotCount || k == logicSlot || !slot_used(k))
        return 0;
    otherTarget = logicSlot;
    if (otherTarget)
        swap_slot(slots[otherTarget]);
    if (k)
        swap_slot(slots[k]);
    logicSlot = k;
    return 1;
}

void G64OtherEnd(s32 k) {
    if (k)
        swap_slot(slots[k]);
    if (otherTarget)
        swap_slot(slots[otherTarget]);
    logicSlot = otherTarget;
}

/* The same height for every player's camera: a level-wide change (the Atlantis boss's end). */
void G64SetLevelCamHeight(s32 height) {
    s32 i;
    levelCamHeight = height;
    for (i = 1; i < slotCount; i++) {
        if (slots[i])
            ((G64Slot *)slots[i])->levelCamHeight = height;
    }
    /* A player who comes later starts from the start copy. */
    if (startSlot)
        ((G64Slot *)startSlot)->levelCamHeight = height;
}

/* The first space boss's level ends when a player brings their ball to the robot's platform. From then until the
   exit the robot's code deals with that player, the carrier: the number, plus 1, is kept here. G64CarrierSet(1) notes
   the player in the globals, and G64CarrierSet(0) that there is none. G64CarrierBegin puts the carrier in the globals when another player is there, and
   returns their slot (0 when nothing was swapped, -1 when the carrier has left the game); G64CarrierEnd undoes it. */
static s32 carrier;

void G64CarrierSet(s32 on) {
    carrier = on ? g64Member[logicSlot] + 1 : 0;
}

s32 G64CarrierBegin(void) {
    s32 k;
    /* With one player offline that player is the carrier. */
    if (swapped || !carrier || (g64Players < 2 && !g64Online))
        return 0;
    k = slot_of(carrier - 1);
    if (k < 0)
        return -1;
    return G64OtherBegin(k) ? k + 1 : 0;
}

void G64CarrierEnd(s32 k) {
    if (k > 0)
        G64OtherEnd(k - 1);
}

void G64EnemyHitsCleared(s32 x) {
    if (x >= 0 && x < G64_ENEMIES && !enemy_hit(x))
        enemyHitBy[x] = 0;
}

s32 G64EnemyNear(Actor *a, f32 rangeSq) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s)
            continue;
        if (s->ballControl.enemy == a || DistanceBetweenPointsSquared(&a->pos, &s->camera.pos) <= rangeSq)
            return 1;
    }
    return 0;
}

static G64BulletOwner *find_bullet(struct Bullet *b) {
    s32 i;
    for (i = 0; i < G64_BULLETS; i++)
        if (bulletOwner[i].bullet == b)
            return &bulletOwner[i];
    return NULL;
}

/* A bullet's entry, made when it has none; NULL when the table is full. */
static G64BulletOwner *bullet_entry(struct Bullet *b) {
    G64BulletOwner *o = find_bullet(b);
    if (!o) {
        o = find_bullet(NULL);
        if (o) {
            o->bullet = b;
            o->player = g64Member[0];
            o->hitBy = -1;
        }
    }
    return o;
}

void G64BulletMade(struct Bullet *b) {
    G64BulletOwner *o;
    if (slotCount < 2)
        return;
    o = bullet_entry(b);
    if (o) {
        o->player = g64Member[logicSlot];
        o->hitBy = -1;
    }
}

void G64BulletGone(struct Bullet *b) {
    G64BulletOwner *o = find_bullet(b);
    if (o) {
        o->bullet = NULL;
        o->player = 0;
        o->hitBy = -1;
    }
    bulletGone = b;
}

s32 G64BulletBegin(struct Bullet *b) {
    G64BulletOwner *o;
    s32 preferred = -1;
    if (slotCount < 2)
        return 0;
    bulletGone = NULL;
    o = find_bullet(b);
    /* With no record, the hit came while the player in the globals was the only one. */
    if (b->actor.flags & BULLET_HIT_FLAGS)
        preferred = o ? recorded_slot(o->hitBy) : 0;
    if (preferred < 0 && (b->target == &hand || b->target == &ball)) {
        /* With no record, the bullet was aimed while the player in the globals was the only one. */
        preferred = o ? recorded_slot(o->player) : 0;
    }
    return target_begin(&b->actor, preferred);
}

void G64BulletEnd(struct Bullet *b, s32 i) {
    G64BulletOwner *o;
    if (slotCount < 2 && bulletGone != b && (o = find_bullet(b)) != NULL) {
        /* A bullet made when there were more players: only the player in the globals can have hit it. */
        o->hitBy = (b->actor.flags & BULLET_HIT_FLAGS) ? g64Member[0] : -1;
    } else if (slotCount >= 2 && bulletGone != b) {
        o = NULL;
        if (b->target == &hand || b->target == &ball || (b->actor.flags & BULLET_HIT_FLAGS))
            o = bullet_entry(b);
        if (o) {
            /* The bullet homes in on the glove or the ball in the globals: the target's player. */
            if (b->target == &hand || b->target == &ball)
                o->player = g64Member[i];
            /* Its update cleared the hit flags, then a collision in it may have set them again. */
            o->hitBy = (b->actor.flags & BULLET_HIT_FLAGS) ? g64Member[i] : -1;
        } else if ((o = find_bullet(b)) != NULL) {
            o->hitBy = -1;
        }
    }
    bulletGone = NULL;
    G64TargetEnd(i);
}

s32 G64BulletMine(struct Bullet *b) {
    G64BulletOwner *o;
    s32 i;
    if (slotCount < 2)
        return 1;
    o = find_bullet(b);
    if (!o)
        return 1;
    i = recorded_slot(o->player);
    return i < 0 || i == logicSlot;
}
s32 G64EnemyInfo(s32 i, f32 *x, f32 *y, f32 *z) {
    Enemy *e;
    if (i < 0 || i >= numberOfEnemies)
        return 0;
    e = &enemies[i];
    *x = e->actor.pos.x;
    *y = e->actor.pos.y;
    *z = e->actor.pos.z;
    return e->actor.active ? e->actor.type + 1 : 0;
}
s32 G64BulletPositions(f32 *xyz, s32 max) {
    Bullet *b;
    s32 n = 0;
    for (b = bulletList.head.next; b != &bulletList.head; b = b->next) {
        if (n < max) {
            xyz[n * 3] = b->actor.pos.x;
            xyz[n * 3 + 1] = b->actor.pos.y;
            xyz[n * 3 + 2] = b->actor.pos.z;
        }
        n++;
    }
    return n;
}
s32 G64BallHolder(s32 player) {
    s32 i = slot_of(player), k;
    Actor *e;
    if (i < 0 || !slot_used(i))
        return -1;
    e = slot_ball_control(i)->enemy;
    for (k = 0; k < numberOfEnemies; k++)
        if (e == &enemies[k].actor)
            return k;
    return -1;
}
/* Puzzles and platforms (phase 3). A puzzle's condition on the player is tested with each player swapped into the
   globals in turn (puzzle.c, PuzzleEvaluateCondition); the conditions only read. Platforms that wait for a ball or a
   glove, or that face one, look at every player. */

s32 G64PuzzleSlots(void) {
    if (slotCount < 2 || swapped || logicSlot)
        return 1;
    return slotCount;
}

s32 G64PuzzleSlotBegin(s32 i) {
    if (i == 0)
        return 1;
    if (!slot_used(i) || logicSlot)
        return 0;
    swap_slot(slots[i]);
    logicSlot = i;
    return 1;
}

void G64PuzzleSlotEnd(s32 i) {
    if (i > 0 && logicSlot == i) {
        swap_slot(slots[i]);
        logicSlot = 0;
    }
}

s32 G64AnyBallOn(struct Platform *p) {
    s32 i;
    for (i = 1; i < slotCount; i++)
        if (slot_used(i) && slot_ball(i)->obj && slot_ball(i)->obj->platform == p)
            return 1;
    return 0;
}

s32 G64AnyGloveOn(struct Platform *p) {
    s32 i;
    for (i = 1; i < slotCount; i++)
        if (slot_used(i) && slot_glove(i)->obj && slot_glove(i)->obj->platform == p)
            return 1;
    return 0;
}

/* The glove or ball of the player nearest to a point (a tie goes to the lower slot); the globals' with one player. */
static Actor *nearest(const Vec3 *p, s32 isBall) {
    s32 i, best = 0;
    f32 bestDist = 0.0f;
    for (i = 0; i < slotCount; i++) {
        Actor *a;
        f32 d;
        if (!slot_used(i))
            continue;
        a = isBall ? slot_ball(i) : slot_glove(i);
        d = DistanceBetweenPointsSquared(&a->pos, (Vec3 *)p);
        if (i == 0 || d < bestDist) {
            best = i;
            bestDist = d;
        }
    }
    /* A slot's actor is a copy: the platform reads only its position, which the slot holds. */
    return isBall ? slot_ball(best) : slot_glove(best);
}

Actor *G64NearestBall(const Vec3 *p) {
    return slotCount < 2 ? &ball : nearest(p, 1);
}

Actor *G64NearestGlove(const Vec3 *p) {
    return slotCount < 2 ? &hand : nearest(p, 0);
}
s32 G64PuzzleInfo(s32 k, s32 *timesLeft, s32 *active, s32 *count) {
    Puzzle *pz;
    s32 i = 0;
    for (pz = puzzleList.head.prev; pz != &puzzleList.head; pz = pz->prev, i++) {
        if (i == k) {
            *timesLeft = pz->conds.timesLeft;
            *active = pz->actions.active;
            *count = pz->conds.count;
            return 1;
        }
    }
    return 0;
}

s32 G64PuzzleCondition(s32 k, s32 j, s32 *type, s32 *tag, s32 *value) {
    Puzzle *pz;
    PuzzleCond *c;
    s32 i = 0;
    for (pz = puzzleList.head.prev; pz != &puzzleList.head; pz = pz->prev, i++) {
        if (i != k)
            continue;
        i = 0;
        for (c = pz->conds.head.next; c != &pz->conds.head; c = c->next, i++) {
            if (i == j) {
                *type = c->type;
                *tag = c->tag;
                *value = c->value;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

void G64PlaceBall(s32 player, f32 x, f32 y, f32 z) {
    s32 i = slot_of(player);
    Actor *b;
    if (i < 0 || !slot_used(i))
        return;
    b = slot_ball(i);
    b->pos.x = x;
    b->pos.y = y;
    b->pos.z = z;
    ZERO_VECTOR(&b->vel);
}

void G64PushBall(s32 player, f32 x, f32 y, f32 z) {
    s32 i = slot_of(player);
    if (i < 0 || !slot_used(i))
        return;
    slot_ball(i)->vel.x = x;
    slot_ball(i)->vel.y = y;
    slot_ball(i)->vel.z = z;
}

s32 G64PuzzleInSlot(void) {
    return logicSlot != 0;
}

void G64PuzzleCameraState(s32 i) {
    G64Slot *s;
    s32 size;
    if (i < 1 || !slot_used(i))
        return;
    s = slots[i];
    if (!s->puzzleState) {
        size = PuzzleCameraStateSize();
        if (size <= 0)
            return;
        s->puzzleState = (u8 *)JallocAlloc(size, 0, "G64Zones");
        /* A player's zones start as the level's are now. */
        PuzzleSwapCameraState((u8 *)s->puzzleState, 1);
    }
    PuzzleSwapCameraState((u8 *)s->puzzleState, 0);
}
s32 G64PuzzleZone(s32 k, f32 *box) {
    Puzzle *pz;
    PuzzleCond *c;
    PuzzleAction *a;
    s32 i = 0;
    for (pz = puzzleList.head.prev; pz != &puzzleList.head; pz = pz->prev, i++) {
        if (i != k)
            continue;
        c = pz->conds.head.next;
        if (pz->conds.count != 1 || c->type != CONDITION_HAND_IN_BOX || !c->cam || pz->actions.count == 0)
            return 0;
        for (a = pz->actions.head.next; a != &pz->actions.head; a = a->next)
            if (!IsCameraZoneAction(a->type))
                return 0;
        box[0] = c->cam->pos.x;
        box[1] = c->cam->pos.y;
        box[2] = c->cam->pos.z;
        box[3] = c->cam->size.x;
        box[4] = c->cam->size.y;
        box[5] = c->cam->size.z;
        return 1;
    }
    return 0;
}
/* Respawns (phase 3, the owner's rules). Online, or with more than one player, each player respawns at their own
   checkpoint: the gate their own ball last crossed first. A gate's countdown (restartTimer), the camera's place for
   the respawn (cameraPos) and RestartPlayer's frame stamps are each player's own, here, by player number. The game
   reaches them through G64RestartTimer, G64RestartCamera and G64RestartStamp, for the player in the globals. A
   respawn after a death restarts no enemies (the level's opening does), and a death takes no life. Offline with one
   player the gate's own fields are used, as before. */
#define G64_GATES 16
typedef struct G64Restart {
    s32 valid;
    PTR32(Platform) gate;
    s16 timer;
    /* 1 while the countdown is the level's opening respawn (parse.c), not a death's. */
    s16 opening;
    Vec3 cameraPos;
    u32 stamps[4];
    PTR32(Platform) touched[G64_GATES];
    /* The camera's place at each touch, for a respawn there (the pause menu can pick an earlier gate). */
    Vec3 touchedCamera[G64_GATES];
    s32 touchedCount;
} G64Restart;
static G64Restart restarts[G64_MAX_PLAYERS];

s32 G64PerPlayerRestart(void) {
    return g64Online || g64Players > 1;
}

/* The slot of the player in the globals: one swapped in for the logic or for a view, or 0. */
static s32 globals_slot(void) {
    return logicSlot ? logicSlot : swapped;
}

static G64Restart *restart_of(s32 slot) {
    return &restarts[g64Member[slot] & (G64_MAX_PLAYERS - 1)];
}

/* A player's record, made from their checkpoint the first time. The first record takes the gate's countdown (the
   level's opening respawn, parse.c), and the gate's own field is then 0: no later player inherits it. */
static G64Restart *restart_init(s32 slot) {
    G64Restart *r = restart_of(slot);
    Platform *gate = slot_hand_control(slot)->checkpoint;
    if (r->valid)
        return r;
    memset(r, 0, sizeof *r);
    r->valid = 1;
    r->gate = gate;
    if (gate && gate->restartGate) {
        r->timer = gate->restartGate->restartTimer;
        r->opening = r->timer > 0;
        gate->restartGate->restartTimer = 0;
        r->cameraPos = gate->restartGate->cameraPos;
        r->touchedCamera[r->touchedCount] = r->cameraPos;
        r->touched[r->touchedCount++] = gate;
    }
    return r;
}

/* The record of the player in the globals for this gate. A new checkpoint starts with no countdown. */
static G64Restart *restart_for(Platform *gate) {
    G64Restart *r = restart_init(globals_slot());
    s32 k;
    if (r->gate != gate) {
        r->gate = gate;
        r->timer = 0;
        if (gate && gate->restartGate)
            r->cameraPos = gate->restartGate->cameraPos;
        /* A gate this player touched keeps the camera's place from their touch. Another gate (one the pause menu
           picked) has the place from its first touch by anyone, as in the game. Crossing it later still makes it
           this player's checkpoint. */
        for (k = 0; k < r->touchedCount; k++)
            if (r->touched[k] == gate)
                r->cameraPos = r->touchedCamera[k];
    }
    return r;
}

s16 *G64RestartTimer(Platform *gate) {
    if (!G64PerPlayerRestart())
        return &gate->restartGate->restartTimer;
    return &restart_for(gate)->timer;
}

Vec3 *G64RestartCamera(Platform *gate) {
    if (!G64PerPlayerRestart())
        return &gate->restartGate->cameraPos;
    return &restart_for(gate)->cameraPos;
}

s32 G64AnyRestarting(void) {
    s32 i, n = slotCount > 0 ? slotCount : 1;
    if (!G64PerPlayerRestart())
        return handControl.checkpoint->restartGate->restartTimer != 0;
    for (i = 0; i < n; i++)
        if (slot_used(i) && slot_hand_control(i)->checkpoint && restart_init(i)->timer != 0)
            return 1;
    return 0;
}

s32 G64RestartIsDeath(void) {
    G64Restart *r;
    if (!G64PerPlayerRestart())
        return 0;
    r = restart_init(globals_slot());
    if (r->opening) {
        r->opening = 0;
        return 0;
    }
    return 1;
}

u32 *G64RestartStamp(s32 k, u32 *own) {
    if (!G64PerPlayerRestart() || k < 0 || k >= 4)
        return own;
    return &restart_init(globals_slot())->stamps[k];
}

void AddPuff(Vec3 *pos, Vec3 *vel, f32 size);

void G64CheckRestartGate(Platform *platform) {
    PlatformRestartGate *gate = platform->restartGate;
    s32 i, k, n = slotCount > 0 ? slotCount : 1;
    /* On Hard only the level's start is a checkpoint (parse.c leaves the other gates touched). */
    if (saveSlot[currentSaveSlot].difficultyLevel == DIFFICULTY_HARD)
        return;
    for (i = 0; i < n; i++) {
        Actor *b;
        G64Restart *r;
        Vec3 v;
        s32 seen = 0;
        if (!slot_used(i))
            continue;
        b = slot_ball(i);
        if (!(DistanceBetweenPoints2D(&b->pos, &platform->actor.pos) < platform->actor.radius &&
              b->pos.y <= platform->actor.pos.y && platform->actor.pos.y < b->prevPos.y))
            continue;
        r = restart_init(i);
        /* Crossing the player's own checkpoint changes nothing, and is no touch: a respawn drops and launches the
           ball through it. */
        if (slot_hand_control(i)->checkpoint == platform)
            continue;
        for (k = 0; k < r->touchedCount; k++)
            seen |= r->touched[k] == platform;
        if (seen)
            continue;
        if (r->touchedCount < G64_GATES) {
            r->touchedCamera[r->touchedCount] = i == 0 ? camera.pos : ((G64Slot *)slots[i])->camera.pos;
            r->touched[r->touchedCount++] = platform;
        }
        /* The puffs and the sound of the game's own touch (CheckRestartGate), once per player. */
        v.x = 8.0f;
        v.y = 0.0f;
        v.z = 0.0f;
        /* A gate of another level's file may have no motion. */
        RotateVector2D(&v, &v, platform->motion ? platform->motion->rot.y : 0.0f);
        for (k = 0; k < 6; k++) {
            RotateVector2D(&v, &v, 1.0f);
            AddPuff(&platform->actor.pos, &v, 2.0f);
        }
        if (gate->untouched) {
            /* The gate's own place for the camera, from its first touch (CheckRestartGate). */
            gate->cameraPos = i == 0 ? camera.pos : ((G64Slot *)slots[i])->camera.pos;
        }
        gate->untouched = 0;
        gate->targetScale = 0.0f;
        slot_hand_control(i)->checkpoint = platform;
        /* A level pack's script hears of the new checkpoint (src/port/g64lua.c, kind 4). */
        G64LuaEvent(4, g64Member[i]);
        if (r->gate != platform)
            r->timer = 0;
        r->gate = platform;
        r->cameraPos = i == 0 ? camera.pos : ((G64Slot *)slots[i])->camera.pos;
        soundtable_set_owner(g64Member[i]);
        PlaySample(280, &b->pos, 100, 128);
        soundtable_set_owner(-1);
    }
}

static void forget_restart(s32 player) {
    memset(&restarts[player & (G64_MAX_PLAYERS - 1)], 0, sizeof(G64Restart));
}

/* The start copy's record (take_start). */
static G64Restart startRestart;

/* Player toPlayer's record becomes fromPlayer's, by number. */
static void move_restart(s32 toPlayer, s32 fromPlayer) {
    restarts[toPlayer & (G64_MAX_PLAYERS - 1)] = restarts[fromPlayer & (G64_MAX_PLAYERS - 1)];
}

static void forget_restarts(void) {
    memset(restarts, 0, sizeof restarts);
    memset(&startRestart, 0, sizeof startRestart);
}

/* Slot to's player gets the record of slot from's player; -1 is the start copy on either side. */
static void copy_restart(s32 to, s32 from) {
    G64Restart *src, *dst;
    if (!G64PerPlayerRestart())
        return;
    src = from < 0 ? &startRestart : restart_init(from);
    dst = to < 0 ? &startRestart : restart_of(to);
    *dst = *src;
}
s32 G64RestartCountdown(s32 player) {
    s32 i = slot_of(player);
    if (i < 0 || !slot_used(i))
        return -1;
    if (!G64PerPlayerRestart()) {
        Platform *gate = slot_hand_control(i)->checkpoint;
        return gate && gate->restartGate ? gate->restartGate->restartTimer : 0;
    }
    return restart_init(i)->timer;
}
/* The k-th platform with a restart gate, in the platform list's order, or NULL. */
static Platform *gate_at(s32 k) {
    Platform *p;
    s32 i = 0;
    for (p = platformList.head.next; p != &platformList.head; p = p->next)
        if (p->restartGate && i++ == k)
            return p;
    return NULL;
}

s32 G64GatePosition(s32 k, f32 *x, f32 *y, f32 *z) {
    Platform *p = gate_at(k);
    if (!p)
        return 0;
    *x = p->actor.pos.x;
    *y = p->actor.pos.y;
    *z = p->actor.pos.z;
    return 1;
}

s32 G64PlayerCheckpoint(s32 player) {
    s32 i = slot_of(player), k;
    Platform *cp;
    if (i < 0 || !slot_used(i))
        return -2;
    cp = slot_hand_control(i)->checkpoint;
    for (k = 0; gate_at(k); k++)
        if (gate_at(k) == cp)
            return k;
    return -1;
}

s32 G64PlayerLives(s32 who) {
    s32 i = slot_of(who);
    Player *pl;
    if (i < 0 || !slot_used(i))
        return -100;
    pl = i == 0 ? &player : (Player *)(((G64Slot *)slots[i])->handBlock + ((u8 *)&player - (u8 *)&handControl));
    return pl->lives;
}
void ShadowAltitude(Actor *actor);

void G64SlotShadows(void) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        HandControl *hc;
        Platform *platform;
        Debris *fb;
        if (!slot_used(i))
            continue;
        if ((slot_glove(i)->flags & 0x100) && slot_glove(i)->shadow)
            ShadowAltitude(slot_glove(i));
        if ((slot_ball(i)->flags & 0x100) && slot_ball(i)->shadow)
            ShadowAltitude(slot_ball(i));
        hc = slot_hand_control(i);
        fb = hc->fakeball;
        if (fb != NULL && fb->shadow)
            fb->shadow->altitude = Altitude(&fb->pos, PORT_OUT(Vec3, &fb->shadow->floor), &platform, 0);
    }
}
f32 G64PlayerAltitude(s32 who) {
    s32 i = slot_of(who);
    if (i < 0 || !slot_used(i) || !slot_glove(i)->shadow)
        return -2.0f;
    return slot_glove(i)->shadow->altitude;
}
/* World hazards (phase 3). Winds, fans, water, spikes and buzzers act on the actors in the actor list, which hold the
   first player's glove and ball and the proxies. The other players' actors are in their slots. */

s32 G64SlotCount(void) {
    return slotCount >= 2 && !swapped && !logicSlot ? slotCount : 0;
}

Actor *G64SlotGlove(s32 i) {
    return i >= 1 && slot_used(i) ? slot_glove(i) : NULL;
}

Actor *G64SlotBall(s32 i) {
    return i >= 1 && slot_used(i) ? slot_ball(i) : NULL;
}
s32 G64HazardBegin(s32 i) {
    if (i < 1 || !slot_used(i) || swapped || logicSlot)
        return 0;
    swap_slot(slots[i]);
    logicSlot = i;
    /* The hurt's sounds are this player's (the sound table, soundtable.h). */
    soundtable_set_owner(g64Member[i]);
    return 1;
}

void G64HazardEnd(s32 i) {
    if (i >= 1 && logicSlot == i) {
        soundtable_set_owner(-1);
        swap_slot(slots[i]);
        logicSlot = 0;
    }
}

void DisableControl(s16 time);

/* Cameos (cameo.c). A cameo is a scene that moves the camera and holds the glove. It plays for one player: the one
   who set it off, when a puzzle's condition on a player started it, and the first player otherwise (a level's
   opening scene). That player is in the globals while the cameo starts and steps (G64CameoSwapIn). The other
   players are held (G64CameoAfter), nothing hurts them (G64CameoHeld), and G64CameoView gives them the cameo's
   camera: where it stands, how it is turned, what it looks at and its roll. Their own cameras go on from there
   when it ends. */
static void forget_cameo(void) {
    cameoStarter = cameoMet = cameoUsed = cameoPending = cameoHolding = 0;
}

/* The slot of the cameo's player. */
static s32 cameo_slot(void) {
    s32 i = cameoStarter ? slot_of(cameoStarter - 1) : 0;
    return i > 0 && slot_used(i) ? i : 0;
}

/* puzzle.c: a puzzle's conditions are about to be tested; slot i's player met one; the puzzle, which starts a cameo,
   has its conditions met. */
void G64PuzzleMetReset(void) {
    cameoMet = 0;
}

void G64PuzzleMet(s32 i) {
    if (i >= 0 && i < G64_MAX_PLAYERS)
        cameoMet = g64Member[i] + 1;
}

void G64CameoStarter(void) {
    cameoPending = cameoMet;
}

/* cameo.c: the cameo held its player in this frame. */
void G64CameoHolding(void) {
    cameoHolding = 1;
}

/* Puts the cameo's player in the globals and returns their slot, or 0 when they are there already. */
s32 G64CameoSwapIn(void) {
    s32 i = cameo_slot();
    if (i == 0)
        return 0;
    if (swapped || logicSlot) {
        /* Another player is in the globals for their own part of the frame: the cameo is the first player's. */
        cameoStarter = 0;
        return 0;
    }
    swap_slot(slots[i]);
    logicSlot = i;
    return i;
}

void G64CameoSwapOut(s32 i) {
    if (i > 0 && logicSlot == i) {
        swap_slot(slots[i]);
        logicSlot = 0;
    }
}

/* A puzzle starts a cameo: for its player. */
s32 G64CameoStartBegin(void) {
    /* A cameo that starts while another plays joins it: the one that plays keeps its player. */
    if (!gameInfo.cameoActive && !cameoUsed) {
        cameoStarter = cameoPending;
        cameoUsed = 1;
    }
    cameoPending = 0;
    return G64CameoSwapIn();
}

void G64CameoView(void) {
    s32 i;
    if (swapped)
        return;
    /* With the cameo's player swapped in, their slot holds the first player. */
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s)
            continue;
        s->camera.pos = camera.pos;
        s->camera.rot = camera.rot;
        s->camera.rotQuat = camera.rotQuat;
        ZERO_VECTOR(&s->camera.vel);
        s->cameraInfo.actor.pos = cameraInfo.actor.pos;
        s->cameraUpVect = cameraUpVect;
    }
}

/* For tests: 1 when the glove of a player who is not in the globals is drawn (its proxy has a size), 0 when it is
   hidden, -1 when the player has no proxy. */
s32 G64ProxyShown(s32 player) {
    s32 i = slot_of(player);
    Actor *a;
    if (i < 1 || i >= slotCount || !proxyHand[i])
        return -1;
    a = proxyHand[i];
    return a->scale.x != 0.0f || a->scale.y != 0.0f || a->scale.z != 0.0f;
}

/* For tests: the number of the player a cameo plays for, or -1 when none plays. */
s32 G64CameoPlayer(void) {
    if (!gameInfo.cameoActive)
        return -1;
    return g64Member[cameo_slot()];
}

/* After the frame's cameo step, with the first player back in the globals: every player but the cameo's is held. A
   cameo that has ended forgets its player. */
void G64CameoAfter(void) {
    s32 i, own = cameo_slot();
    if (swapped || logicSlot)
        return;
    if (!gameInfo.cameoActive) {
        if (cameoUsed)
            forget_cameo();
        return;
    }
    /* In its last frame a cameo no longer holds its player, and the others go free with them. */
    if (!cameoHolding)
        return;
    cameoHolding = 0;
    if (own != 0)
        DisableControl(2);
    for (i = 1; i < slotCount; i++) {
        if (i != own && G64HazardBegin(i)) {
            DisableControl(2);
            G64HazardEnd(i);
        }
    }
}

/* Tips (tip.c). Offline a tip stops the game while the player reads it. Online the level goes on, and each player
   reads a tip on their own: the reader's glove gets no input and nothing hurts them, and their A turns the page or
   closes the tip. What a player reads is part of the game's state, kept by player number: the client only draws
   it (G64TipPage). */
s32 TipFirstLine(s32 tag);
s32 TipPageEnd(s32 line);
s32 TipLineType(s32 line);
const char *TipLineText(s32 line);

/* The N64 game takes A from the 13th frame of an open tip, when its box has grown. */
#define TIP_OPENING 13
/* tipLine when the tip is closed and the player still holds the A that closed it. */
#define TIP_CLOSING (-1)
/* tipLine: the number plus 1 of the first line of the page that the player reads, or 0. tipTime: frames since the
   tip opened. tipButtons: the player's buttons of the frame before. */
static s16 tipLine[G64_MAX_PLAYERS];
static s16 tipTime[G64_MAX_PLAYERS];
static u16 tipButtons[G64_MAX_PLAYERS];

s32 G64TipsOwn(void) {
    return g64Online;
}

static void forget_tip(s32 player) {
    tipLine[player & (G64_MAX_PLAYERS - 1)] = 0;
}

static void forget_tips(void) {
    memset(tipLine, 0, sizeof tipLine);
    memset(tipTime, 0, sizeof tipTime);
    memset(tipButtons, 0, sizeof tipButtons);
}

u32 G64TipEveryone(void) {
    return lastPresent;
}

u32 G64TipReaders(const Vec3 *pos) {
    u32 readers = 0;
    s32 i;
    if (swapped || logicSlot)
        return 0;
    if (DistanceBetweenPoints(&hand.pos, (Vec3 *)pos) < 20.0f && pad[ActiveController].pressed[BUTTON_B])
        readers |= 1u << g64Member[0];
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (s && DistanceBetweenPoints(&s->hand.pos, (Vec3 *)pos) < 20.0f && s->pad.pressed[BUTTON_B])
            readers |= 1u << g64Member[i];
    }
    return readers;
}

s32 G64TipOpen(u32 players, s32 tag, const Vec3 *pos) {
    s32 p, opened = 0;
    for (p = 0; p < G64_MAX_PLAYERS; p++) {
        if (!(players & lastPresent & (1u << p)) || tipLine[p])
            continue;
        tipLine[p] = (s16)(TipFirstLine(tag) + 1);
        tipTime[p] = 0;
        opened = 1;
    }
    /* One sound at the tip, however many players it opened for. */
    if (opened)
        PlaySample(59, (Vec3 *)pos, 100, 190);
    return opened;
}

void G64TipsStep(void) {
    s32 p;
    if (!g64Online || swapped || logicSlot)
        return;
    for (p = 0; p < G64_MAX_PLAYERS; p++) {
        u16 pressed = (u16)(g64Buttons[p] & ~tipButtons[p]);
        s32 i = slot_of(p);
        tipButtons[p] = g64Buttons[p];
        if (!(lastPresent & (1u << p))) {
            tipLine[p] = 0;
            continue;
        }
        if (!tipLine[p])
            continue;
        /* The tip is closed. The reader stays held until they let go of A: the press that closed it must not
           also make the glove jump. */
        if (tipLine[p] == TIP_CLOSING && !(g64Buttons[p] & 0x8000)) {
            tipLine[p] = 0;
            continue;
        }
        /* The reader's glove gets no input. A player whose glove is not made yet has none to hold. */
        if (i == 0) {
            DisableControl(2);
        } else if (G64HazardBegin(i)) {
            DisableControl(2);
            G64HazardEnd(i);
        }
        if (tipLine[p] == TIP_CLOSING) {
            continue;
        }
        if (tipTime[p] < TIP_OPENING) {
            tipTime[p]++;
        } else if (pressed & 0x8000) {
            s32 end = TipPageEnd(tipLine[p] - 1);
            Vec3 *at = i > 0 && slot_used(i) ? &((G64Slot *)slots[i])->hand.pos : &hand.pos;
            PlaySample(180, at, 120, 120);
            tipLine[p] = TipLineType(end) == 2 ? TIP_CLOSING : (s16)(end + 2);
        }
    }
}

s32 G64TipHeld(void) {
    return g64Online && !swapped && tipLine[g64Member[logicSlot] & (G64_MAX_PLAYERS - 1)] != 0;
}

void TipDrawPage(s32 line, s32 time);

/* The client calls this before it draws a player's view: the tip that the player reads goes into the picture, in
   the game's own box. */
void G64TipView(s32 player) {
    if (player >= 0 && player < G64_MAX_PLAYERS && tipLine[player] > 0)
        TipDrawPage(tipLine[player] - 1, tipTime[player]);
}

/* The online program's own menu and text, in the game's style: the game's pause panes, fonts and colors. The
   client calls these before it draws a player's view, as it calls G64TipView, and loads the state again after the
   view: nothing here stays in the game. */
extern Overlay tipOverlay2;
extern PTR32(char) pauseString[];
extern s32 frameCount2;
struct Text *NEW_TEXT(s16 x, s16 y, u8 r, u8 g, u8 b, u8 a, f32 sx, f32 sy, u8 oneShot, char *str, u8 centre);
void SetFont(u8 type);
void AddOverlay(Overlay *overlay);
s16 GetStringPixelLength(char *str, f32 sx, f32 sy);

/* A line of the client's text for the game's text list, which holds 63 characters and takes its text as a format:
   the plain printable characters, without the percent sign. */
static char *view_string(char *out, const char *text) {
    s32 n = 0;
    for (; *text != 0 && n < 62; text++)
        if (*text >= 32 && *text < 127 && *text != '%')
            out[n++] = *text;
    out[n] = 0;
    return out;
}

/* The pause menu: the game's green pane, its "PAUSED" in the game's language, and the choices in the game's menu
   font. The chosen one is full size and blinks, as in the game's own pause menu; the others are smaller. */
void G64MenuView(const char *const *items, s32 count, s32 selected) {
    char line[64];
    s32 i, y;
    if (count < 1 || count > 6)
        return;
    tipOverlay2.x = 160.0f;
    tipOverlay2.y = 110.0f;
    tipOverlay2.scaleX = 5600;
    tipOverlay2.scaleY = (count * 128 + 512) * 4;
    tipOverlay2.alpha = 200;
    tipOverlay2.r = 0;
    tipOverlay2.g = 200;
    tipOverlay2.b = 0;
    AddOverlay(&tipOverlay2);
    /* The pane is count * 16 + 64 high around 110; the game's own menu has its title 24 below the pane's top. */
    y = 110 - (count * 16 + 64) / 2 + 24;
    SetFont(0);
    NEW_TEXT(160, y, 255, 255, 255, 252, 1.0f, 1.0f, 1, pauseString[gameInfo.language], 1);
    SetFont(1);
    y += 30;
    for (i = 0; i < count; i++, y += 20) {
        f32 size = i == selected ? 1.0f : 0.8f;
        u8 alpha = i == selected && !(frameCount2 & 4) ? 126 : 252;
        NEW_TEXT(160, y, 255, 255, 255, alpha, size, size, 1, view_string(line, items[i]), 1);
    }
    SetFont(0);
}

/* A line of text in the upper middle of the picture, on a pane of the game's as wide as the text: what a level
   pack's script shows to the player. */
void G64LineView(const char *text) {
    char line[64];
    s32 width;
    view_string(line, text);
    if (line[0] == 0)
        return;
    SetFont(1);
    /* A pane's scale is 32 for each point of the picture. The text's middle is 4 above the place it is given. */
    width = GetStringPixelLength(line, 0.6f, 0.6f);
    tipOverlay2.x = 160.0f;
    tipOverlay2.y = 50.0f;
    tipOverlay2.scaleX = (width + 28) * 32;
    tipOverlay2.scaleY = 16 * 32;
    tipOverlay2.alpha = 200;
    tipOverlay2.r = 255;
    tipOverlay2.g = 200;
    tipOverlay2.b = 255;
    AddOverlay(&tipOverlay2);
    NEW_TEXT(160, 54, 255, 255, 255, 252, 0.6f, 0.6f, 1, line, 1);
    SetFont(0);
}

/* The newest chat lines at the lower left of the picture, in the game's menu font. A line that starts with a star
   is the program's own note, in the color of the game's tips. */
void G64ChatView(const char *const *lines, s32 count) {
    char line[64];
    s32 i;
    if (count > 6)
        count = 6;
    SetFont(1);
    for (i = 0; i < count; i++) {
        s32 note = lines[i][0] == '*';
        NEW_TEXT(14, 178 - (count - 1 - i) * 11, note ? 170 : 255, note ? 170 : 255, 255, 252, 0.55f, 0.55f, 1,
                 view_string(line, lines[i]), 0);
    }
    SetFont(0);
}

void CarnivalView(s32 slot);

/* The client calls this before it draws a player's view: in the Carnival bonus level the cannon points where that
   player aims (bonus.c). */
void G64CannonView(s32 player) {
    CarnivalView(slot_of(player));
}

s32 G64TipPage(s32 player, const char **lines, s32 max, s32 *more) {
    s32 line, end, n = 0;
    if (player < 0 || player >= G64_MAX_PLAYERS || tipLine[player] <= 0)
        return 0;
    line = tipLine[player] - 1;
    end = TipPageEnd(line);
    for (; line <= end; line++, n++)
        if (n < max)
            lines[n] = TipLineText(line);
    if (more)
        *more = TipLineType(end) != 2;
    return n < max ? n : max;
}

/* 1 while a cameo plays for a player other than the one in the globals: they are held, and nothing hurts them. */
s32 G64CameoHeld(void) {
    s32 own = cameo_slot();
    return !swapped && gameInfo.cameoActive && logicSlot != own;
}

/* debris.c frees a piece of debris: no player keeps it as their fake ball (the ball a glove throws or whacks
   away). The slots are read where they are: with a player swapped in, their slot holds the first player. */
void G64FakeballGone(const void *debris) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (s && (const void *)(Debris *)slot_control(s)->fakeball == debris)
            slot_control(s)->fakeball = NULL;
    }
}

void G64FootprintGone(const void *footprint) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        if (slot_used(i) && (const void *)(Footprint *)slot_hand_control(i)->footprint == footprint)
            slot_hand_control(i)->footprint = NULL;
    }
}

/* The running quake's place, the two ranges its strength comes from (StartEarthquakeAtPoint), and the number, plus
   1, of the player whose distance set its strength. quakePlaced is 0 for a quake without a place. */
static Vec3 quakePos;
static f32 quakeMax, quakeMin;
static s32 quakePlaced, quakeBy;

void G64QuakeAt(const Vec3 *pos, f32 maxStrength, f32 minStrength) {
    quakePlaced = pos != NULL;
    if (pos != NULL)
        quakePos = *pos;
    quakeMax = maxStrength;
    quakeMin = minStrength;
    quakeBy = g64Member[logicSlot] + 1;
}

/* The strengths are the game's own (StartEarthquakeAtPoint), from this player's distance. */
s32 G64QuakeStrength(f32 blend, f32 *str) {
    f32 dist, start, end;
    if (slotCount < 2 || !quakePlaced || quakeBy == g64Member[logicSlot] + 1)
        return 1;
    dist = DistanceBetweenPoints((Vec3 *)cameraInfo.focus, &quakePos);
    if (dist >= quakeMax && gameInfo.level != SPACEBOSS1)
        return 0;
    start = (quakeMax - dist) * 0.2;
    start = MIN(start, 1000.0f);
    end = (quakeMin - dist) * 0.2;
    end = MIN(end, 1000.0f);
    if (gameInfo.level == SPACEBOSS1) {
        start *= 5.0f;
        end *= 5.0f;
        start = MIN(start < 50.0f ? 50.0f : start, 800.0f);
        end = MIN(end < 25.0f ? 25.0f : end, 400.0f);
    }
    /* StartEarthquake keeps the strengths as whole numbers. */
    *str = blend * (s16)start + (1.0f - blend) * (s16)end;
    return 1;
}

s32 G64IsPlayerFootprint(const void *footprint) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        if (slot_used(i) && (const void *)(Footprint *)slot_hand_control(i)->footprint == footprint)
            return 1;
    }
    return 0;
}

void G64DisableControlAll(s32 time) {
    s32 i;
    for (i = 1; i < G64SlotCount(); i++) {
        if (G64HazardBegin(i)) {
            DisableControl(time);
            G64HazardEnd(i);
        }
    }
}

s32 G64SlotBallBursting(s32 i) {
    return i >= 1 && slot_used(i) && slot_ball_control(i)->state == BALL_BURST;
}
Actor *G64EventKey(Actor *a) {
    if (logicSlot && a == &hand)
        return slot_glove(logicSlot);
    if (logicSlot && a == &ball)
        return slot_ball(logicSlot);
    return a;
}

void G64SlotWaterReset(void) {
    s32 i;
    for (i = 1; i < G64SlotCount(); i++) {
        Debris *fb;
        if (!slot_used(i))
            continue;
        if (slot_glove(i)->state)
            slot_glove(i)->state->waterState = 0;
        if (slot_ball(i)->state)
            slot_ball(i)->state->waterState = 0;
        fb = slot_hand_control(i)->fakeball;
        if (fb != NULL && fb->state)
            fb->state->waterState = 0;
    }
}
s32 G64HazardPlace(s32 kind, s32 k, f32 *x, f32 *y, f32 *z) {
    s32 i = 0;
    if (kind == 0) {
        Wind *w;
        for (w = windList.head.next; w != &windList.head; w = w->next) {
            if (w->type == 1 && i++ == k) {
                *x = w->pos[0] + w->size[0] / 2;
                *y = w->pos[1] + w->size[1] / 2;
                *z = w->pos[2] + w->size[2] / 2;
                return 1;
            }
        }
    } else if (kind == 1) {
        Water *w;
        for (w = waterList.head.next; w != &waterList.head; w = w->next) {
            if (i++ == k) {
                *x = w->box.pos.x + w->box.size.x / 2;
                *y = w->height;
                *z = w->box.pos.z + w->box.size.z / 2;
                return 1;
            }
        }
    } else if (kind == 2) {
        Spike *sp;
        for (sp = spikeList.head.next; sp != &spikeList.head; sp = sp->next) {
            if (i++ == k) {
                *x = sp->platform->actor.pos.x;
                *y = sp->platform->actor.pos.y;
                *z = sp->platform->actor.pos.z;
                return 1;
            }
        }
    } else if (kind == 3) {
        Buzzer *b;
        for (b = buzzerList.head.next; b != &buzzerList.head; b = b->next) {
            if (i++ == k) {
                *x = b->mid.x;
                *y = b->mid.y;
                *z = b->mid.z;
                return 1 + (b->buzzing != 0);
            }
        }
    }
    return 0;
}

s32 G64PlayerHealth(s32 who) {
    s32 i = slot_of(who);
    Player *pl;
    if (i < 0 || !slot_used(i))
        return -100;
    pl = i == 0 ? &player : (Player *)(((G64Slot *)slots[i])->handBlock + ((u8 *)&player - (u8 *)&handControl));
    return pl->health;
}

s32 G64PlayerVelocity(s32 who, f32 *x, f32 *y, f32 *z) {
    s32 i = slot_of(who);
    if (i < 0 || !slot_used(i))
        return 0;
    *x = slot_glove(i)->vel.x;
    *y = slot_glove(i)->vel.y;
    *z = slot_glove(i)->vel.z;
    return 1;
}

s32 G64PlayerInWater(s32 who) {
    s32 i = slot_of(who);
    if (i < 0 || !slot_used(i) || !slot_glove(i)->state)
        return -1;
    return slot_glove(i)->state->waterState;
}

/* Garibs and pick-ups (phase 3). The game lets the first player's glove and ball take them. Here every player can:
   collect.c asks who takes one, and that player is swapped in for the game's own code (G64HazardBegin). */

static Player *slot_player(s32 i) {
    return i == 0 ? &player : (Player *)(((G64Slot *)slots[i])->handBlock + ((u8 *)&player - (u8 *)&handControl));
}

s32 G64GaribGroupNear(const Vec3 *pos, f32 radius) {
    s32 i;
    for (i = 1; i < G64SlotCount(); i++) {
        if (slot_used(i) && (!(radius < DistanceBetweenPoints((Vec3 *)pos, &slot_glove(i)->pos)) ||
                             !(radius < DistanceBetweenPoints((Vec3 *)pos, &slot_ball(i)->pos))))
            return 1;
    }
    return 0;
}

/* The squared distance from which a player takes the garib at pos, by the game's own rule (UpdateGaribs): the ball
   when it is not lost, or the glove when it is nearer and not joined to the ball; within the taker's radius plus 15.
   -1 when the player does not reach it. */
static f32 garib_reach(Actor *h, Actor *b, BallControl *bc, const Vec3 *pos) {
    f32 dist = 100000.0f, radius = 0.0f, d;
    if (bc->loseTimer == 0) {
        dist = DistanceBetweenPointsSquared(&b->pos, (Vec3 *)pos);
        radius = b->radius * 1.5;
    }
    if (h->state && h->state->mode != HAND_JOINED) {
        d = DistanceBetweenPointsSquared(&h->pos, (Vec3 *)pos);
        if (d < dist) {
            dist = d;
            radius = h->radius;
        }
    }
    return dist < (radius + 15.0) * (radius + 15.0) ? dist : -1.0f;
}

s32 G64GaribTaker(const Vec3 *pos) {
    f32 best = garib_reach(&hand, &ball, &ballControl, pos), d;
    s32 i, taker = 0;
    for (i = 1; i < G64SlotCount(); i++) {
        if (!slot_used(i))
            continue;
        d = garib_reach(slot_glove(i), slot_ball(i), slot_ball_control(i), pos);
        if (d >= 0.0f && (best < 0.0f || d < best)) {
            best = d;
            taker = i;
        }
    }
    return taker;
}

/* slots[logicSlot] holds the first player's block while a slot is swapped in, so the loop reaches every player but
   the one in the globals. */
void G64GaribGained(void) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        if (slot_used(i))
            slot_player(i)->garibs++;
    }
}

s32 G64PickupTaker(const Vec3 *pos, f32 radiusSq) {
    s32 i;
    for (i = 1; i < G64SlotCount(); i++) {
        if (slot_used(i) && DistanceBetweenPointsSquared(&slot_glove(i)->pos, (Vec3 *)pos) < radiusSq)
            return i;
    }
    return -1;
}

void G64PickupTaken(const void *collectable) {
    s32 n, spot = -1;
    if (slotCount < 2)
        return;
    for (n = 0; n < G64_TAKEN; n++) {
        if ((const void *)(Collectable *)takenPickup[n] == collectable) {
            spot = n;
            break;
        }
        if (spot < 0 && !takenBy[n])
            spot = n;
    }
    if (spot < 0)
        return;
    takenPickup[spot] = (Collectable *)collectable;
    takenBy[spot] = g64Member[logicSlot] + 1;
}

s32 G64PickupScorer(const void *collectable) {
    s32 n, i;
    for (n = 0; n < G64_TAKEN; n++) {
        if (takenBy[n] && (const void *)(Collectable *)takenPickup[n] == collectable) {
            i = slot_of(takenBy[n] - 1);
            takenBy[n] = 0;
            takenPickup[n] = NULL;
            return i > 0 && !swapped && !logicSlot ? i : 0;
        }
    }
    return 0;
}

s32 G64WorldOrbiterDone(void) {
    return logicSlot && world_orbiter3();
}

s32 G64PlayerGaribs(s32 who) {
    s32 i = slot_of(who);
    return i < 0 || !slot_used(i) ? -1 : slot_player(i)->garibs;
}

s32 G64PlayerScore(s32 who) {
    s32 i = slot_of(who);
    return i < 0 || !slot_used(i) ? -1 : slot_player(i)->score;
}

s32 G64PlayerPowerup(s32 who, s32 type) {
    s32 i = slot_of(who);
    return i < 0 || !slot_used(i) || type < 0 || type >= 15 ? -1 : slot_player(i)->powerups[type];
}

s32 G64GaribPlace(s32 k, f32 *x, f32 *y, f32 *z) {
    GaribGroup *g;
    Garib *garib;
    s32 n = 0;
    for (g = garibGroupList.head.prev; g != &garibGroupList.head; g = g->prev) {
        for (garib = g->garibs.head.prev; garib != &g->garibs.head; garib = garib->prev) {
            if (abs(garib->active) != 1 || garib->type != NORMAL_GARIB)
                continue;
            if (n++ == k) {
                *x = garib->sprite.pos.x;
                *y = garib->sprite.pos.y;
                *z = garib->sprite.pos.z;
                return 1;
            }
        }
    }
    return 0;
}

/* For tests: where the level's tip number k is. It returns the tip's tag plus 1, or 0 when there is none. */
s32 G64TipPlace(s32 k, f32 *x, f32 *y, f32 *z) {
    Tip *tip;
    s32 n = 0;
    for (tip = tipList.head.next; tip != &tipList.head; tip = tip->next) {
        if (n++ == k) {
            *x = tip->pos.x;
            *y = tip->pos.y;
            *z = tip->pos.z;
            return tip->tag + 1;
        }
    }
    return 0;
}

s32 G64PickupPlace(s32 k, f32 *x, f32 *y, f32 *z) {
    Collectable *c;
    s32 n = 0;
    for (c = collectableList.head.next; c != &collectableList.head; c = c->next) {
        if (n++ == k) {
            *x = c->actor.pos.x;
            *y = c->actor.pos.y;
            *z = c->actor.pos.z;
            return c->active == 1 && c->scaleTarget != 0.0f && c->alphaTarget > 0 ? c->type + 100 : 1;
        }
    }
    return 0;
}

/* Any ball (the host's choice for the session, g64AnyBall). A glove may take any free ball, not only its own.
   Each player still has one ball in the game's sense: the one in their slot, which their glove touches, holds and
   throws. When a free glove touches another player's free ball, the two players' balls change places in the slots:
   the glove's player now has the ball it touched, and the other player has the ball the first one left behind.
   Every ball keeps its real owner, the player it started with: ballOwner[p] is the real owner of the ball in
   player p's slot. A ball that is lost kills its real owner, whoever has it: the player who has it gets it back
   whole at their checkpoint and lives, and the owner dies at the frame's end (ballKill).
   Only plain balls change places. A power-up's ball (bowling, crystal, a snowball and the others) runs on its
   player's own timers, which do not go with the ball. The rule is off in the bonus levels, where a lost ball ends
   the level for its player, and in the last space boss's levels, where the gloves do not move. */
s32 g64AnyBall;

s32 IsBonusLevel(void);
extern f32 gravity;
void ResetBall(void);
void ReleaseBall(void);
void ApplyActionIdle(void);
void LoseLife(void);

static s32 any_ball_on(void) {
    return g64AnyBall && !IsBonusLevel() && gameInfo.level != SPACEBOSS3 && gameInfo.level != SPACEBOSS_TWEEN;
}

static void forget_ball_owners(void) {
    s32 p;
    for (p = 0; p < G64_MAX_PLAYERS; p++)
        ballOwner[p] = (s8)p;
    ballKill = 0;
}

/* One player's ball and what belongs to it, in the globals (slot 0) or in a slot. */
typedef struct BallRef {
    Actor *ball;
    BallControl *control;
    s32 *isStatic;
    s32 *leftBehind;
    Vec3 *frameVel;
    u8 *pain;
    PTR32(ObjectInstance) *crystal;
    u32 *tex[6];
    Actor *glove;
    /* The player is dying or respawning (dying), or held for a cameo or a tip (held). */
    s32 dying;
    s32 held;
} BallRef;

static s32 ball_ref(s32 i, BallRef *r) {
    s32 k;
    if (i == 0) {
        r->ball = &ball;
        r->control = &ballControl;
        r->isStatic = &ballStatic;
        r->leftBehind = &ballLeftBehind;
        r->frameVel = &ballFrameVel;
        r->pain = &ballPain;
        r->crystal = &crystalBall;
        for (k = 0; k < 6; k++)
            r->tex[k] = tex_word(k);
        r->glove = &hand;
        r->dying = handControl.deathTimer || restarts[g64Member[0]].timer;
        r->held = handControl.noControlTimer != 0;
        return 1;
    }
    if (i < 1 || i >= slotCount || !slots[i])
        return 0;
    {
        G64Slot *s = slots[i];
        r->ball = &s->ball;
        r->control = &s->ballControl;
        r->isStatic = &s->ballStatic;
        r->leftBehind = &s->ballLeftBehind;
        r->frameVel = &s->ballFrameVel;
        r->pain = &s->ballPain;
        r->crystal = &s->crystalBall;
        for (k = 0; k < 6; k++)
            r->tex[k] = tex_word(k) ? &s->texWords[k] : NULL;
        r->glove = &s->hand;
        r->dying = slot_control(s)->deathTimer || restarts[g64Member[i]].timer;
        r->held = slot_control(s)->noControlTimer != 0;
    }
    return 1;
}

/* The balls of slots i and j change places, with their real owners. */
static void exchange_balls(s32 i, s32 j) {
    BallRef a, b;
    s32 k;
    s8 owner;
    if (i == j || !ball_ref(i, &a) || !ball_ref(j, &b))
        return;
    swap_actor(a.ball, b.ball);
    /* Each actor keeps the heap blocks it came with: a slot frees its own when its player leaves, and the level
       frees the first player's. What the blocks hold changes places. The physics block stays as it is: two plain
       balls have the same, and it can be the game's own table (ResetBall). */
    swap_bytes(&a.ball->phys, &b.ball->phys, sizeof(a.ball->phys));
    swap_bytes(&a.ball->obj, &b.ball->obj, sizeof(a.ball->obj));
    swap_bytes(&a.ball->anim, &b.ball->anim, sizeof(a.ball->anim));
    swap_bytes(&a.ball->shadow, &b.ball->shadow, sizeof(a.ball->shadow));
    swap_bytes(&a.ball->state, &b.ball->state, sizeof(a.ball->state));
    if (a.ball->obj && b.ball->obj)
        swap_bytes((ActorObject *)a.ball->obj, (ActorObject *)b.ball->obj, sizeof(ActorObject));
    if (a.ball->anim && b.ball->anim)
        swap_bytes((AnimState *)a.ball->anim, (AnimState *)b.ball->anim, sizeof(AnimState));
    if (a.ball->shadow && b.ball->shadow)
        swap_bytes((ActorShadow *)a.ball->shadow, (ActorShadow *)b.ball->shadow, sizeof(ActorShadow));
    if (a.ball->state && b.ball->state)
        swap_bytes((ActorState *)a.ball->state, (ActorState *)b.ball->state, sizeof(ActorState));
    swap_bytes(a.control, b.control, sizeof(BallControl));
    /* The ball's orbiter is an actor: its list links stay where they are. */
    swap_bytes(&a.control->orbiter, &b.control->orbiter, LINKS);
    swap_bytes(a.isStatic, b.isStatic, sizeof(s32));
    swap_bytes(a.leftBehind, b.leftBehind, sizeof(s32));
    swap_bytes(a.frameVel, b.frameVel, sizeof(Vec3));
    swap_bytes(a.pain, b.pain, 1);
    swap_bytes(a.crystal, b.crystal, sizeof(*a.crystal));
    for (k = 0; k < 6; k++)
        if (a.tex[k] && b.tex[k])
            swap_bytes(a.tex[k], b.tex[k], sizeof(u32));
    owner = ballOwner[g64Member[i]];
    ballOwner[g64Member[i]] = ballOwner[g64Member[j]];
    ballOwner[g64Member[j]] = owner;
}

/* 1 when the ball lies free and plain: no glove holds it (a held ball is linked to its glove), no enemy has it,
   it is whole and in play, and no power-up has changed it. */
static s32 ball_free(const BallRef *r) {
    const BallControl *c = r->control;
    return r->ball->link == NULL && c->state == BALL_NORMAL && c->enemy == NULL && c->loseTimer == 0
           && r->ball->radius > 0.0f && c->type == BALL_MODE_NORMAL && c->snow == 0.0f && c->cancerTimer == 0
           && c->transformState == 0 && c->orbiter.inst == NULL;
}

/* 1 when the glove of r reaches the ball of b. */
static s32 glove_reaches(const BallRef *r, const BallRef *b) {
    f32 reach = r->glove->radius + b->ball->radius + 2.0f;
    return DistanceBetweenPointsSquared(&r->glove->pos, &b->ball->pos) < reach * reach;
}

/* ball.c: the ball of the player in the globals is lost for good. It returns 1 when the ball is another player's.
   That player dies for it at the frame's end. This one lets go of it, and gets it back whole the way a respawn
   brings a ball back: at their checkpoint, or over their glove when they have none. */
s32 G64BallLostElsewhere(void) {
    s32 me = g64Member[logicSlot];
    s32 owner, at;
    if (!any_ball_on() || swapped)
        return 0;
    owner = ballOwner[me];
    at = slot_of(owner);
    if (owner == me || at < 0 || (at > 0 && !slot_used(at)))
        return 0;
    ballKill |= 1u << owner;
    if (ball.link == &hand) {
        ReleaseBall();
        ball.link = NULL;
        ApplyActionIdle();
    }
    ResetBall();
    ballControl.loseTimer = 0;
    if (handControl.checkpoint) {
        ball.pos = handControl.checkpoint->actor.pos;
    } else {
        ball.pos = hand.pos;
        ball.pos.y += 40.0f;
    }
    ZERO_VECTOR(&ball.vel);
    ball.vel.y = gravity * 6.0f + 2.0f;
    return 1;
}

/* Once a frame, after every player's own part, with the first player in the globals: the owners of lost balls
   die, and the free gloves take the free balls they touch. */
void G64AnyBallStep(void) {
    s32 i, j;
    u32 moved = 0;
    if (!any_ball_on() || swapped || logicSlot || slotCount < 2) {
        ballKill = 0;
        return;
    }
    for (i = 0; i < slotCount && ballKill; i++) {
        u32 bit = 1u << g64Member[i];
        BallRef r;
        if (!(ballKill & bit) || !ball_ref(i, &r))
            continue;
        /* A player who is held (a cameo, a tip) dies when they are free again. One who is dying already dies
           once. */
        if (r.held && !r.dying)
            continue;
        ballKill &= ~bit;
        if (r.dying)
            continue;
        if (i == 0) {
            LoseLife();
        } else if (G64HazardBegin(i)) {
            LoseLife();
            G64HazardEnd(i);
        }
        moved |= 1u << i;
    }
    /* A player who has left dies no more. */
    for (i = 0; i < G64_MAX_PLAYERS; i++)
        if ((ballKill & (1u << i)) && slot_of(i) < 0)
            ballKill &= ~(1u << i);
    for (i = 0; i < slotCount; i++) {
        BallRef mine, theirs;
        if ((moved & (1u << i)) || !ball_ref(i, &mine) || mine.dying || mine.held || !ball_free(&mine)
            || glove_reaches(&mine, &mine))
            continue;
        for (j = 0; j < slotCount; j++) {
            if (j == i || (moved & (1u << j)) || !ball_ref(j, &theirs) || theirs.dying || theirs.held
                || !ball_free(&theirs))
                continue;
            /* The ball's own player keeps it while their glove is at it. */
            if (!glove_reaches(&mine, &theirs) || glove_reaches(&theirs, &theirs))
                continue;
            exchange_balls(i, j);
            moved |= 1u << i | 1u << j;
            break;
        }
    }
}

/* For tests: the real owner of the ball in a player's slot, and a loss of that ball that ends in two frames. */
s32 G64BallOwner(s32 player) {
    return player >= 0 && player < G64_MAX_PLAYERS && slot_of(player) >= 0 ? ballOwner[player] : -1;
}

/* For tests: the frames left of a player's respawn countdown after a death, or 0. */
s32 G64PlayerRestarting(s32 player) {
    return player >= 0 && player < G64_MAX_PLAYERS ? restarts[player].timer : 0;
}

void G64TestLoseBall(s32 player) {
    s32 i = slot_of(player);
    if (i == 0)
        ballControl.loseTimer = 2;
    else if (i > 0 && slot_used(i))
        slot_ball_control(i)->loseTimer = 2;
}

PORT_C_END
