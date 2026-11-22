PORT_C_BEGIN
#include "common.h"
#include "ident.h"
#include "camera.h"
#include "ball.h"
#include "hand.h"
#include "platform.h"
#include "bullet.h"
#include "maths.h"
#include "game.h"
#include "controller.h"
#include "cameo.h"
#include "debris.h"
#include "sound.h"
#include "actor.h"
#include "banks.h"
#include "jalloc.h"
#include "bonus.h"
#include "object.h"
#include "parse.h"
#include "delta.h"
#include "g64players.h"

extern s32 controlMethod;
extern Vec3 zero;
extern s32 fireTimer;
extern f32 tweenjoy;
extern f32 tweensep;
extern s32 frameCount;
Platform *TagToFirstPlatform(s32 tag);
Bullet *FireBulletFromActor(Actor *actor, s32 type, s32 flash);
void NMEGetNewInstruction(Enemy *enemy, s32 instructionNum, s32 overrideConditions);
void AnimateActor(Actor *actor, s32 animNum, s32 loop, s32 queue, f32 speed);

extern s32 atlantisInWater;
extern f32 atbonusval1;
extern f32 atbonusval2;
extern f32 atbonusmass1;
extern s32 maxCamZ;
extern s32 maxHandZ;
extern s32 maxCamTargetZ;
extern s32 minCamZ;
extern s32 minHandZ;
extern s32 minCamTargetZ;

extern f32 cannonxspeed;
extern f32 cannonyspeed;
extern f32 cannonrecoil;
extern f32 cannonrecoilfac;
extern f32 ckbcamfrac;
extern s32 reloadCounter;
extern s32 cannonrand;
extern f32 ckbmulfx;
extern s32 ckbfxnum;
extern PTR32(char) nmeObjectName[];
extern Vec3 carnivalTempVect2;
extern Vec3 carnivalCamOffVect;
extern Vec3 carnivalCamVect;
extern Vec3 carnivalTempVect;
extern s32 carnivalReload;
extern s32 carnivalTimer;
extern f32 carnivalYPos;
extern s32 ckbScoreDisplayed;
extern s32 ckbNumShots;
extern u8 gamePaused;
extern u8 ActiveController;
extern Pad pad[4];
u32 UpdateCRC(char *ptr);

/* Port-side state for the frame-rate rewrite (docs/delta-time.md), outside port_ram. */
/* The frame windows in which the set-ups of the levels and the control room's camera snap last ran (DeltaFirst). */
static u32 carnivalBonusSetUpFrame;
static u32 atlantisBonusSetUpFrame;
static u32 controlRoomSnapFrame;

/* g64coopdx: the slot of the player whose part runs (EachOtherPlayer). */
static s32 cannonSlot;

/* g64coopdx: runs a bonus level's per-player part for each player who is not in the globals, with them in the
   globals. */
static void EachOtherPlayer(void (*part)(void)) {
    s32 p;
    s32 players = G64SlotCount();

    for (p = 1; p < players; p++) {
        if (G64HazardBegin(p)) {
            cannonSlot = p;
            part();
            G64HazardEnd(p);
        }
    }
}

/* A shot from the glove in the globals: the bullet, its random shape, its sounds and its sparks. The same for the
   first player and, online, for every other player on the cannon. */
