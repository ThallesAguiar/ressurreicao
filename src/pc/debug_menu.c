// SPDX-License-Identifier: AGPL-3.0-or-later
// In-game debug menu: open/close with R2, navigate with up/down, toggle
// with Cross. Reuses the same flags the --god-mode/--one-hit-kill/--no-clip/
// --infinite-hearts/--give-all-items CLI flags set at startup (src/pc/main.c),
// so either entry point controls the same behavior.
#include <game.h>
#include "pc.h"
#include <psxsdk/libgpu.h>

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
