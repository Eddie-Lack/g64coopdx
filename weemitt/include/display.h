#ifndef DISPLAY_H
#define DISPLAY_H

#include "common.h"

typedef struct Dynamic {
    /* 0x0000 */ Mtx projection;
    /* 0x0040 */ Mtx translation;
    /* 0x0080 */ Mtx objectMtx[256];
    /* 0x4080 */ Mtx cameraView;
    /* 0x40C0 */ Mtx viewing;
    /* 0x4100 */ Gfx glist[20000];
    /* 0x2B200 */ LookAt lookAt;
    /* 0x2B220 */ Hilite hilite;
} Dynamic;

/* The object matrices of a frame past the 256 in Dynamic, for each draw buffer. Many players need them: a glove
   takes up to 14 matrices and its shadows more. They are outside the N64's layout, in port_win (gen/ram.S). */
#define OBJECT_MTX_MORE 768
extern Mtx objectMtxMore[2][OBJECT_MTX_MORE];

/* The place of the frame's next object matrix. It takes the next number. */
Mtx *NextObjectMtx(void);

extern PTR32(Gfx) glistp;
extern PTR32(Dynamic) dynamicp;
extern s32 objectMatrix;

#endif