static void CarnivalShot(void) {
    Bullet *bul;
    Debris *d;
    f32 ang;
    s32 j;

    ckbNumShots++;
    bul = FireBulletFromActor(&hand, BALL_BULLET, 0);
    if (bul != NULL) {
        switch (Random(15)) {
            case 0:
                FindObject(&bul->actor.inst, UpdateCRC(nmeObjectName[28]), nmeObjectName[28], 1);
                bul->actor.obj->spheres[0].radius = bul->actor.radius = defaultBehaviour[28].radius;
                bul->actor.spinSpeed = 0.6f;
                PlaySample(25, &bul->actor.pos, 80, (Random(24) + 116));
                break;
            case 1:
                FindObject(&bul->actor.inst, UpdateCRC(nmeObjectName[29]), nmeObjectName[29], 1);
                bul->actor.obj->spheres[0].radius = bul->actor.radius = defaultBehaviour[29].radius;
                PlaySample(19, &bul->actor.pos, 80, (Random(24) + 116));
                break;
            case 2:
                FindObject(&bul->actor.inst, UpdateCRC(nmeObjectName[30]), nmeObjectName[30], 1);
                bul->actor.obj->spheres[0].radius = bul->actor.radius = defaultBehaviour[30].radius;
                PlaySample(21, &bul->actor.pos, 80, (Random(24) + 116));
                break;
        }
        PlaySample(27, &bul->actor.pos, 80, (Random(24) + 116));
        AddDebris(POPB, &bul->actor.pos, &zero, NULL, NULL);
        ang = Random(62) / 10;
        for (j = 0; j < 5; j++) {
            carnivalTempVect.x = carnivalTempVect.z = 0.0f;
            carnivalTempVect.y = 8.0f;
            RotateVector2DXYZ(&carnivalTempVect, &carnivalTempVect, ang + j * 2 * 3.141592654 / 5.0, 2);
            RotateVectorByQuaternion(&carnivalTempVect, &carnivalTempVect, &hand.rotQuat);
            ADD_VECTORS(&carnivalCamVect, &carnivalTempVect, &bul->actor.pos);
            d = AddDebris(WIND_BIT, &carnivalCamVect, &carnivalTempVect, NULL, NULL);
            if (d != NULL) {
                d->lifeTimer = 5;
                d->info->friction = 0.6f;
                d->friction = 0.6f;
            }
        }
        for (j = 0; j < 10; j++) {
            carnivalTempVect = bul->actor.vel;
            carnivalTempVect.x += Random(15) - 7;
            carnivalTempVect.y += Random(15) - 7;
            carnivalTempVect.z += Random(15) - 7;
            if (j < 5) {
                d = AddDebris(WIND_BIT, &bul->actor.pos, &carnivalTempVect, NULL, NULL);
                if (d != NULL) {
                    d->lifeTimer = 8;
                    d->info->friction = 0.6f;
                    d->friction = 0.6f;
                }
            } else {
                d = AddDebris(STAR, &bul->actor.pos, &carnivalTempVect, NULL, NULL);
                if (d != NULL) {
                    d->flags &= ~0x800000;
                    d->info->friction = 0.6f;
                    d->friction = 0.6f;
                }
            }
        }
    }
}

extern Vec3 spaceBossTempVect2;

/* The cannon's platform, for CarnivalBonusPlayer. */
static Platform *carnivalPlat;
/* g64coopdx: each other player's own aim (the turn and the tilt of the cannon as they see it) and the frames until
   their next shot, by the player's slot. The first player's are the cannon's own, as in the original. */
static f32 cannonAim[G64_MAX_PLAYERS][2];
static s32 cannonReload[G64_MAX_PLAYERS];

/* The bullets the level allows at once: the original's 8 for one player, and 4 more for each other player. */
static s32 G64CannonBullets(void) {
    s32 n = 8 + 4 * (G64SlotCount() - 1);
    return n > 40 ? 40 : n;
}

/* The Carnival bonus level's part for a player other than the first: their glove is held on the cannon as the first
   player's is, and their camera stands behind the cannon. The camera's aim is the one the level has worked out for
   the first player in this update: every glove is at the same place. The camera's place is worked out here, because
   a shot's sparks use the level's variable for it. */
