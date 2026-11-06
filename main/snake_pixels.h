#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DIY_COLS = 48, DIY_ROWS = 30, DIY_COLORS = 32, DIY_SLOTS = 3,
    DIY_HEADER = 4, DIY_INDEX = DIY_HEADER + DIY_COLORS * 2,
    DIY_FRAME_BYTES = DIY_INDEX + DIY_COLS * DIY_ROWS,
    DIY_TILE = 24, DIY_TILE_COLS = 8, DIY_TILE_ROWS = 5,
    DIY_TILES = DIY_TILE_COLS * DIY_TILE_ROWS
};

bool snake_pixels_valid(const uint8_t *data, size_t size);
void snake_pixels_decode(const uint8_t *data, unsigned slot, unsigned reveal, uint16_t *pixels, unsigned stride);
unsigned snake_pixels_next(unsigned current, unsigned mask);
unsigned snake_pixels_tile_order(unsigned tile, unsigned slot);
