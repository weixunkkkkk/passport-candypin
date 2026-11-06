#include "snake_games.h"

#include <stdlib.h>
#include <string.h>

enum { NO_CELL = UINT8_MAX, HIT_GRACE_MS = 1600 };

static const int8_t dx[] = {1, 0, -1, 0};
static const int8_t dy[] = {0, 1, 0, -1};

static uint8_t neighbor(unsigned cell, unsigned direction) {
    int x = (int)(cell % ARCADE_COLS) + dx[direction];
    int y = (int)(cell / ARCADE_COLS) + dy[direction];
    if (x < 0 || x >= ARCADE_COLS || y < 0 || y >= ARCADE_ROWS) return NO_CELL;
    return (uint8_t)(y * ARCADE_COLS + x);
}

static uint8_t turn(unsigned direction, arcade_key_t key) {
    return (uint8_t)((direction + (key == ARCADE_LEFT ? 3u : 1u)) % 4);
}

static unsigned interval(unsigned start, unsigned floor, unsigned level, unsigned step) {
    unsigned reduction = (level - 1) * step;
    return reduction >= start - floor ? floor : start - reduction;
}

static uint32_t random_value(arcade_game_t *game) {
    uint32_t value = game->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return game->random = value;
}

static void level_up(arcade_game_t *game) {
    if (game->level < 999) ++game->level;
}

static void lose_life(arcade_game_t *game) {
    if (--game->lives == 0) game->phase = ARCADE_RESULT;
}

static unsigned body_slot(const arcade_snake_t *snake, unsigned distance) {
    return (snake->head + ARCADE_CELLS - distance) % ARCADE_CELLS;
}

static void next_pellet(arcade_game_t *game) {
    arcade_snake_t *snake = &game->play.snake;
    bool occupied[ARCADE_CELLS] = {0};
    for (unsigned i = 0; i < snake->length; ++i)
        occupied[snake->body[body_slot(snake, i)]] = true;
    unsigned skip = random_value(game) % (ARCADE_CELLS - snake->length);
    for (unsigned i = 0; i < ARCADE_CELLS; ++i) {
        if (!occupied[i] && skip-- == 0) {
            snake->pellet = (uint8_t)i;
            return;
        }
    }
}

static bool update_snake(arcade_game_t *game) {
    arcade_snake_t *snake = &game->play.snake;
    if (game->elapsed - snake->moved_at < interval(410, 150, game->level, 28)) return false;
    snake->moved_at = game->elapsed;
    snake->direction = snake->next_direction;
    unsigned next = neighbor(snake->body[snake->head], snake->direction);
    bool grow = next == snake->pellet;
    bool hit = next == NO_CELL;
    unsigned check = snake->length - (grow ? 0u : 1u);
    for (unsigned i = 0; !hit && i < check; ++i)
        hit = snake->body[body_slot(snake, i)] == next;
    if (hit) {
        lose_life(game);
        return true;
    }
    snake->head = (uint8_t)((snake->head + 1u) % ARCADE_CELLS);
    snake->body[snake->head] = (uint8_t)next;
    if (grow) {
        ++snake->length;
        if (++game->score % 5 == 0) level_up(game);
        if (snake->length == ARCADE_CELLS) game->phase = ARCADE_RESULT;
        else next_pellet(game);
    }
    return true;
}

static bool update_bricks(arcade_game_t *game) {
    brick_game_t *brick = &game->play.brick;
    uint32_t before = brick->index;
    brick_event_t event = brick_game_tick(brick, game->elapsed);
    if (event == BRICK_COIN) ++game->score;
    if (event == BRICK_CRASH) lose_life(game);
    if (before != brick->index && brick->index % 6 == 0) level_up(game);
    return true;
}

typedef struct {
    const char *row[ARCADE_ROWS];
    const char *patrol;
} maze_layout_t;