static void CarnivalBonusPlayer(void) {
    Platform *plat = carnivalPlat;
    f32 *aim = cannonAim[cannonSlot & (G64_MAX_PLAYERS - 1)];
    s32 *reload = &cannonReload[cannonSlot & (G64_MAX_PLAYERS - 1)];
    s32 free = !gamePaused && (cameoList.head.next == NULL || !cameoList.head.next->active);
    Vec3 rot;
    Vec3 up;
    Vec3 spot;
    Vec3 target;
    f32 keep;

    camera.flags &= ~0x10;
    if (hand.link != &ball) {
        hand.link = &ball;
        AnimateActor(&hand, HANDANIM_DENIS, 0, 0, 0);
    }
    ZERO_VECTOR(&hand.vel);
    if (gameInfo.cameoActive != 0) {
        cameraInfo.followSpeed = 0;
        cameraInfo.followTimer = 5;
    }
    hand.flags = 0;
    hand.state->mode = HAND_NOTHING;
    /* This player's own stick turns their aim, within the cannon's limits. */
    if (free) {
        aim[0] = Aabs(aim[0] - pad[ActiveController].stickX / cannonxspeed * deltaTime);
        aim[1] = MIN(MAX(5.4977871445, aim[1] + pad[ActiveController].stickY / cannonyspeed * deltaTime), 6.282185308);
    }
    rot = plat->motion->rot;
    rot.y = aim[0];
    rot.x = aim[1];
    GetQuaternionFromXYZRot(&hand.rotQuat, &rot);
    up.x = 0.0f;
    up.y = 13.0f;
    up.z = 0.0f;
    RotateVectorByQuaternion(&up, &up, &hand.rotQuat);
    ADD_VECTORS(&hand.pos, &up, &plat->actor.pos);
    if (!cameoList.head.next->active) {
        /* The camera stands behind this player's aim and looks along it. */
        target.x = target.y = 0.0f;
        target.z = 40.0f;
        RotateVectorByQuaternion(&target, &target, &hand.rotQuat);
        ADD_TO_VECTOR(&target, &hand.pos);
        RotateVectorByQuaternion(&spot, &carnivalCamOffVect, &hand.rotQuat);
        ADD_TO_VECTOR(&spot, &hand.pos);
        keep = DeltaPow(ckbcamfrac);
        AddScaledVector(&camera.pos, &camera.pos, keep, &spot, DeltaTake(ckbcamfrac, 1.0f - ckbcamfrac));
        /* The camera keeps a pointer to what it looks at, which must be in the game's memory and this player's
           own. The last Space boss's aim point is such a variable (g64players.c swaps it with the player), and
           this level does not use it. */
        spaceBossTempVect2 = target;
        SetCameraOverride(camera.pos.x, camera.pos.y, camera.pos.z, &spaceBossTempVect2, &zero, 10, 0);
        if (free) {
            if (deltaTick && *reload != 0) {
                (*reload)--;
            }
            /* Their own A fires along their aim. The cannon's recoil is the first player's alone. */
            if (bulletList.count < G64CannonBullets() && *reload == 0 && pad[ActiveController].pressed[BUTTON_A]) {
                *reload = reloadCounter;
                CarnivalShot();
            }
        }
    }
}

/* g64coopdx: before a player's view is drawn (G64CannonView, g64players.c): the cannon points where that player
   aims. slot is the player's slot; the first player's aim is the cannon's own. The client loads the state again
   after the view. */
void CarnivalView(s32 slot) {
    Platform *plat = carnivalPlat;
    Platform *side;
    Vec3 at;
    s32 tag;

    if (gameInfo.level != CARNIVALBONUS || plat == NULL || slot < 1 || slot >= G64_MAX_PLAYERS) {
        return;
    }
    plat->motion->rot.y = cannonAim[slot][0];
    plat->motion->rot.x = cannonAim[slot][1];
    GetQuaternionFromXYZRot(&plat->actor.rotQuat, &plat->motion->rot);
    for (tag = 2; tag <= 3; tag++) {
        side = TagToFirstPlatform(tag);
        side->motion->rot.y = plat->motion->rot.y;
        at.x = tag == 2 ? -12.0f : 12.0f;
        at.y = -5.0f;
        at.z = -2.0f;
        RotateVector2D(&side->actor.pos, &at, side->motion->rot.y);
        GetQuaternionFromXYZRot(&side->actor.rotQuat, &side->motion->rot);
    }
}

