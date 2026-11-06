#include "snake_pixels.h"

#include <string.h>

bool snake_pixels_valid(const uint8_t *data, size_t size) {
    if (!data || size != DIY_FRAME_BYTES || memcmp(data, "PX01", DIY_HEADER)) return false;
    for (size_t i = DIY_INDEX; i < size; ++i)
        if (data[i] >= DIY_COLORS) return false;
    return true;
}

void snake_pixels_decode(const uint8_t *data, unsigned slot, unsigned reveal, uint16_t *pixels, unsigned stride) {
    enum { CELL = DIY_COLS / DIY_TILE_COLS };
    for (unsigned tile = 0; tile < DIY_TILES; ++tile) {
        unsigned x = tile % DIY_TILE_COLS * CELL, y = tile / DIY_TILE_COLS * CELL;
        bool show = snake_pixels_tile_order(tile, slot) < reveal;
        for (unsigned row = 0; row < CELL; ++row) {
            for (unsigned col = 0; col < CELL; ++col) {
                uint16_t *pixel = &pixels[(y + row) * stride + x + col];
                if (show) {
                    unsigned i = (y + row) * DIY_COLS + x + col;
                    unsigned pos = DIY_HEADER + 2u * data[DIY_INDEX + i];
                    *pixel = (uint16_t)(data[pos] | (uint16_t)data[pos + 1] << 8);
                } else {
                    bool edge = row == 0 || col == 0 || row == CELL - 1 || col == CELL - 1;
                    *pixel = edge ? 0x2905 : 0x4188;
                }
            }
        }
    }
}

unsigned snake_pixels_next(unsigned current, unsigned mask) {
    for (unsigned i = 1; i <= DIY_SLOTS; ++i) {
        unsigned slot = (current + i) % DIY_SLOTS;
        if (mask & (1u << slot)) return slot;
    }
    return 0;
}

unsigned snake_pixels_tile_order(unsigned tile, unsigned slot) {
    return (tile * 13u + slot * 7u) % DIY_TILES;
}