static const maze_layout_t maze_maps[ARCADE_MAZES] = {
    {{
        "#################",
        "#.......#......o#",
        "#.###.#.#.#.###.#",
        "#.....#...#.....#",
        "#.#.###.#.###.#.#",
        "#.#.....#.....#.#",
        "#.###.#...#.###.#",
        "#o..............#",
        "#################",
    }, "EEEEEE" "SS" "EE" "SS" "EEEE" "NN" "EE" "SSSS" "WWWWWWWWWWWWWW" "NNNNNN"},
    {{
        "#################",
        "#......o........#",
        "#.###.#####.###.#",
        "#...#.......#...#",
        "###.#.#####.#.###",
        "#...#.#...#.#...#",
        "#.#...#.#.#...#.#",
        "#o..............#",
        "#################",
    }, "EEEEEEEEEEEEEE" "SS" "WW" "SS" "EE" "SS" "WWWWWWWWWWWW" "NNNN" "WW" "NN"},
    {{
        "#################",
        "#....o..#.......#",
        "#.###.#.###.###.#",
        "#.....#...#.....#",
        "#.###.###.#.###.#",
        "#.............#.#",
        "#.###.#######.#.#",
        "#o..............#",
        "#################",
    }, "EEEEEE" "SS" "EE" "SS" "EE" "NNNN" "EEEE" "SSSSSS" "WWWWWWWWWWWWWW" "NNNNNN"},
};

enum { MAZE_START = ARCADE_COLS + 1, GHOST_HOME = 7 * ARCADE_COLS + 15 };

bool arcade_maze_wall(unsigned map, unsigned cell) {
    return cell >= ARCADE_CELLS || maze_maps[map].row[cell / ARCADE_COLS][cell % ARCADE_COLS] == '#';
}

bool arcade_maze_power(unsigned map, unsigned cell) {
    return cell < ARCADE_CELLS && maze_maps[map].row[cell / ARCADE_COLS][cell % ARCADE_COLS] == 'o';
}

unsigned arcade_maze_patrol_length(unsigned map) {
    return (unsigned)strlen(maze_maps[map].patrol);
}

unsigned arcade_maze_patrol(unsigned map, uint32_t step) {
    const char *patrol = maze_maps[map].patrol;
    uint8_t cell = MAZE_START;
    for (unsigned i = 0, end = step % arcade_maze_patrol_length(map); i < end; ++i) {
        unsigned direction = patrol[i] == 'E' ? EAST : patrol[i] == 'S' ? SOUTH :
                             patrol[i] == 'W' ? WEST : NORTH;
        cell = neighbor(cell, direction);
    }
    return cell;
}

bool arcade_maze_pellet(const arcade_maze_t *maze, unsigned cell) {
    return cell < ARCADE_CELLS && (maze->pellets[cell / 8] & (1u << (cell % 8)));
}

bool arcade_maze_powered(const arcade_game_t *game) {
    return game->elapsed < game->play.maze.power_until;
}

static void reset_maze_positions(arcade_game_t *game) {
    arcade_maze_t *maze = &game->play.maze;
    maze->player = MAZE_START;
    maze->ghost = GHOST_HOME;
    maze->direction = EAST;
    maze->next_direction = EAST;
    maze->moved_at = game->elapsed;
    maze->ghost_at = game->elapsed;
    maze->safe_at = game->elapsed;
    maze->power_until = 0;
    maze->ghost_return_until = 0;
}

static void fill_maze(arcade_game_t *game, unsigned map) {
    arcade_maze_t *maze = &game->play.maze;
    maze->map = (uint8_t)map;
    reset_maze_positions(game);
    maze->remaining = 0;
    memset(maze->pellets, 0, sizeof maze->pellets);
    for (unsigned cell = 0; cell < ARCADE_CELLS; ++cell) {
        if (cell == MAZE_START || arcade_maze_wall(maze->map, cell)) continue;
        maze->pellets[cell / 8] |= 1u << (cell % 8);
        ++maze->remaining;
    }
}

