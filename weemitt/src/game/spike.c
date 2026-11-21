PORT_C_BEGIN
#include "common.h"
#include "ident.h"
#include "spike.h"
#include "jalloc.h"
#include "maths.h"
#include "actor.h"
#include "ball.h"
#include "enemy.h"
#include "delta.h"
#include "g64players.h"

extern s32 SPIKE_RADIUS;

void HurtHand(s32 type, s32 damage);

INCLUDE_ASM("asm/nonmatchings/spike", pad_8018E37C);

/* The spikes test for hits once per original frame, at the original positions. */
void UpdateSpikes(void) {
    Spike *spike;
    Actor *actPtr;
    Vec3 spikePos;
    s32 i;
    s32 n;

    if (!deltaTick) {
        return;
    }
    for (spike = spikeList.head.next; spike != &spikeList.head; spike = spike->next) {
        spikePos = spike->platform->actor.pos;
        for (actPtr = actorList.head.next; actPtr != &actorList.head; actPtr = actPtr->next) {
            if (actPtr == &hand) {
                if ((LENGTH_2D(&hand.vel) > 1.6 || !hand.obj->onGround)
                    && DistanceBetweenPoints(&actPtr->pos, &spikePos) < hand.obj->spheres[0].radius + 1.0 + SPIKE_RADIUS) {
                    HurtHand(HURT_GO_BACKWARDS, 1);
                }
            } else if (actPtr == &ball) {
                if (ballControl.state != BALL_BURST && DistanceBetweenPoints(&actPtr->pos, &spikePos) < ball.radius + 1.0 + SPIKE_RADIUS) {
                    BurstBall();
                }
            } else if (actPtr->type == BUGLE && DistanceBetweenPoints(&actPtr->pos, &spikePos) < actPtr->obj->spheres[0].radius + 4.0 + SPIKE_RADIUS) {
                KillEnemy(&enemies[actPtr->state->index]);
            }
        }
        /* g64coopdx: the other players' gloves and balls, tested where they are; a hit swaps that player into the
           globals (g64players.c). */
        n = G64SlotCount();
        for (i = 1; i < n; i++) {
            Actor *h = G64SlotGlove(i);
            Actor *b = G64SlotBall(i);
            if (h != NULL && (LENGTH_2D(&h->vel) > 1.6 || !h->obj->onGround)
                && DistanceBetweenPoints(&h->pos, &spikePos) < h->obj->spheres[0].radius + 1.0 + SPIKE_RADIUS
                && G64HazardBegin(i)) {
                HurtHand(HURT_GO_BACKWARDS, 1);
                G64HazardEnd(i);
            }
            if (b != NULL && !G64SlotBallBursting(i) && DistanceBetweenPoints(&b->pos, &spikePos) < b->radius + 1.0 + SPIKE_RADIUS
                && G64HazardBegin(i)) {
                BurstBall();
                G64HazardEnd(i);
            }
        }
    }
}

void InitSpikeList(void) {
    spikeList.count = 0;
    spikeList.head.next = spikeList.head.prev = &spikeList.head;
}

void AddSpike(Spike *spike) {
    if (spike->next == NULL) {
        spikeList.count++;
        spike->prev = &spikeList.head;
        spike->next = spikeList.head.next;
        spikeList.head.next->prev = spike;
        spikeList.head.next = spike;
    }
}

INCLUDE_ASM("asm/nonmatchings/spike", pad_8018E658);

INCLUDE_RODATA("asm/nonmatchings/spike", pad_8010980C);

PORT_C_END
