#pragma once

#include "snake_art.h"
#include "snake_games.h"

enum {
    ARCADE_SCENE_MS = 11600,
    STACK_FIELD_H = 194, STACK_SIDE_X = 100,
    STACK_PORT_Y = 88, STACK_PORT_STEP = 20, STACK_PANEL_Y = 10,
};

static inline unsigned snake_idle_scene(uint32_t phase) {
    return phase / ARCADE_SCENE_MS % ARCADE_MAZES;
}

void snake_draw_style(snake_style_t style, const snake_model_t *model, uint32_t now,
                      uint32_t phase, snake_rect_fn rect, void *context);
void snake_draw_bricks(const snake_model_t *model, const brick_relay_t *relay,
                       uint32_t now, uint32_t phase, snake_rect_fn rect, void *context);
void snake_draw_play(const arcade_game_t *game, const snake_model_t *model, const brick_relay_t *relay,
                     uint32_t now, snake_rect_fn rect, void *context);