static void move_ghost(arcade_game_t *game) {
    arcade_maze_t *maze = &game->play.maze;
    uint8_t distance[ARCADE_CELLS], queue[ARCADE_CELLS];
    memset(distance, NO_CELL, sizeof distance);
    distance[maze->player] = 0;
    queue[0] = maze->player;
    unsigned count = 1;
    for (unsigned i = 0; i < count; ++i) {
        unsigned cell = queue[i];
        for (unsigned direction = 0; direction < 4; ++direction) {
            uint8_t next = neighbor(cell, direction);
            if (arcade_maze_wall(maze->map, next) || distance[next] != NO_CELL) continue;
            distance[next] = (uint8_t)(distance[cell] + 1u);
            queue[count++] = next;
        }
    }

    bool flee = arcade_maze_powered(game);
    uint8_t choice = maze->ghost;
    int best = flee ? -1 : NO_CELL;
    for (unsigned direction = 0; direction < 4; ++direction) {
        uint8_t next = neighbor(maze->ghost, direction);
        if (arcade_maze_wall(maze->map, next) || distance[next] == NO_CELL) continue;
        int steps = distance[next];
        if (flee ? steps > best : steps < best) {
            best = steps;
            choice = next;
        }
    }
    maze->ghost = choice;
}

static bool maze_contact(arcade_game_t *game) {
    arcade_maze_t *maze = &game->play.maze;
    if (maze->player != maze->ghost || game->elapsed < maze->ghost_return_until) return false;
    if (arcade_maze_powered(game)) {
        game->score += 25;
        maze->ghost = GHOST_HOME;
        maze->ghost_return_until = game->elapsed + 900u;
    } else {
        if (game->elapsed - maze->safe_at < HIT_GRACE_MS) return false;
        lose_life(game);
        if (game->phase == ARCADE_PLAYING) reset_maze_positions(game);
    }
    return true;
}

static bool update_maze(arcade_game_t *game) {
    arcade_maze_t *maze = &game->play.maze;
    uint32_t time = game->elapsed;
    bool moved = false;
    if (time - maze->moved_at >= interval(290, 180, game->level, 12)) {
        maze->moved_at = time;
        uint8_t next = neighbor(maze->player, maze->next_direction);
        if (!arcade_maze_wall(maze->map, next)) maze->direction = maze->next_direction;
        else next = neighbor(maze->player, maze->direction);
        if (!arcade_maze_wall(maze->map, next)) {
            moved = true;
            maze->player = next;
            if (arcade_maze_pellet(maze, next)) {
                maze->pellets[next / 8] &= (uint8_t)~(1u << (next % 8));
                --maze->remaining;
                ++game->score;
                if (arcade_maze_power(maze->map, next)) maze->power_until = time + 5000u;
            }
        }
        if (!maze->remaining) {
            game->score += 50;
            level_up(game);
            fill_maze(game, (maze->map + 1u) % ARCADE_MAZES);
            return true;
        }
        if (maze_contact(game)) return true;
    }
    if (time >= maze->ghost_return_until && time - maze->ghost_at >= interval(670, 275, game->level, 40)) {
        maze->ghost_at = time;
        move_ghost(game);
        maze_contact(game);
        moved = true;
    }
    return moved;
}

uint16_t arcade_stack_shape(unsigned piece) {
    static const uint16_t shapes[STACK_TYPES] = {0x00f0, 0x0066, 0x0072, 0x0036, 0x0063, 0x0071, 0x0074};
    return shapes[piece];
}

static uint8_t stack_take(arcade_game_t *game) {
    arcade_stack_t *stack = &game->play.stack;
    if (!stack->bag_left) {
        for (unsigned i = 0; i < STACK_TYPES; ++i) stack->bag[i] = (uint8_t)i;
        for (unsigned i = STACK_TYPES - 1; i > 0; --i) {
            unsigned j = random_value(game) % (i + 1);
            uint8_t swap = stack->bag[i];
            stack->bag[i] = stack->bag[j];
            stack->bag[j] = swap;
        }
        stack->bag_left = STACK_TYPES;
    }
    return stack->bag[--stack->bag_left];
}

static bool stack_fits(const arcade_stack_t *stack, uint16_t shape, int x, int y) {
    for (int i = 0; i < 16; ++i) {
        if (!(shape & (1u << i))) continue;
        int col = x + i % 4, row = y + i / 4;
        if (col < 0 || col >= STACK_COLS || row < 0 || row >= STACK_ROWS || stack->cells[row][col])
            return false;
    }
    return true;
}

int arcade_stack_landing(const arcade_stack_t *stack) {
    int y = stack->y;
    while (stack_fits(stack, stack->shape, stack->x, y + 1)) ++y;
    return y;
}