void DoCarnivalBonus(void) {
    Platform *plat;
    Platform *platform;
    f32 ang;
    f32 keep;
    s32 numTargetsHit;
    s32 j;

    numTargetsHit = 0;
    plat = TagToFirstPlatform(1);
    if (frameCount == 0 && DeltaFirst(&carnivalBonusSetUpFrame)) {
        camera.flags &= ~0x10;
        ckbScoreDisplayed = 0;
        carnivalTimer = 0;
        ckbNumShots = 0;
        carnivalReload = 0;
        plat->motion->rot.x = 5.9f;
        for (j = 0; j < G64_MAX_PLAYERS; j++) {
            cannonAim[j][0] = plat->motion->rot.y;
            cannonAim[j][1] = plat->motion->rot.x;
            cannonReload[j] = 0;
        }
        hand.link = &ball;
        AnimateActor(&hand, HANDANIM_DENIS, 0, 0, 0);
        carnivalCamOffVect.x = 0.0f;
        carnivalYPos = 0.0f;
        carnivalCamOffVect.y = 40.0f;
        carnivalCamOffVect.z = -60.0f;
    }
    ZERO_VECTOR(&hand.vel);
    for (j = 70; j < 80; j++) {
        platform = TagToFirstPlatform(j);
        if (platform->path->target == 1 || (platform->path->current == 1 && platform->actor.speed == 0.0f)) {
            numTargetsHit++;
        }
    }
    for (j = 90; j < 100; j++) {
        platform = TagToFirstPlatform(j);
        if (platform->path->target == 1 || (platform->path->current == 1 && platform->actor.speed == 0.0f)) {
            numTargetsHit++;
        }
    }
    for (j = 110; j < 120; j++) {
        platform = TagToFirstPlatform(j);
        if (platform->path->target == 1 || (platform->path->current == 1 && platform->actor.speed == 0.0f)) {
            numTargetsHit++;
        }
    }
    /* The score shown counts towards the targets hit, and the exit waits, once per original frame. */
    if (deltaTick) {
        if (ckbScoreDisplayed != numTargetsHit) {
            if (numTargetsHit >= ckbScoreDisplayed) {
                ckbScoreDisplayed++;
            } else {
                ckbScoreDisplayed--;
            }
        }
        if (ckbScoreDisplayed == 30) {
            if (carnivalTimer == 0) {
                carnivalTimer = 60;
            } else if (--carnivalTimer == 0) {
                ExitLevel(BONUS_DONE_EXIT, 1, 1);
            }
        } else {
            carnivalTimer = 0;
        }
    }
    if (gameInfo.cameoActive != 0) {
        cameraInfo.followSpeed = 0;
        cameraInfo.followTimer = 5;
    }
    hand.flags = 0;
    hand.state->mode = HAND_NOTHING;
    plat->actor.flags |= 0x4000;
    plat->motion->oldRot.y = plat->motion->rot.y;
    plat->motion->oldRot.x = plat->motion->rot.x;
    if (!gamePaused && (cameoList.head.next == NULL || !cameoList.head.next->active)) {
        /* g64coopdx: the first player's stick turns the cannon. Every other player has an aim of their own
           (CarnivalBonusPlayer), and in their view the cannon points along it (CarnivalView). */
        plat->motion->rot.y = Aabs(plat->motion->rot.y - (f32)pad[ActiveController].stickX / cannonxspeed * deltaTime);
        plat->motion->rot.x = MIN(MAX(5.4977871445,
                                      plat->motion->rot.x + (f32)pad[ActiveController].stickY / cannonyspeed * deltaTime),
                                  6.282185308);
    }
    ang = FindShortestAngle(plat->motion->oldRot.y, plat->motion->rot.y);
    ang = MAX(ang, FindShortestAngle(plat->motion->oldRot.x, plat->motion->rot.x));
    /* The turn of this update as a rate per original frame: the sound follows the turning speed. */
    ang = DeltaPerFrame(ang);
    if (ang != 0.0f) {
        if (handControl.samples[0].handle == 0) {
            PlayContinuousSample(&handControl.samples[0], ckbfxnum, 0, &hand.pos, 128);
        }
        handControl.samples[0].volume = MIN(ang * ckbmulfx, 100.0f);
        handControl.samples[0].pitch = ang * 1000.0f + 128.0f;
    } else if (deltaTick && handControl.samples[0].handle != 0) {
        handControl.samples[0].volume -= 5;
        if (handControl.samples[0].volume <= 0) {
            StopContinuousSample(&handControl.samples[0]);
        }
    }
    /* The cannon shakes once per original frame. */
    if (deltaTick) {
        if (plat->motion->rot.x != plat->motion->oldRot.x || plat->motion->rot.y != plat->motion->oldRot.y) {
            carnivalYPos = ((f32)Random(cannonrand * 2 | 1) - cannonrand) / 10.0;
        } else {
            carnivalYPos = carnivalYPos / 2.0f;
        }
    }
    platform = TagToFirstPlatform(2);
    platform->motion->rot.y = plat->motion->rot.y;
    carnivalTempVect.x = -12.0f;
    carnivalTempVect.y = -5.0f;
    carnivalTempVect.z = -2.0f;
    RotateVector2D(&platform->actor.pos, &carnivalTempVect, platform->motion->rot.y);
    platform = TagToFirstPlatform(3);
    platform->motion->rot.y = plat->motion->rot.y;
    carnivalTempVect.x = 12.0f;
    carnivalTempVect.y = -5.0f;
    carnivalTempVect.z = -2.0f;
    RotateVector2D(&platform->actor.pos, &carnivalTempVect, platform->motion->rot.y);
    GetQuaternionFromXYZRot(&plat->actor.rotQuat, &plat->motion->rot);
    hand.rotQuat = plat->actor.rotQuat;
    carnivalTempVect.x = 0.0f;
    carnivalTempVect.y = 13.0f;
    carnivalTempVect.z = 0.0f;
    RotateVectorByQuaternion(&carnivalTempVect, &carnivalTempVect, &hand.rotQuat);
    ADD_VECTORS(&hand.pos, &carnivalTempVect, &plat->actor.pos);
    if (!cameoList.head.next->active) {
        carnivalTempVect2.x = carnivalTempVect2.y = 0.0f;
        carnivalTempVect2.z = 40.0f;
        RotateVectorByQuaternion(&carnivalTempVect2, &carnivalTempVect2, &hand.rotQuat);
        RotateVectorByQuaternion(&carnivalCamVect, &carnivalCamOffVect, &hand.rotQuat);
        ADD_TO_VECTOR(&carnivalTempVect2, &hand.pos);
        ADD_TO_VECTOR(&carnivalCamVect, &hand.pos);
        keep = DeltaPow(ckbcamfrac);
        AddScaledVector(&camera.pos, &camera.pos, keep, &carnivalCamVect, DeltaTake(ckbcamfrac, 1.0f - ckbcamfrac));
        SetCameraOverride(camera.pos.x, camera.pos.y, camera.pos.z, &carnivalTempVect2, &zero, 10, 0);
        if (!gamePaused && (cameoList.head.next == NULL || !cameoList.head.next->active)) {
            if (deltaTick && carnivalReload != 0) {
                carnivalReload--;
            }
            /* The recoil dies away. */
            keep = DeltaPow(cannonrecoilfac);
            SCALE_VECTOR(&plat->actor.pos, keep);
            /* g64coopdx: each player fires with their own A (CarnivalBonusPlayer has the others). */
            if (bulletList.count < G64CannonBullets() && carnivalReload == 0 && pad[ActiveController].pressed[BUTTON_A]) {
                carnivalTempVect.x = carnivalTempVect.y = 0.0f;
                carnivalTempVect.z = cannonrecoil;
                RotateVectorByQuaternion(&carnivalTempVect, &carnivalTempVect, &hand.rotQuat);
                ADD_TO_VECTOR(&plat->actor.pos, &carnivalTempVect);
                carnivalReload = reloadCounter;
                CarnivalShot();
            }
            TagToFirstPlatform(60)->motion->speed =
                (frameCount / 30000.0f < 0.025) ? frameCount / 30000.0f * 0.55 + 0.015 : 0.02875f;
            TagToFirstPlatform(157)->motion->speed =
                (frameCount / 30000.0f < 0.025) ? -(frameCount / 30000.0f + 0.015) * 0.66 : -0.0264f;
            TagToFirstPlatform(129)->motion->speed =
                (frameCount / 30000.0f < 0.025) ? (frameCount / 30000.0f + 0.015) * 0.7 : 0.028f;
            if (deltaTick) {
                plat->actor.pos.y += carnivalYPos;
            }
            carnivalTempVect.x = 0.0f;
            carnivalTempVect.y = 13.0f;
            carnivalTempVect.z = 0.0f;
            RotateVectorByQuaternion(&carnivalTempVect, &carnivalTempVect, &hand.rotQuat);
            ADD_VECTORS(&hand.pos, &carnivalTempVect, &plat->actor.pos);
        }
    }
    /* g64coopdx: every other player sits on the cannon too and has its camera. */
    carnivalPlat = plat;
    EachOtherPlayer(CarnivalBonusPlayer);
}

