#pragma once

#include "snake_pixels.h"

typedef enum { DIY_EMPTY, DIY_READY, DIY_BUSY } snake_album_view_t;

void snake_album_init(void);
bool snake_album_save(unsigned slot, const uint8_t *data, size_t size);
unsigned snake_album_mask(void);
uint32_t snake_album_revision(void);
unsigned snake_album_latest(void);
snake_album_view_t snake_album_decode(unsigned slot, unsigned reveal, uint16_t *pixels, unsigned stride);
