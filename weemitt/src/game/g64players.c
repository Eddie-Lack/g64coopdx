/* g64coopdx: more than one player in one game, by swapping. A prototype (docs/discovery/state-inventory.md, "Two
   ways to make the game multi-player").

   The game has one glove, one ball and one camera, and its code tests for them by address (actor == &hand). So the
   other players do not get globals of their own. Each extra player has a slot on the game's heap that holds their
   copy of the per-player globals. In each frame, after the first player's part of the frame, the slot is swapped
   into the globals, that player's part of the frame runs, and the slot is swapped back out.

   What a swap leaves alone: the list links of every swapped actor (the first 0x18 bytes: the actor lists hold the
   globals' addresses), and every world system. Enemies, collectables, puzzles and platforms still see only the first
   player. With one player nothing here runs, and the game is unchanged. */
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
PORT_C_END

#include <string.h>

PORT_C_BEGIN
#include "g64players.h"

extern u8 ActiveController;
extern u8 gamePaused;
extern OSContPad controllerdata[];
extern Vec3 oldHandPos;
extern s32 ballWithHand;
extern s32 ballLeftBehind;
extern f32 handInertia;
extern Vec3 ballFrameVel;

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
u16 g64Buttons[G64_MAX_PLAYERS];
s8 g64StickX[G64_MAX_PLAYERS];
s8 g64StickY[G64_MAX_PLAYERS];

/* The bytes of handControl and player: player starts inside handControl's last word (gen/ram.S), so they are one
   block. */
#define HAND_BLOCK_MAX 0x640
/* An actor's list links: next, prev, collNext, collPrev, distNext, distPrev. */
#define LINKS 0x18

typedef struct G64Slot {
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
} G64Slot;

/* Each extra player's slot, on the game's heap. Slot 0 is unused: player 0 lives in the globals. */
static PTR32(G64Slot) slots[G64_MAX_PLAYERS];
static s32 slotCount;
/* Actors that only show what a slot holds: its glove and its ball. They are in the actor list, so the game draws
   them, and their contents are copied from the slot before each draw. A swapped slot holds another player's state,
   so the proxies always show the players who are not in the globals. */
static PTR32(Actor) proxyHand[G64_MAX_PLAYERS];
static PTR32(Actor) proxyBall[G64_MAX_PLAYERS];
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
    return a;
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
    swap_with_actors(&handControl, s->handBlock, hand_block_size(), handActors, 5);
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

void G64ExtraPlayersStart(void) {
    s32 i;
    G64ExtraPlayersFree();
    if (g64Players < 2 || hand_block_size() > HAND_BLOCK_MAX)
        return;
    slotCount = g64Players > G64_MAX_PLAYERS ? G64_MAX_PLAYERS : g64Players;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = (G64Slot *)JallocAlloc(sizeof(G64Slot), 1, "G64Slot");
        /* The new player starts as a copy of the first one, with heap blocks of their own. */
        memcpy(&s->hand, &hand, sizeof(Actor));
        memcpy(&s->ball, &ball, sizeof(Actor));
        memcpy(&s->camera, &camera, sizeof(Actor));
        memcpy(&s->cameraInfo, &cameraInfo, sizeof(CameraInfo));
        memcpy(s->handBlock, &handControl, hand_block_size());
        memcpy(&s->ballControl, &ballControl, sizeof(BallControl));
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
        clone_blocks(&s->hand);
        clone_blocks(&s->ball);
        clone_blocks(&s->camera);
        clone_blocks(&s->cameraInfo.actor);
        clone_blocks(slot_hand_actor(s, &handControl.edgeProbe));
        clone_blocks(slot_hand_actor(s, &handControl.frontProbe));
        /* Side by side, not inside each other. */
        s->hand.pos.x += 40.0f * i;
        s->ball.pos.x += 40.0f * i;
        slots[i] = s;
        proxyHand[i] = new_proxy(&s->hand);
        proxyBall[i] = new_proxy(&s->ball);
    }
}

void G64UpdateProxies(void) {
    s32 i;
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (s && proxyHand[i] && proxyBall[i]) {
            copy_to_proxy(proxyHand[i], &s->hand);
            copy_to_proxy(proxyBall[i], &s->ball);
        }
    }
}

void G64ExtraPlayersFree(void) {
    s32 i;
    for (i = 1; i < G64_MAX_PLAYERS; i++) {
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
            continue;
        free_blocks(&s->hand);
        free_blocks(&s->ball);
        free_blocks(&s->camera);
        free_blocks(&s->cameraInfo.actor);
        free_blocks(slot_hand_actor(s, &handControl.edgeProbe));
        free_blocks(slot_hand_actor(s, &handControl.frontProbe));
        JallocFree(&slots[i]);
    }
    slotCount = 0;
}

/* One extra player's part of the frame, in the order MainGameLoop runs the first player's (main.c). */
static void player_frame(s32 p) {
    Vec3 tempVect;
    if (deltaNewFrame) {
        OSContPad saved = controllerdata[ActiveController];
        controllerdata[ActiveController].button = g64Buttons[p];
        controllerdata[ActiveController].stick_x = g64StickX[p];
        controllerdata[ActiveController].stick_y = g64StickY[p];
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
}

void G64ExtraPlayersStep(void) {
    s32 i;
    if (g64Players < 2 || gamePaused || !GameBeingPlayed())
        return;
    /* The other players start in the level's first frame of play, when the level has set the first player up (its
       start position, its first checkpoint). */
    if (slotCount == 0)
        G64ExtraPlayersStart();
    for (i = 1; i < slotCount; i++) {
        G64Slot *s = slots[i];
        if (!s)
            continue;
        swap_slot(s);
        player_frame(i);
        swap_slot(s);
    }
}

s32 G64PlayerPosition(s32 p, f32 *x, f32 *y, f32 *z) {
    const Actor *a;
    if (p == 0) {
        a = &hand;
    } else if (p < slotCount && slots[p]) {
        a = &((G64Slot *)slots[p])->hand;
    } else {
        return 0;
    }
    *x = a->pos.x;
    *y = a->pos.y;
    *z = a->pos.z;
    return 1;
}
PORT_C_END
