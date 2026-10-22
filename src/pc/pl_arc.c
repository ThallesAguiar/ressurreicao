// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <psyz/log.h>
#include "pc.h"
#include <stdlib.h>
#include <string.h>

static const int BaseAddr = 0x8013C000;
u8* g_PlOvlSpritesheet[256];
u8** g_PlOvlAluBatSpritesheet[1];
static u8* g_batSpritesheetData[256];
static u8* sprite_data = NULL;

#include "gen/jesus_ferido.h"
static u16 s_JesusFeridoPalette[] = {
#include "gen/jesus_ferido_palette.h"
};
STATIC_ASSERT(LEN(s_JesusFeridoPalette) == COLORS_PER_PAL);
STATIC_ASSERT(LEN(jesus_ferido_sprites) <= LEN(g_PlOvlSpritesheet));

extern s16* spritebank2[];

// The 27 whip-arm poses are ordinary tiles, not part of the 197 compressed
// body frames. Their atlas is F_GAME2 block 6 (VRAM 0x240, 0x180). Translate
// only their texels to the Jesus palette; other weapon tiles keep their colors.
void PatchPlayerRicArmTiles(u8* data, size_t length) {
    static const u8 colors[16] = {
        0, 1, 11, 10, 9, 7, 7, 11, 3, 4, 5, 6, 7, 4, 9, 13};
    const size_t blockOffset = 6 * 0x2000;
    u8 touched[0x2000] = {0};

    if (length < blockOffset + sizeof(touched)) {
        return;
    }
    for (int frame = 1; frame <= 27; frame++) {
        const s16* part = spritebank2[frame] + 1;
        for (int i = 0; i < spritebank2[frame][0]; i++, part += 11) {
            for (int y = part[8]; y < part[10]; y++) {
                for (int x = part[7] / 2; x < part[9] / 2; x++) {
                    size_t offset = y * 64 + x;
                    if (!touched[offset]) {
                        u8 value = data[blockOffset + offset];
                        data[blockOffset + offset] =
                            colors[value & 15] | (colors[value >> 4] << 4);
                        touched[offset] = 1;
                    }
                }
            }
        }
    }
}

// PAL_RICHTER is in the F_GAME2 palette bank (VRAM x=0, y=0xF2).
// Keep the RAM copy in sync so palette fades restore the same colors.
void LoadPlayerRicPalette(void) {
    RECT rect = {0, 0xF2, COLORS_PER_PAL, 1};
    if (g_StageId != STAGE_ST0 && g_PlayableCharacter != PLAYER_RICHTER) {
        return;
    }
    memcpy(&g_Clut[1][(PAL_RICHTER - 0x100) * COLORS_PER_PAL],
           s_JesusFeridoPalette, sizeof(s_JesusFeridoPalette));
    LoadImage(&rect, (u_long*)s_JesusFeridoPalette);
}

// Reads a table of PSX pointers starting at byte offset `tableOffset` and
// rebases each one into `sprite_data`, stopping at the first invalid entry.
static void ParseSpritesheetTable(
    size_t tableOffset, u32 fileLength, u8** dst, int dstLen) {
    u32* ptr = (u32*)(sprite_data + tableOffset);
    for (int i = 0; i < dstLen; i++) {
        u32 psxPtr = *ptr++;
        if (psxPtr < BaseAddr) { // validate PTR correctness
            break;
        }
        size_t dataPos = psxPtr - BaseAddr;
        if (dataPos >= fileLength) { // validate data correctness
            break;
        }
        dst[i] = sprite_data + dataPos;
    }
}

void InitPlayerArc(const struct FileUseContent* file) {
    if (sprite_data) {
        free(sprite_data);
    }
    // this is not freed anywhere else, technically a memory leak but it's not
    DEBUGF("allocating %d bytes for ARC spritesheet", file->length);
    sprite_data = malloc(file->length);
    memcpy(sprite_data, file->content, file->length);

    // clean-up previously allocated data, only useful for memory sanity checks
    memset(g_PlOvlSpritesheet, 0, sizeof(g_PlOvlSpritesheet));
    memset(g_batSpritesheetData, 0, sizeof(g_batSpritesheetData));

    u32* header = (u32*)sprite_data;
    u32 batTablePtr = header[1];
    if (batTablePtr >= BaseAddr && batTablePtr - BaseAddr < file->length) {
        ParseSpritesheetTable(batTablePtr - BaseAddr, file->length,
                              g_batSpritesheetData, LEN(g_batSpritesheetData));
    }
    g_PlOvlAluBatSpritesheet[0] = g_batSpritesheetData;

    ParseSpritesheetTable(8 * sizeof(u32), file->length, g_PlOvlSpritesheet,
                          LEN(g_PlOvlSpritesheet));

    if (strcmp(file->filename, "disks/us/BIN/RIC.BIN") == 0) {
        memcpy(g_PlOvlSpritesheet, jesus_ferido_sprites,
               sizeof(jesus_ferido_sprites));
        INFOF("loaded %d Jesus ferido sprites", LEN(jesus_ferido_sprites));
    }
}
