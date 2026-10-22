// SPDX-License-Identifier: AGPL-3.0-or-later
// Single-byte Latin-1 text, rendered with the stage's existing 4bpp fonts.
#ifndef ST0_PORTUGUESE_FONT_H
#define ST0_PORTUGUESE_FONT_H

static u8 PortugueseBaseLetter(u8 ch) {
    switch (ch) {
    case 0xC9: return 'E';
    case 0xE1: case 0xE3: return 'a';
    case 0xE7: return 'c';
    case 0xE9: case 0xEA: return 'e';
    case 0xED: return 'i';
    case 0xF3: case 0xF5: return 'o';
    case 0xFA: return 'u';
    default: return ch;
    }
}

static int PortugueseGlyphPixel(u16* glyph, int width, int x, int y) {
    return (glyph[y * (width / 4) + x / 4] >> ((x & 3) * 4)) & 15;
}

static void PortugueseGlyphDot(
    u16* glyph, int width, int x, int y, int color) {
    int shift = (x & 3) * 4;
    u16* word = &glyph[y * (width / 4) + x / 4];
    *word = (*word & ~(15 << shift)) | (color << shift);
}

static void AccentPortugueseGlyph(u16* glyph, int width, int height, u8 ch) {
    int left = width, right = 0, top = height, bottom = 0, color = 0;
    int stride = width / 4;
    if (ch == 0xED) {
        // The acute replaces the i's dot. Find the gap before its stem.
        int seenDot = 0;
        for (int y = 0; y < height / 2; y++) {
            int ink = 0;
            for (int x = 0; x < stride; x++) ink |= glyph[y * stride + x];
            if (seenDot && !ink) {
                for (int row = 0; row < y; row++) {
                    for (int x = 0; x < stride; x++) glyph[row * stride + x] = 0;
                }
                break;
            }
            seenDot |= ink != 0;
        }
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int pixel = PortugueseGlyphPixel(glyph, width, x, y);
            if (pixel) {
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
                if (pixel > color) color = pixel;
            }
        }
    }
    if (!color) return;
    int center = (left + right) / 2;
    if (ch == 0xE7) {
        // Reserve two rows for the cedilla without clipping the base c.
        int shift = bottom + 3 > height ? bottom + 3 - height : 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < stride; x++) {
                glyph[y * stride + x] = y + shift < height
                    ? glyph[(y + shift) * stride + x] : 0;
            }
        }
        bottom -= shift;
        PortugueseGlyphDot(glyph, width, center + 1, bottom + 1, color);
        PortugueseGlyphDot(glyph, width, center, bottom + 2, color);
        return;
    }
    // Acute, circumflex and tilde need two rows above the base letter.
    int shift = top < 2 ? 2 - top : 0;
    for (int y = height - 1; y >= 0 && shift; y--) {
        for (int x = 0; x < stride; x++) {
            glyph[y * stride + x] = y >= shift
                ? glyph[(y - shift) * stride + x] : 0;
        }
    }
    top += shift;
    int y = top - 2;
    if (ch == 0xE3 || ch == 0xF5) {
        PortugueseGlyphDot(glyph, width, center - 2, y + 1, color);
        PortugueseGlyphDot(glyph, width, center - 1, y, color);
        PortugueseGlyphDot(glyph, width, center, y, color);
        PortugueseGlyphDot(glyph, width, center + 1, y + 1, color);
        PortugueseGlyphDot(glyph, width, center + 2, y + 1, color);
    } else if (ch == 0xEA) {
        PortugueseGlyphDot(glyph, width, center - 1, y + 1, color);
        PortugueseGlyphDot(glyph, width, center, y, color);
        PortugueseGlyphDot(glyph, width, center + 1, y + 1, color);
    } else {
        PortugueseGlyphDot(glyph, width, center + 1, y, color);
        PortugueseGlyphDot(glyph, width, center, y + 1, color);
    }
}

#endif
