// SPDX-License-Identifier: AGPL-3.0-or-later
#include <psyz/module.h>
#include <game.h>
#include <cutscene.h>
#include <string.h>
#include "overlay.h"
#include "../pc.h"
#include "../st/st0/st0.h"

extern Overlay g_Overlay;
extern PfnEntityUpdate EntityUpdates[];
extern LayoutEntity* entityLayoutHorizontal[];
extern LayoutEntity* entityLayoutVertical[];
extern PfnEntityUpdate* PfnEntityUpdates;
extern LayoutEntity** g_pStObjLayoutHorizontal;
extern LayoutEntity** g_pStObjLayoutVertical;

extern u8 gfx_portrait_richter[];
extern u8 gfx_portrait_dracula[];

u8 cutscene_script[] = {
#include "../../st/st0/gen/cutscene_script_psx.h"
};

// The script's SET_EVENTS points at the fixed address 0x80182c4a, which in
// the original binary is right after the 626-byte English dialogue. Keeping
// the events in their own array at that address lets the dialogue text
// change length without the pointer landing in the middle of the text.
static u8 cutscene_events[] = {
#include "../../st/st0/gen/cutscene_events.h"
};

static void InitCutscenePc(void) {
    static const CutsceneSymbolRange symbols[] = {
        {cutscene_script, 0x801829d8, 0x80182c4a - 0x801829d8},
        {cutscene_events, 0x80182c4a, sizeof(cutscene_events)},
        {gfx_portrait_richter, 0x8018f130, 0xd80},
        {gfx_portrait_dracula, 0x8018feb0, 0xd80},
    };
    CutscenePcAlloc(symbols, LEN(symbols));
}

void Psyz_ModuleStart(void* param) {
    Overlay* o = param;

    memcpy(o, &g_Overlay, sizeof(Overlay));
    // The player rescue keeps updating while ordinary stage actors freeze.
    g_JesusBlessingUpdate = EntityUpdates[E_CUTSCENE - 1];
    PfnEntityUpdates = EntityUpdates;
    g_pStObjLayoutHorizontal = entityLayoutHorizontal;
    g_pStObjLayoutVertical = entityLayoutVertical;
    InitCutscenePc();
}

void Psyz_ModuleStop(void) { g_JesusBlessingUpdate = NULL; }
