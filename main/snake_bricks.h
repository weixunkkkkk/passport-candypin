#pragma once

#include "snake_model.h"

enum {
    BRICK_CONTACT_MS = 180,
    BRICK_WIDTH = 18,
    BRICK_RUNNER_X = 33,
    BRICK_JUMP_HEIGHT = 20,
    BRICK_RELAY_MORPH_MS = 800,
    BRICK_RELAY_MS = 3600,
    BRICK_REPLAY_TRANSFER_MS = 2200,
    BRICK_REPLAY_MS = 7100
};

typedef enum { BRICK_NONE, BRICK_COIN, BRICK_CRASH } brick_event_t;

typedef struct {
    uint32_t target_at, jump_at, index, feedback_at;
    bool jumping, bumped, passed, crashed;
} brick_game_t;

typedef struct {
    uint32_t at;
    uint8_t from;
    uint8_t to;
    bool pending;
} brick_relay_t;

brick_event_t brick_game_tick(brick_game_t *game, uint32_t time);
void brick_game_jump(brick_game_t *game, uint32_t time);
int brick_game_left(const brick_game_t *game, uint32_t time);
int brick_game_height(const brick_game_t *game, uint32_t time);
bool brick_game_hint(const brick_game_t *game, uint32_t time);
bool brick_game_obstacle(const brick_game_t *game);
bool brick_relay_active(const brick_relay_t *relay, uint32_t now);
bool brick_relay_observe(brick_relay_t *relay, const snake_sample_t *before,
                         const snake_sample_t *after, uint32_t now);
snake_sample_t brick_replay_sample(unsigned step, uint32_t now);
