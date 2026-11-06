#pragma once

#include "snake_model.h"

enum { SNAKE_BOARD_W = 192, SNAKE_BOARD_H = 120 };
typedef void (*snake_rect_fn)(void *context, uint32_t rgb, int x, int y, int w, int h);

extern const uint32_t snake_port_colors[SNAKE_PORTS];
void snake_draw_game(const snake_model_t *model, uint32_t now, snake_rect_fn rect, void *context);
void snake_draw_watts(const snake_model_t *model, uint32_t now, snake_rect_fn rect, void *context);
