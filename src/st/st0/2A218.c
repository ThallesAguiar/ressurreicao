// SPDX-License-Identifier: AGPL-3.0-or-later
#include "st0.h"
#ifdef VERSION_PC
#include <string.h>

extern s16* sprites_st0_4[];

static void PrepareJesusDialogueSprites(void) {
    static s16 poses[14][24];
    static bool prepared;
    // Player animation frame 7 uses body image 6. Keep its exact stance
    // when the dialogue switches from the player to the gesture actor.
    u8* idle = g_PlOvlSpritesheet[6];
    RECT rect = {65, 448, idle[0] / 4, idle[1]};
    static const s16 legs[] = {
        2, -13, -1, 24, 26, 0, 0, 4, 83, 28, 109};
    // The native 2-frame transitions add a separate Richter arm to poses
    // whose Jesus artwork already contains both arms. The native 1/10
    // speaking loop also switches between unrelated neutral body images.
    // Keep one neutral torso and use complete gesture images for returns;
    // retain the original animation timing and pointing/open-arm poses.
    static const u8 sourcePose[] = {
        0, 2, 2, 2, 2, 2, 2, 7, 8, 9, 2, 11, 12, 9};
    // Align the robe at the waist seam, rather than centering each crop.
    // The body axis moves inside the atlas as the arms change position.
    static const s16 torsoX[] = {
        0, 0, -15, -15, -15, -15, -15, -13, -12, -12, -11, -15, -13, -11};

    // The gesture atlas occupies (64,384), with unused rows below y=64.
    // Leave one VRAM word (four texels) before the mirrored image. The
    // renderer subtracts one from its left UV; zero would wrap to 255.
    // Sprite flag 2 mirrors the existing idle image toward Morte.
    LoadImage(&rect, (u_long*)(idle + 4));
    if (prepared) {
        return;
    }
    for (int frame = 1; frame <= 13; frame++) {
        s16* pose = poses[frame];
        s16* torso = pose + 12;
        int source = sourcePose[frame];
        const s16* original = sprites_st0_4[source];
        memcpy(pose, original, (1 + original[0] * 11) * sizeof(s16));
        memcpy(pose + 1, legs, sizeof(legs));
        // Each torso previously included a second skirt/hip section over
        // the shared legs. Stop at the waist and use one stable body axis.
        int dx = torsoX[source] - torso[1];
        int dy = ((torso[0] & 32) ? -21 : -20) - torso[2];
        torso[1] += dx;
        torso[2] += dy;
        torso[4] = (torso[0] & 32) ? 20 : 19;
        torso[10] = torso[8] + torso[4];
    }
    for (int frame = 1; frame <= 13; frame++) {
        sprites_st0_4[frame] = poses[frame];
    }
    prepared = true;
}
#endif

static void func_801AA218(s16 arg0) {
    s16 temp_a1 = ((0xE0 - arg0) / 2) + 0x80;
    s16 temp_v1 = temp_a1 - g_unkGraphicsStruct.unk14;

    if (temp_v1 > 2) {
        g_unkGraphicsStruct.unk14 += 2;
    } else if (temp_v1 < -2) {
        g_unkGraphicsStruct.unk14 -= 2;
    } else {
        g_unkGraphicsStruct.unk14 = temp_a1;
    }
}

extern s32 g_SkipCutscene;
#ifdef VERSION_PSP
extern u32 g_CutsceneFlags;
extern s32 g_IsCutsceneDone;
#else
// These two variables are initialized in cutscene.c, but defined here because
// there is an unrelated variable stuck in between them and the rest of the
// cutscene bss variables for the us version
u32 g_CutsceneFlags;
s32 g_IsCutsceneDone;
#endif

static u8 D_80180830[] = {0x08, 0x05, 0x08, 0x06, 0x08, 0x07, 0xFF, 0x00};

// This appears to be an unused animation and deadstripped on PSP
static u8 unused[] = {0x20, 0x01, 0xFF, 0x00};

static u8 D_8018083C[] = {0x02, 0x02, 0x02, 0x03, 0x02, 0x04, 0x02, 0x05,
                          0x02, 0x06, 0x02, 0x05, 0xFF, 0x00, 0x00, 0x00};
static u8 D_8018084C[] = {0x02, 0x04, 0x02, 0x03, 0x20, 0x02, 0x03, 0x0D,
                          0x03, 0x09, 0x02, 0x08, 0x02, 0x07, 0xFF, 0x00};
static u8 D_8018085C[] = {0x02, 0x08, 0x02, 0x09, 0xFF, 0x00, 0x00, 0x00};
static u8 D_80180864[] = {0x06, 0x0D, 0x06, 0x0A, 0x02, 0x01, 0xFF, 0x00};
static u8 D_8018086C[] = {0x06, 0x0A, 0x06, 0x01, 0x08, 0x0A, 0x08, 0x01,
                          0x08, 0x0A, 0x08, 0x01, 0x08, 0x0A, 0xFF, 0x00};
static u8 D_8018087C[] = {0x06, 0x0D, 0x06, 0x09, 0xFF, 0x00, 0x00, 0x00};
static u8 D_80180884[] = {0x02, 0x08, 0x02, 0x07, 0xFF, 0x00, 0x00, 0x00};
static u8 D_8018088C[] = {0x06, 0x0C, 0x06, 0x0B, 0xFF, 0x00, 0x00, 0x00};
static u8 D_80180894[] = {0x06, 0x0C, 0x30, 0x0A, 0x10, 0x01, 0xFF, 0x00};