static void stack_spawn(arcade_game_t *game) {
    arcade_stack_t *stack = &game->play.stack;
    stack->piece = stack->next;
    stack->next = stack_take(game);
    stack->shape = arcade_stack_shape(stack->piece);
    stack->x = 3;
    stack->y = 0;
    stack->grounded = false;
    stack->fall_at = game->elapsed;
    if (!stack_fits(stack, stack->shape, stack->x, stack->y)) game->phase = ARCADE_RESULT;
}

static void stack_lock(arcade_game_t *game) {
    arcade_stack_t *stack = &game->play.stack;
    for (int i = 0; i < 16; ++i) {
        if (stack->shape & (1u << i))
            stack->cells[stack->y + i / 4][stack->x + i % 4] = stack->piece + 1u;
    }
    int destination = STACK_ROWS - 1;
    for (int row = destination; row >= 0; --row) {
        unsigned occupied = 0;
        for (unsigned col = 0; col < STACK_COLS; ++col) occupied += stack->cells[row][col] != 0;
        if (occupied == STACK_COLS) continue;
        if (destination != row) memcpy(stack->cells[destination], stack->cells[row], STACK_COLS);
        --destination;
    }
    unsigned cleared = (unsigned)(destination + 1);
    static const uint16_t points[] = {0, 100, 300, 500, 800};
    memset(stack->cells, 0, cleared * STACK_COLS);
    game->score += points[cleared] * game->level;
    stack->lines += (uint16_t)cleared;
    if (stack->lines / 5u >= game->level) level_up(game);
    stack_spawn(game);
}

static bool update_stack(arcade_game_t *game) {
    arcade_stack_t *stack = &game->play.stack;
    uint32_t time = game->elapsed;
    if (!stack_fits(stack, stack->shape, stack->x, stack->y + 1)) {
        if (!stack->grounded) {
            stack->grounded = true;
            stack->lock_at = time;
        } else if (time - stack->lock_at >= 450) {
            stack_lock(game);
            return true;
        }
    } else {
        stack->grounded = false;
        if (time - stack->fall_at >= interval(760, 135, game->level, 62)) {
            ++stack->y;
            stack->fall_at = time;
            return true;
        }
    }
    return false;
}

static bool stack_press(arcade_game_t *game, arcade_key_t key) {
    arcade_stack_t *stack = &game->play.stack;
    if (key == ARCADE_DROP) {
        int landing = arcade_stack_landing(stack);
        game->score += (unsigned)(landing - stack->y);
        stack->y = (int8_t)landing;
        stack_lock(game);
        return true;
    }
    if (key == ARCADE_LEFT || key == ARCADE_RIGHT) {
        int x = stack->x + (key == ARCADE_LEFT ? -1 : 1);
        if (!stack_fits(stack, stack->shape, x, stack->y)) return false;
        stack->x = (int8_t)x;
        return true;
    }
    if (key != ARCADE_OK || stack->piece == 1) return false;
    uint16_t rotated = 0;
    int edge = stack->piece == 0 ? 3 : 2;
    for (int i = 0; i < 16; ++i) {
        if (stack->shape & (1u << i)) rotated |= 1u << (i % 4 * 4 + edge - i / 4);
    }
    static const int8_t kicks[][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1}, {0, -2}};
    for (unsigned i = 0; i < sizeof kicks / sizeof kicks[0]; ++i) {
        int x = stack->x + kicks[i][0], y = stack->y + kicks[i][1];
        if (!stack_fits(stack, rotated, x, y)) continue;
        stack->shape = rotated;
        stack->x = (int8_t)x;
        stack->y = (int8_t)y;
        return true;
    }
    return false;
}

int arcade_star_x(const arcade_star_t *star, unsigned index) {
    return star->fleet_x + STAR_ENEMY_LEFT + (int)(index % STAR_COLS) * STAR_ENEMY_STEP;
}

int arcade_star_y(const arcade_star_t *star, unsigned index) {
    return star->fleet_y + (int)(index / STAR_COLS) * STAR_ENEMY_ROW;
}