static void FortressBonusPlayer(void) {
    cameraInfo.sideOffsetEnabled = 0;
    SetControlToBall();
    /* The camera's requests and overrideTimer change in ticks. */
    if (deltaTick && ballControl.loseTimer && !cameraInfo.destActive && !cameraInfo.overrideTimer) {
        SetCameraOverride(camera.pos.x, camera.pos.y, camera.pos.z, &cameraInfo.actor.pos, &zero, 60, 0);
    }
}

void DoFortressBonus(void) {
    FortressBonusPlayer();
    EachOtherPlayer(FortressBonusPlayer);
}

static void PrehistoricBonusPlayer(void) {
    if (deltaTick && ballControl.loseTimer && !cameraInfo.destActive && !cameraInfo.overrideTimer) {
        SetCameraOverride(camera.pos.x, camera.pos.y, camera.pos.z, &cameraInfo.actor.pos, &zero, 60, 0);
    }
    if (ballControl.loseTimer) {
        camera.pos.z += 5.8 * deltaTime;
    }
    SetControlToBall();
    /* g64coopdx: online each player wins when their own ball arrives. */
    if (deltaTick && ball.pos.z > 3750.0f && !G64PlayerExits(BONUS_DONE_EXIT, 1)) {
        ExitLevel(BONUS_DONE_EXIT, 1, 1);
    }
}

