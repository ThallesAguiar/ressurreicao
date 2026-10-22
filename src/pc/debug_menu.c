// SPDX-License-Identifier: AGPL-3.0-or-later
// In-game debug menu: open/close with R2, navigate with up/down, toggle
// with Cross. Reuses the same flags the --god-mode/--one-hit-kill/--no-clip/
// --infinite-hearts/--give-all-items CLI flags set at startup (src/pc/main.c),
// so either entry point controls the same behavior.
#include <game.h>
#include "pc.h"
#include <psxsdk/libgpu.h>
#include <stdio.h>

// Writes the whole PS1 VRAM (1024x512, 16-bit) to vram_dump.bin and the
// visible entities to entities_dump.txt, for offline palette/texture debugging.
static void DebugDumpVram(void) {
    static u16 vram[1024 * 512];
    RECT rect = {0, 0, 1024, 512};
    FILE* f;
    s32 i;

    DrawSync(0);
    StoreImage(&rect, (u_long*)vram);
    DrawSync(0);
    f = fopen("vram_dump.bin", "wb");
    if (f) {
        fwrite(vram, sizeof(vram), 1, f);
        fclose(f);
    }
    f = fopen("entities_dump.txt", "w");
    if (f) {
        fprintf(f, "stage=%d player=%d\n", g_StageId, g_PlayableCharacter);
        for (i = 0; i < TOTAL_ENTITY_COUNT; i++) {
            Entity* e = &g_Entities[i];
            if (!e->pfnUpdate) {
                continue;
            }
            fprintf(f,
                    "slot=%d id=0x%X params=0x%X x=%d y=%d palette=0x%X "
                    "animSet=0x%X frame=%d unk5A=0x%X blend=0x%X "
                    "drawFlags=0x%X flags=0x%X\n",
                    i, e->entityId, e->params, e->posX.i.hi, e->posY.i.hi,
                    e->palette, (u16)e->animSet, e->animCurFrame, e->unk5A,
                    e->blendMode, e->drawFlags, e->flags);
        }
        fclose(f);
    }
}

typedef struct {
    const char* label;
    s32* flag; // NULL for a one-shot action row
    void (*action)(void);
} DebugMenuRow;

static DebugMenuRow g_DebugMenuRows[] = {
    {"God Mode", &g_GodMode, NULL},
    {"One-Hit-Kill", &g_OneHitKill, NULL},
    {"No-Clip (L2 to fly)", &g_DebugPlayer, NULL},
    {"Infinite Hearts", &g_PcInfiniteHearts, NULL},
    {"Give All Items", NULL, DebugGiveAllItems},
    {"Dump VRAM", NULL, DebugDumpVram},
};
#define NUM_DEBUG_MENU_ROWS \
    ((s32)(sizeof(g_DebugMenuRows) / sizeof(g_DebugMenuRows[0])))

static bool g_DebugMenuOpen = false;
static s32 g_DebugMenuCursor = 0;

void DebugMenu_Update(void) {
    s32 i;

    if (g_pads[0].tapped & PAD_R2) {
        g_DebugMenuOpen = !g_DebugMenuOpen;
    }
    if (!g_DebugMenuOpen) {
        return;
    }

    if (g_pads[0].tapped & PAD_UP) {
        g_DebugMenuCursor =
            (g_DebugMenuCursor + NUM_DEBUG_MENU_ROWS - 1) % NUM_DEBUG_MENU_ROWS;
    }
    if (g_pads[0].tapped & PAD_DOWN) {
        g_DebugMenuCursor = (g_DebugMenuCursor + 1) % NUM_DEBUG_MENU_ROWS;
    }
    if (g_pads[0].tapped & PAD_CROSS) {
        DebugMenuRow* row = &g_DebugMenuRows[g_DebugMenuCursor];
        if (row->flag != NULL) {
            *row->flag = !*row->flag;
        } else if (row->action != NULL) {
            row->action();
        }
    }

    FntPrint("-- debug menu (R2 to close) --\n");
    for (i = 0; i < NUM_DEBUG_MENU_ROWS; i++) {
        DebugMenuRow* row = &g_DebugMenuRows[i];
        const char* cursor = (i == g_DebugMenuCursor) ? "> " : "  ";
        if (row->flag != NULL) {
            FntPrint(
                "%s%s: %s\n", cursor, row->label, *row->flag ? "ON" : "OFF");
        } else {
            FntPrint("%s%s\n", cursor, row->label);
        }
    }
    FntFlush(-1);
}