static void star_formation(arcade_game_t *game) {
    arcade_star_t *star = &game->play.star;
    star->fleet_x = 14;
    star->fleet_y = 28;
    star->direction = 1;
    star->moved_at = game->elapsed;
    star->fired_at = game->elapsed;
    star->safe_at = game->elapsed;
    memset(star->player_shots, 0, sizeof star->player_shots);
    memset(star->enemy_shots, 0, sizeof star->enemy_shots);
}

static void star_spawn(arcade_game_t *game) {
    game->play.star.enemies = (1u << STAR_ENEMIES) - 1u;
    star_formation(game);
}

static bool shoot(arcade_shot_t *shots, unsigned count, int x, int y) {
    for (unsigned i = 0; i < count; ++i) {
        if (shots[i].y) continue;
        shots[i] = (arcade_shot_t){(uint8_t)x, (int8_t)y};
        return true;
    }
    return false;
}

static void fleet_fire(arcade_game_t *game) {
    arcade_star_t *star = &game->play.star;
    unsigned start = random_value(game) % STAR_COLS;
    for (unsigned i = 0; i < STAR_COLS; ++i) {
        unsigned column = (start + i) % STAR_COLS;
        for (int row = STAR_ROWS - 1; row >= 0; --row) {
            unsigned enemy = (unsigned)row * STAR_COLS + column;
            if (star->enemies & (1u << enemy)) {
                shoot(star->enemy_shots, STAR_FOE_SHOTS, arcade_star_x(star, enemy), arcade_star_y(star, enemy) + 7);
                return;
            }
        }
    }
}

static void update_lasers(arcade_game_t *game) {
    arcade_star_t *star = &game->play.star;
    for (unsigned i = 0; i < STAR_OWN_SHOTS; ++i) {
        arcade_shot_t *shot = &star->player_shots[i];
        if (!shot->y) continue;
        shot->y = shot->y > 25 ? shot->y - 5 : 0;
        if (!shot->y) continue;
        for (unsigned enemy = 0; enemy < STAR_ENEMIES; ++enemy) {
            if (!(star->enemies & (1u << enemy)) ||
                abs(shot->x - arcade_star_x(star, enemy)) > 6 ||
                abs(shot->y - arcade_star_y(star, enemy)) > 5) continue;
            star->enemies &= (uint16_t)~(1u << enemy);
            shot->y = 0;
            game->score += 10;
            break;
        }
    }
    for (unsigned i = 0; i < STAR_FOE_SHOTS; ++i) {
        arcade_shot_t *shot = &star->enemy_shots[i];
        if (!shot->y) continue;
        shot->y = shot->y < 114 ? shot->y + 3 : 0;
        if (shot->y < 98 || shot->y > 110 || abs(shot->x - star->ship_x) > 8) continue;
        shot->y = 0;
        if (game->elapsed - star->safe_at >= HIT_GRACE_MS) {
            star->safe_at = game->elapsed;
            lose_life(game);
        }
    }
}

static bool update_star(arcade_game_t *game) {
    arcade_star_t *star = &game->play.star;
    uint32_t time = game->elapsed;
    if (!star->enemies) {
        if (time - star->wave_at < 750) return false;
        level_up(game);
        star_spawn(game);
        return true;
    }
    if (time - star->moved_at < 40) return false;
    star->moved_at = time;
    int speed = game->level < 4 ? game->level : 4;
    int x = star->fleet_x + star->direction * speed;
    if (x < 8 || x > 93) {
        star->direction = (int8_t)-star->direction;
        star->fleet_y += 8;
    } else star->fleet_x = (uint8_t)x;

    if (star->fleet_y >= 82) {
        lose_life(game);
        if (game->phase == ARCADE_PLAYING) star_formation(game);
        return true;
    }
    if (time - star->fired_at >= interval(1380, 500, game->level, 120)) {
        star->fired_at = time;
        fleet_fire(game);
    }
    update_lasers(game);
    if (!star->enemies && game->phase == ARCADE_PLAYING) {
        game->score += 50;
        star->wave_at = time;
    }
    return true;
}