void DoPrehistoricBonus(void) {
    PrehistoricBonusPlayer();
    EachOtherPlayer(PrehistoricBonusPlayer);
}

void SetControlToBall(void) {
    hand.alpha = 0;
    ZERO_VECTOR(&hand.scale);
    if (controlMethod == CONTROL_HAND && !handControl.noControlTimer) {
        cameraInfo.focus = &ball.pos;
        cameraInfo.focusVel = &ball.vel;
        controlMethod = CONTROL_BALL;
    }
    if (ball.link != &hand) {
        ApplyAction(HAND_JOINED);
    }
}

void SetControlToHand(void) {
    hand.scale.x = hand.scale.y = hand.scale.z = 0.05f;
    hand.alpha = 255;
    cameraInfo.focus = &hand.pos;
    cameraInfo.focusVel = &hand.vel;
    if (!controlMethod) {
        DeactivateCameraControlMode();
    }
    controlMethod = CONTROL_HAND;
}

/* The first part of the Atlantis bonus level that each player has: the fail in the water or out of the level's range,
   the camera's range, the frog spell's hold and the glove's damping on a surface. */
static void AtlantisBonusPlayer(void) {
    f64 damp;

    /* The time in the water and the frog spell's hold count original frames. The exits happen in ticks. */
    if (deltaTick && hand.state->waterState == INWATER_SWIMMING && !(gameInfo.exitFlags & 1)) {
        if (++atlantisInWater > 20) {
            if (!G64PlayerExits(BONUS_DONE_EXIT, 0)) {
                ExitLevel(BONUS_DONE_EXIT, 0, 0);
            }
        }
    }
    if (camera.pos.z >= maxCamZ) {
        camera.pos.z = maxCamZ;
    } else if (camera.pos.z <= minCamZ) {
        camera.pos.z = minCamZ;
    }
    if (cameraInfo.actor.pos.z >= maxCamTargetZ) {
        cameraInfo.actor.pos.z = maxCamTargetZ;
        cameraInfo.followSpeed = 0;
        cameraInfo.followTimer = 2;
    }
    if (cameraInfo.actor.pos.z <= minCamTargetZ) {
        cameraInfo.actor.pos.z = minCamTargetZ;
        cameraInfo.followSpeed = 0;
        cameraInfo.followTimer = 2;
    }
    if (deltaTick && (hand.pos.z > maxHandZ || hand.pos.z < minHandZ) && !G64PlayerExits(BONUS_DONE_EXIT, 0)) {
        ExitLevel(BONUS_DONE_EXIT, 0, 0);
    }
    if (deltaTick && handControl.frog) {
        handControl.spellTimer[2]++;
    }
    cameraInfo.sideOffsetEnabled = 0;
    if (hand.obj->planes[0] != NULL && hand.vel.y < 0.0f) {
        damp = DeltaDampD(0.6);
        SCALE_VECTOR_2D(&hand.vel, damp);
    }
}