void EntityCutsceneSupport(Entity* self) {
    Tilemap* tilemap = &g_Tilemap;
    Entity* player = &PLAYER;
    u16 posX;

    self->posX.i.hi = player->posX.i.hi;
    self->posY.i.hi = player->posY.i.hi - 1;

    if ((self->step != 14) && g_SkipCutscene && g_IsCutsceneDone &&
        (self->step > 4)) {
        self->step = 15;
        self->animSet = ANIMSET_DRA(0);
        self->animCurFrame = 0;
    }

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitCutscene);
        self->unk5A = 0x46;
        self->palette = PAL_RICHTER;
        break;

    case 1:
        if (self->step_s) {
            if ((player->step <= 2) || (player->step == 25)) {
                posX = player->posX.i.hi + tilemap->scrollX.i.hi;
                if (posX > 0x8000) {
                    posX = 0;
                }
                if (posX > 0xA0) {
                    g_Player.padSim = PAD_LEFT;
                } else {
                    g_Player.padSim = PAD_RIGHT;
                }
                g_Entities[E_AFTERIMAGE_1].ext.afterImage.disableFlag = 1;
                g_Player.demo_timer = 255;
                func_801AA218(posX);
                SetStep(2);
            }
        } else {
            if ((player->posX.i.hi + tilemap->scrollX.i.hi) < 0xE1) {
                g_PauseAllowed = false;
                self->step_s++;
            }
        }
        break;

    case 2:
        posX = player->posX.i.hi + tilemap->scrollX.i.hi;
        if (posX > 0x8000) {
            posX = 0;
        }

        if ((g_Player.padSim == PAD_LEFT) && (posX <= 0xA0) ||
            (g_Player.padSim == PAD_RIGHT) && (posX >= 0x9F)) {
            g_Player.demo_timer = 1;
            g_Player.padSim = PAD_LEFT;
            self->ext.utimer.t = 0x18;
            g_CutsceneFlags |= 1;
            self->step++;
        }
        func_801AA218(posX);
        break;

    case 3:
        posX = player->posX.i.hi + tilemap->scrollX.i.hi;
        if (posX > 0x8000) {
            posX = 0;
        }
        func_801AA218(posX);
        g_Player.demo_timer = 1;
        g_Player.padSim = 0;
        if (!--self->ext.utimer.t) {
            SetStep(4);
        }
        break;

    case 4:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_80180830, self) == 0) {
            self->step++;
            g_Tilemap.width = 0x100;
        }
        player->animCurFrame = self->animCurFrame;
        break;

    case 5:
        g_Player.demo_timer = 1;
        player->animCurFrame = self->animCurFrame;
        if (g_CutsceneFlags & 0x400) {
#ifdef VERSION_PC
            PrepareJesusDialogueSprites();
#endif
            player->animCurFrame = 0;
            self->animSet = ANIMSET_OVL(5);
            self->animCurFrame = 1;
            SetStep(6);
        }
        break;

    case 6:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018083C, self) == 0) {
            if (g_CutsceneFlags & 0x800) {
                SetStep(7);
            }
        }
        player->animCurFrame = 0;
        break;

    case 7:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018084C, self) == 0) {
            if (g_CutsceneFlags & 0x1000) {
                SetStep(8);
            }
        }
        player->animCurFrame = 0;
        break;

    case 8:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018085C, self) == 0) {
            if (g_CutsceneFlags & 0x2000) {
                SetStep(9);
            }
        }
        player->animCurFrame = 0;
        break;

    case 9:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_80180864, self) == 0) {
            if (g_CutsceneFlags & 0x4000) {
                SetStep(10);
            }
        }
        player->animCurFrame = 0;
        break;

    case 10:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018086C, self) == 0) {
            if (g_CutsceneFlags & 0x8000) {
                SetStep(11);
            }
        }
        player->animCurFrame = 0;
        break;

    case 11:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018087C, self) == 0) {
            if (g_CutsceneFlags & 0x10000) {
                SetStep(12);
            }
        }
        player->animCurFrame = 0;
        break;

    case 12:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_80180884, self) == 0) {
            if (g_CutsceneFlags & 0x20000) {
                SetStep(13);
            }
        }
        player->animCurFrame = 0;
        break;

    case 13:
        g_Player.demo_timer = 1;
        if (AnimateEntity(D_8018088C, self) == 0) {
            if (g_CutsceneFlags & 0x40000) {
                SetStep(14);
            }
        }
        player->animCurFrame = 0;
        break;

    case 14:
        g_Player.demo_timer = 1;
        player->animCurFrame = 0;
        if (AnimateEntity(D_80180894, self) == 0) {
            self->animCurFrame = 0;
            player->animCurFrame = 7;
            SetStep(15);
        }
        break;

    case 15:
        g_Player.demo_timer = 1;
        player->animCurFrame = 7;
        if (g_CutsceneFlags & 4) {
            g_Player.padSim = PAD_LEFT;
            DestroyEntity(self);
            g_PauseAllowed = true;
        }
        break;
    }
}