void arcade_start(arcade_game_t *game, snake_style_t style, uint32_t now) {
    *game = (arcade_game_t){.phase = ARCADE_PLAYING, .style = style, .wall_at = now,
        .level = 1, .random = (now ^ 0x9e3779b9u) | 1u};
    switch (style) {
        case STYLE_SNAKE: {
            arcade_snake_t *snake = &game->play.snake;
            game->lives = 1;
            snake->body[0] = 4 * ARCADE_COLS + 2;
            snake->body[1] = 4 * ARCADE_COLS + 3;
            snake->body[2] = 4 * ARCADE_COLS + 4;
            snake->head = 2;
            snake->length = 3;
            snake->pellet = 4 * ARCADE_COLS + 8;
            break;
        }
        case STYLE_BRICKS: game->lives = 3; break;
        case STYLE_STACK:
            game->play.stack.next = stack_take(game);
            stack_spawn(game);
            break;
        case STYLE_MAZE:
            game->lives = 3;
            fill_maze(game, 0);
            break;
        case STYLE_STAR:
            game->lives = 3;
            game->play.star.ship_x = 96;
            star_spawn(game);
            break;
        default: game->phase = ARCADE_IDLE; break;
    }
}

bool arcade_update(arcade_game_t *game, uint32_t now) {
    if (game->phase != ARCADE_PLAYING) return false;
    uint32_t dt = now - game->wall_at;
    game->wall_at = now;
    game->elapsed += dt > 240 ? 240u : dt;
    bool changed = false;
    switch (game->style) {
        case STYLE_SNAKE: changed = update_snake(game); break;
        case STYLE_BRICKS: changed = update_bricks(game); break;
        case STYLE_STACK: changed = update_stack(game); break;
        case STYLE_MAZE: changed = update_maze(game); break;
        case STYLE_STAR: changed = update_star(game); break;
        default: break;
    }
    if (game->phase == ARCADE_RESULT) game->ended_at = now;
    return changed;
}

bool arcade_press(arcade_game_t *game, arcade_key_t key, uint32_t now) {
    if (game->phase == ARCADE_RESULT) {
        if (key != ARCADE_OK || now - game->ended_at < 420u) return false;
        arcade_start(game, game->style, now);
        return true;
    }
    if (game->phase == ARCADE_IDLE) return false;
    if (key == ARCADE_PAUSE || (key == ARCADE_OK && game->phase == ARCADE_PAUSED) ||
        (key == ARCADE_OK && game->style == STYLE_SNAKE)) {
        game->phase = game->phase == ARCADE_PAUSED ? ARCADE_PLAYING : ARCADE_PAUSED;
        game->wall_at = now;
        return true;
    }
    if (game->phase != ARCADE_PLAYING) return false;

    bool lateral = key == ARCADE_LEFT || key == ARCADE_RIGHT;
    switch (game->style) {
        case STYLE_SNAKE:
            if (lateral) game->play.snake.next_direction = turn(game->play.snake.direction, key);
            return lateral;
        case STYLE_BRICKS:
            if (key == ARCADE_OK) brick_game_jump(&game->play.brick, game->elapsed);
            return key == ARCADE_OK;
        case STYLE_STACK: {
            bool changed = stack_press(game, key);
            if (game->phase == ARCADE_RESULT) game->ended_at = now;
            return changed;
        }
        case STYLE_MAZE:
            if (lateral) game->play.maze.next_direction = turn(game->play.maze.direction, key);
            else if (key == ARCADE_OK) game->play.maze.next_direction = (game->play.maze.direction + 2u) % 4u;
            return lateral || key == ARCADE_OK;
        case STYLE_STAR: {
            arcade_star_t *star = &game->play.star;
            if (key == ARCADE_OK) {
                return star->enemies && shoot(star->player_shots, STAR_OWN_SHOTS, star->ship_x, STAR_SHOT_Y);
            }
            if (!lateral) return false;
            int x = star->ship_x + (key == ARCADE_LEFT ? -12 : 12);
            if (x < STAR_SHIP_MIN_X) x = STAR_SHIP_MIN_X;
            if (x > STAR_SHIP_MAX_X) x = STAR_SHIP_MAX_X;
            star->ship_x = (uint8_t)x;
            return true;
        }
        default: return false;
    }
}