/* The second part: a pad sinks under the glove that stands on it. */
static void AtlantisBonusPad(void) {
    if (deltaTick && hand.obj->platform != NULL && hand.obj->platform->parent != NULL
        && hand.obj->platform->parent->tag == 3 && hand.obj->platform->path->moving < -99) {
        hand.obj->platform->path->moving = 25;
    }
}

void DoAtlantisBonus(void) {
    Vec3 tempVect;
    Platform *plat;
    Platform *platform;

    AtlantisBonusPlayer();
    if (frameCount == 0 && DeltaFirst(&atlantisBonusSetUpFrame)) {
        atlantisInWater = 0;
        StopDrawing("atbonus");
        for (plat = platformList.head.next; plat != &platformList.head; plat = plat->next) {
            if (plat->tag == 1 || plat->tag == 2) {
                plat->flags &= ~0x20000;
                tempVect = plat->actor.pos;
                if (plat->tag == 1) {
                    plat->push = (PlatformPush *)JallocAlloc(sizeof(PlatformPush), 1, "Plat_Push");
                    plat->push->axis[1] = 1;
                    SetPlatformPos(plat, &tempVect, 0.0f);
                    plat->maxSpeed = 1000.0f;
                } else if (plat->tag == 2) {
                    platform = (Platform *)JallocAlloc(sizeof(Platform), 1, "Plat");
                    platform->actor.type = PLAT_ACTOR;
                    platform->flags |= 0x80;
                    InitActorStructures(&platform->actor, 0);
                    platform->actor.friction = 1.0f;
                    AddPlatform(platform);
                    platform->tag = 3;
                    platform->maxSpeed = 1000.0f;
                    PlatformAddPoint(platform, tempVect.x, tempVect.y, 600.0f, 0);
                    PlatformAddPoint(platform, tempVect.x, tempVect.y, -4900.0f, 0);
                    platform->path->accel = 0.001f;
                    plat->parent = platform;
                    PlatformAddPoint(plat, 0.0f, 0.0f, 0.0f, -100);
                    PlatformAddPoint(plat, 0.0f, -30.0f, 0.0f, 65);
                    SetPlatformPos(platform, &tempVect, 0.0f);
                    SetPlatformPos(plat, &zero, 0.0f);
                    plat->tag = 0;
                    plat->path->accel = 0.015f;
                    plat->maxSpeed = 3.0f;
                }
            }
        }
        StartDrawing("atbonus");
    }
    AtlantisBonusPad();
    /* g64coopdx: each other player has both parts too. */
    EachOtherPlayer(AtlantisBonusPlayer);
    EachOtherPlayer(AtlantisBonusPad);
    for (plat = platformList.head.next; plat != &platformList.head; plat = plat->next) {
        if (plat->tag == 1) {
            /* MovePlatform damps the velocity by DeltaDamp(friction) before the step. The log moves 1 per original
               frame. */
            plat->actor.vel.z = 1.0f / DeltaDamp(atbonusval1);
            if (plat->actor.pos.z > 200.0f) {
                plat->actor.pos.z = -4500.0f;
            }
            plat->push->mass = atbonusmass1;
            plat->actor.friction = atbonusval1;
        }
        if (plat->tag == 3) {
            plat->path->points[0]->pos.y = plat->actor.pos.y;
            plat->path->points[1]->pos.y = plat->actor.pos.y;
            plat->actor.vel.z = -2.0f / DeltaDamp(atbonusval2);
            if (plat->actor.pos.z < -4500.0f) {
                plat->actor.pos.z = 200.0f;
            }
            plat->actor.friction = atbonusval2;
        }
    }
}

