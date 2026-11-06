#pragma once

#include "snake_bricks.h"
#include "snake_prefs.h"

enum {
    ARCADE_COLS = 17, ARCADE_ROWS = 9, ARCADE_CELLS = ARCADE_COLS * ARCADE_ROWS,
    ARCADE_MAZES = 3,
    STACK_COLS = 10, STACK_ROWS = 20, STACK_TYPES = 7,
    STAR_COLS = 6, STAR_ROWS = 2, STAR_ENEMIES = STAR_COLS * STAR_ROWS,
    STAR_SHOT_Y = 93, STAR_SHIP_MIN_X = 12, STAR_SHIP_MAX_X = 180,
    STAR_ENEMY_LEFT = 5, STAR_ENEMY_STEP = 17, STAR_ENEMY_ROW = 15,
    STAR_OWN_SHOTS = 5, STAR_FOE_SHOTS = 3
};

typedef enum { ARCADE_IDLE, ARCADE_PLAYING, ARCADE_RESULT, ARCADE_PAUSED } arcade_phase_t;
typedef enum { ARCADE_LEFT, ARCADE_RIGHT, ARCADE_OK, ARCADE_PAUSE, ARCADE_DROP } arcade_key_t;
typedef enum { EAST, SOUTH, WEST, NORTH } arcade_direction_t;

typedef struct {
    uint32_t moved_at;
    uint8_t body[ARCADE_CELLS];
    uint8_t head, length, pellet, direction, next_direction;
} arcade_snake_t;

typedef struct {
    uint32_t moved_at, ghost_at, safe_at, power_until, ghost_return_until;
    uint8_t pellets[(ARCADE_CELLS + 7) / 8];
    uint8_t map, player, ghost, remaining, direction, next_direction;
} arcade_maze_t;

typedef struct {
    uint32_t fall_at, lock_at;
    uint16_t shape;
    uint8_t cells[STACK_ROWS][STACK_COLS];
    uint8_t bag[STACK_TYPES];
    uint16_t lines;
    uint8_t bag_left, piece, next;
    int8_t x, y;
    bool grounded;
} arcade_stack_t;

typedef struct { uint8_t x; int8_t y; } arcade_shot_t;

typedef struct {
    uint32_t moved_at, fired_at, safe_at, wave_at;
    uint16_t enemies;
    uint8_t ship_x, fleet_x, fleet_y;
    int8_t direction;
    arcade_shot_t player_shots[STAR_OWN_SHOTS], enemy_shots[STAR_FOE_SHOTS];
} arcade_star_t;

typedef struct {
    uint32_t elapsed, wall_at, ended_at, random, score;
    uint16_t level;
    uint8_t lives;
    arcade_phase_t phase;
    snake_style_t style;
    union {
        arcade_snake_t snake;
        brick_game_t brick;
        arcade_stack_t stack;
        arcade_maze_t maze;
        arcade_star_t star;
    } play;
} arcade_game_t;

void arcade_start(arcade_game_t *game, snake_style_t style, uint32_t now);
bool arcade_update(arcade_game_t *game, uint32_t now);
bool arcade_press(arcade_game_t *game, arcade_key_t key, uint32_t now);
bool arcade_maze_wall(unsigned map, unsigned cell);
unsigned arcade_maze_patrol_length(unsigned map);
unsigned arcade_maze_patrol(unsigned map, uint32_t step);
bool arcade_maze_pellet(const arcade_maze_t *maze, unsigned cell);
bool arcade_maze_power(unsigned map, unsigned cell);
bool arcade_maze_powered(const arcade_game_t *game);
uint16_t arcade_stack_shape(unsigned piece);
int arcade_stack_landing(const arcade_stack_t *stack);
int arcade_star_x(const arcade_star_t *star, unsigned index);
int arcade_star_y(const arcade_star_t *star, unsigned index);