void DoControlRoom(void) {
    Platform *joy = TagToFirstPlatform(4);
    Platform *platform2 = TagToFirstPlatform(5);
    Bullet *bullet;
    Vec3 tempVect;

    if (frameCount < 130) {
        cameraInfo.actor.pos.x = -8.1f;
        cameraInfo.actor.pos.y = 22.7f;
        cameraInfo.actor.pos.z = 268.0f;
        if (frameCount == 1 && DeltaFirst(&controlRoomSnapFrame)) {
            camera.pos = cameraInfo.destPos;
        }
    }
    camera.flags &= ~0x10;
    if (hand.state->mode == HAND_NOTHING) {
        /* fireTimer counts original frames. */
        if (deltaTick) {
            fireTimer++;
        }
        if (joy->actor.pos.z > -100.0f) {
            /* A quarter of the way per original frame. x * 0.25f equals x / 4.0f. */
            joy->actor.pos.z -= (joy->actor.pos.z + 100.0f) * DeltaLerp(0.25f);
        }
        if (deltaTick && (fireTimer == 30 || fireTimer == 60)) {
            bullet = FireBulletFromActor(&hand, SPACEBALL_BULLET, 0);
            joy->actor.pos.z += tweenjoy;
            if (fireTimer == 30) {
                enemies[1].actor.vel.x += 15.0f;
            } else {
                enemies[1].actor.vel.x -= 15.0f;
            }
            if (bullet) {
                bullet->actor.pos.z -= 50.0f;
                bullet->actor.vel.y += 5.0f;
            }
        }
        if (fireTimer >= 70) {
            if (deltaTick && fireTimer == 70) {
                NMEGetNewInstruction(&enemies[1], 4, 1);
                cameraInfo.focus = &enemies[1].actor.pos;
                cameraInfo.focusVel = &enemies[1].actor.vel;
                cameraInfo.destPos = camera.pos;
            }
            cameraInfo.destActive = 1;
        }
        hand.rotAim.y = 3.1415927f;
        tempVect = joy->actor.pos;
        tempVect.y += 17.0f;
        tempVect.z += 4.0f;
        if (fireTimer >= 30) {
            hand.pos.z = joy->actor.pos.z + 4.0f;
        } else {
            AddScaledVector(&hand.pos, &hand.pos, DeltaPow(0.7f), &tempVect, DeltaTake(0.7f, 0.3f));
        }
    } else if (DistanceBetweenPointsSquared(&hand.pos, &joy->actor.pos) < tweensep) {
        fireTimer = 0;
        ZERO_VECTOR(&hand.vel);
        hand.state->mode = HAND_NOTHING;
        AnimateActor(&hand, HANDANIM_SLAM, 0, 0, 0.0f);
        hand.flags &= ~0x11;
    }
    platform2->actor.pos.z = joy->actor.pos.z;
}

PORT_C_END
