#include "snake_arcade.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    const snake_model_t *model;
    uint32_t now;
    uint32_t phase;
    bool fresh;
    snake_rect_fn rect;
    void *context;
} scene_t;

static void box(const scene_t *scene, uint32_t color, int x, int y, int w, int h) {
    scene->rect(scene->context, color, x, y, w, h);
}

static uint32_t port_color(const scene_t *scene, unsigned port) {
    uint32_t rgb = snake_port_colors[port];
    if (scene->fresh) return rgb;
    return ((rgb & 0xf8f8f8u) >> 3) + ((rgb & 0xf8f8f8u) >> 2);
}

static int triangle(uint32_t phase, unsigned span) {
    unsigned position = phase % (2u * span);
    return (int)(position <= span ? position : 2u * span - position);
}

static void sprite(const scene_t *scene, const char *const *rows, unsigned height,
                   const uint32_t *colors, int x, int y, int scale) {
    for (unsigned r = 0; r < height; ++r) {
        for (int column = 0; rows[r][column];) {
            int start = column;
            char mark = rows[r][column++];
            while (rows[r][column] == mark) ++column;
            if (mark != '.')
                box(scene, colors[mark - '0'], x + start * scale, y + (int)r * scale,
                    (column - start) * scale, scale);
        }
    }
}

static void draw_slots(const scene_t *scene, int x, int y) {
    snake_freshness_t fresh = snake_freshness(scene->model, scene->now);
    if (fresh == SNAKE_EMPTY || fresh == SNAKE_LOST) return;
    float position = 0;
    int start = 0;
    for (unsigned port = 0; port < SNAKE_PORTS; ++port) {
        position += scene->model->body[port];
        int end = (int)lroundf(position);
        while (start < end) {
            int cell = start / SNAKE_CELL;
            int base = cell * SNAKE_CELL;
            int edge = end < base + SNAKE_CELL ? end : base + SNAKE_CELL;
            int left = (start - base) * 10 / SNAKE_CELL;
            int right = (edge - base) * 10 / SNAKE_CELL;
            if (left < right) {
                int px = x + cell % 14 * 12;
                int py = y - cell / 14 * 8;
                box(scene, port_color(scene, port), px + left, py, right - left, 6);
                if (scene->fresh && right - left >= 4)
                    box(scene, 0x5b3322, px + left + 1, py + 4, right - left - 2, 1);
            }
            start = edge;
        }
    }
}

static void power_rail(const scene_t *scene, int y) {
    enum { WIDTH = 168, X = 12, CAPACITY = 40 * SNAKE_CELL };
    box(scene, 0x433641, X, y, WIDTH, 4);
    snake_freshness_t fresh = snake_freshness(scene->model, scene->now);
    if (fresh == SNAKE_EMPTY || fresh == SNAKE_LOST) return;
    float amount = 0.0f;
    int start = 0;
    for (unsigned port = 0; port < SNAKE_PORTS; ++port) {
        amount += scene->model->body[port];
        int end = (int)lroundf(amount * WIDTH / CAPACITY);
        if (end > WIDTH) end = WIDTH;
        if (end > start) box(scene, port_color(scene, port), X + start, y, end - start, 4);
        start = end;
    }
}

static void clipped_box(const scene_t *scene, uint32_t color, int x, int y, int w, int h) {
    int right = x + w;
    if (x < 0) x = 0;
    if (right > SNAKE_BOARD_W) right = SNAKE_BOARD_W;
    if (right > x) box(scene, color, x, y, right - x, h);
}

static void draw_coin(const scene_t *scene, int x, int y) {
    clipped_box(scene, 0xffa51e, x, y + 1, 6, 7);
    clipped_box(scene, 0xffa51e, x + 1, y, 4, 9);
    clipped_box(scene, 0xffefab, x + 2, y + 1, 1, 6);
}

static void round_bricks(const scene_t *scene, const brick_game_t *game, uint32_t time) {
    int left = brick_game_left(game, time);
    bool hint = brick_game_hint(game, time);
    if (brick_game_obstacle(game)) {
        clipped_box(scene, 0xa14e43, left + 2, 74, 14, 8);
        clipped_box(scene, hint ? 0xffedaa : 0xff976e, left + 4, 70, 10, 4);
        clipped_box(scene, 0x452738, left + 7, 74, 4, 5);
    } else {
        const int top = 66 - BRICK_JUMP_HEIGHT - 12;
        bool scored = game->passed;
        clipped_box(scene, scored ? 0x5b493e : hint ? 0xb57536 : 0x765239, left, top, BRICK_WIDTH, 12);
        clipped_box(scene, scored ? 0x756251 : 0xbf8a55, left + 1, top + 1, BRICK_WIDTH - 2, 2);
        clipped_box(scene, hint ? 0xffecac : 0x4c342d, left + 1, top + 10, BRICK_WIDTH - 2, 2);
        if (!scored) clipped_box(scene, 0xffd479, left + 8, top + 4, 2, 3);
        uint32_t pop_age = time - game->feedback_at;
        if (scored && pop_age < 500) draw_coin(scene, left + 6, 26 - triangle(pop_age, 250) * 7 / 250);
    }
}

static void brick_landscape(const scene_t *scene, unsigned chapter) {
    static const uint32_t sky[] = {0x2c202d, 0x192d30, 0x1f203c};
    static const uint32_t far[] = {0x48303a, 0x2e4841, 0x323456};
    static const uint32_t near[] = {0x563941, 0x37564a, 0x464062};
    box(scene, sky[chapter], 0, 0, 192, 120);
    if (chapter == 0) {
        box(scene, 0xb37d63, 142, 16, 18, 16);
        box(scene, far[chapter], 0, 62, 76, 20);
        box(scene, far[chapter], 18, 53, 47, 9);
        box(scene, near[chapter], 115, 68, 77, 14);
        box(scene, near[chapter], 137, 59, 40, 9);
    } else if (chapter == 1) {
        for (int x = 12; x < 180; x += 42) {
            box(scene, far[chapter], x, 54, 24, 28);
            box(scene, far[chapter], x + 6, 44, 12, 10);
            box(scene, near[chapter], x + 6, 69, 12, 13);
        }
        box(scene, near[chapter], 130, 19, 36, 4);
    } else {
        box(scene, 0x68658b, 145, 15, 18, 15);
        box(scene, sky[chapter], 151, 13, 14, 12);
        for (int x = 14; x < 184; x += 26)
            box(scene, near[chapter], x, 17 + x % 19, 2, 2);
        box(scene, far[chapter], 0, 73, 192, 9);
        box(scene, far[chapter], 24, 65, 19, 9);
        box(scene, far[chapter], 129, 61, 29, 12);
    }
}

static void bricks(const scene_t *scene, const brick_game_t *game, const brick_relay_t *relay, uint32_t time) {
    unsigned chapter = game ? game->index / 6u % ARCADE_MAZES : snake_idle_scene(scene->phase);
    brick_game_t preview = {0};
    if (!game) {
        enum { BEAT_MS = 2900, PRESS_MS = 2010 };
        uint32_t beat = scene->phase % ARCADE_SCENE_MS;
        preview.index = beat / BEAT_MS % 3u;
        time = beat % BEAT_MS;
        if (time >= PRESS_MS) brick_game_jump(&preview, PRESS_MS);
        if (time > PRESS_MS + BRICK_CONTACT_MS)
            brick_game_tick(&preview, PRESS_MS + BRICK_CONTACT_MS);
        brick_game_tick(&preview, time);
        game = &preview;
    }
    bool relaying = scene->fresh && brick_relay_active(relay, scene->now);
    brick_landscape(scene, chapter);
    box(scene, 0x664c42, 0, 82, 192, 3);
    box(scene, 0x3c302e, 0, 85, 192, 2);
    box(scene, 0x43332d, 0, 111, 192, 9);
    box(scene, 0x75543b, 0, 110, 192, 2);
    for (int x = 0; x < 192; x += 16) box(scene, 0x624535, x + 1, 115, 14, 2);
    draw_slots(scene, 12, 102);
    if (relaying) {
        box(scene, snake_port_colors[relay->from], 12, 111, 82, 2);
        box(scene, snake_port_colors[relay->to], 94, 111, 84, 2);
    }
    round_bricks(scene, game, time);
    if (relaying && scene->now - relay->at < 720)
        draw_coin(scene, 86, 33 - (int)((scene->now - relay->at) / 60));
    int runner_y = 66 - brick_game_height(game, time);
    static const char *const runner[] = {
        "..0000..","..00000.","..1111..","..1211..",
        ".33333..","3330333.",".44444..",".44.44.."
    };
    static const uint32_t colors[] = {0xffc444,0xf2bf84,0x35231c,0x2c89fb,0xff6918};
    bool flash = game->crashed && time - game->feedback_at < 400;
    if (!flash || time / 80u % 2 == 0) {
        bool stride = runner_y == 66 && time / 120u % 2;
        sprite(scene, runner, stride ? 6 : 8, colors, BRICK_RUNNER_X, runner_y, 2);
        if (stride) {
            box(scene, colors[4], BRICK_RUNNER_X + 2, runner_y + 12, 8, 2);
            box(scene, colors[4], BRICK_RUNNER_X + 10, runner_y + 14, 4, 2);
        }
    }
}

static void snake_street(const scene_t *scene) {
    unsigned chapter = snake_idle_scene(scene->phase);
    static const uint32_t ground[] = {0x241a17, 0x182824, 0x241d30};
    static const uint32_t trail[] = {0x4c372b, 0x304c3b, 0x473552};
    static const uint32_t trim[] = {0x785042, 0x537e60, 0x745779};
    box(scene, ground[chapter], 0, 0, SNAKE_BOARD_W, SNAKE_BOARD_H);
    for (int i = 0; i < SNAKE_TRACK; ++i) {
        snake_point_t p = snake_track(i);
        box(scene, trail[chapter], p.x - 1, p.y - 1, 2, 2);
    }
    int sway = triangle(scene->phase / 480u, 4);
    if (chapter == 0) {
        box(scene, 0x3f2b22, 32, 50, 24, 32);
        box(scene, trim[chapter], 29, 48, 30, 6);
        box(scene, 0x6d4731, 35, 58, 8, 10);
        box(scene, 0x6d4731, 47, 58, 6, 10);
        box(scene, sway > 1 ? 0xa76f44 : 0x805938, 37, 60, 4, 6);
        box(scene, 0x231b18, 41, 72, 7, 10);
        for (int x = 83; x < 160; x += 18) box(scene, trail[chapter], x, 58, 11, 7);
    } else if (chapter == 1) {
        box(scene, 0x284236, 83, 59, 77, 9);
        for (int x = 90; x < 154; x += 14) {
            box(scene, trim[chapter], x, 57 - sway / 2, 4, 7);
            box(scene, 0x879866, x + 2, 54 - sway / 2, 3, 4);
        }
        box(scene, 0x3b6655, 36, 48, 16, 24);
        box(scene, 0x608578, 39, 51, 10, 15);
    } else {
        box(scene, 0x523b5d, 92, 55, 51, 14);
        box(scene, trim[chapter], 98, 53, 38, 3);
        for (int x = 101; x < 135; x += 8)
            box(scene, (x + sway) % 3 ? 0xb17e9e : 0x7f5c86, x, 60, 3, 4);
        box(scene, 0x775b78, 38, 51, 13, 13);
        box(scene, ground[chapter], 43, 48, 10, 11);
    }
    snake_draw_game(scene->model, scene->now, scene->rect, scene->context);
}

static uint32_t space_backdrop(const scene_t *scene, unsigned chapter) {
    static const uint32_t sky[] = {0x171d2d, 0x241d32, 0x14272c};
    static const uint32_t stars[] = {0x55687f, 0x6a5174, 0x466b68};
    static const uint32_t planet[] = {0x324557, 0x493652, 0x2b494a};
    box(scene, sky[chapter], 0, 0, SNAKE_BOARD_W, SNAKE_BOARD_H);
    for (unsigned i = 0; i < 19; ++i) {
        int x = (int)((i * 47 + chapter * 21) % 183 + 4);
        int y = (int)((i * 29 + chapter * 11) % 85 + 20);
        box(scene, stars[chapter], x, y, i % 5 ? 1 : 2, 1);
    }
    int x = chapter == 1 ? 23 : 142, y = chapter == 2 ? 49 : 35;
    box(scene, planet[chapter], x, y + 6, 31, 16);
    box(scene, planet[chapter], x + 5, y + 2, 21, 24);
    box(scene, stars[chapter], x + 3, y + 13, 26, 2);
    return sky[chapter];
}

static void preview_star(const scene_t *scene) {
    unsigned chapter = snake_idle_scene(scene->phase);
    uint32_t sky = space_backdrop(scene, chapter);
    unsigned t = scene->phase % ARCADE_SCENE_MS;
    int x = t < 2600 ? 19 + (int)t * 77 / 2600 :
            t < 6500 ? 96 : 96 + (int)(t - 6500) * 77 / (ARCADE_SCENE_MS - 6500);
    static const char *const invader[] = {"..0..0..",".000000.","00011000","0.0000.0","..0..0.."};
    const uint32_t enemy[] = {chapter == 1 ? 0xd397ae : 0x8ebcb7, sky};
    for (int i = 0; i < 3; ++i) {
        if (i == 1 && t >= 4400 && t < 9000) continue;
        int y = i == 1 ? 32 : 28 + triangle(t / 180u + (unsigned)i * 3, 8);
        sprite(scene, invader, 5, enemy, 44 + i * 44, y, 2);
    }
    if (t >= 3600 && t < 4400) {
        int y = 65 - (int)(t - 3600) * 26 / 800;
        box(scene, 0xffdc87, 95, y, 2, 7);
    } else if (t >= 4400 && t < 5100) {
        int spread = 2 + (int)(t - 4400) / 88;
        box(scene, 0xffd49a, 96 - spread, 37, 2, 2);
        box(scene, 0xff9c77, 96 + spread, 39, 2, 2);
        box(scene, 0xffe4a6, 96, 36 - spread, 2, 3);
    }
    box(scene, 0xffe4b0, x - 2, 70, 5, 9);
    box(scene, 0x81c8c2, x - 5, 76, 11, 7);
    box(scene, 0x81c8c2, x - 8, 80, 17, 3);
    box(scene, 0xf5a66c, x - 1, 84, 3, 2 + triangle(t / 125u, 4));
    power_rail(scene, 105);
}

enum { GRID_X = 11, GRID_Y = 22, GRID_TILE = 10, WELL_X = 3, WELL_Y = 3, WELL_TILE = 9 };

static void grid_border(const scene_t *scene, uint32_t color) {
    int w = ARCADE_COLS * GRID_TILE, h = ARCADE_ROWS * GRID_TILE;
    box(scene, color, GRID_X - 1, GRID_Y - 1, w + 2, 1);
    box(scene, color, GRID_X - 1, GRID_Y + h, w + 2, 1);
    box(scene, color, GRID_X - 1, GRID_Y, 1, h);
    box(scene, color, GRID_X + w, GRID_Y, 1, h);
}

static int grid_x(unsigned cell) { return GRID_X + (int)(cell % ARCADE_COLS) * GRID_TILE; }
static int grid_y(unsigned cell) { return GRID_Y + (int)(cell / ARCADE_COLS) * GRID_TILE; }

static void play_snake(const scene_t *scene, const arcade_snake_t *snake) {
    box(scene, 0x211914, 0, 0, SNAKE_BOARD_W, SNAKE_BOARD_H);
    box(scene, 0x17130f, GRID_X, GRID_Y, ARCADE_COLS * GRID_TILE, ARCADE_ROWS * GRID_TILE);
    grid_border(scene, 0x66503c);
    for (int x = 1; x < ARCADE_COLS; ++x)
        box(scene, 0x29201a, GRID_X + x * GRID_TILE, GRID_Y, 1, ARCADE_ROWS * GRID_TILE);
    for (int y = 1; y < ARCADE_ROWS; ++y)
        box(scene, 0x29201a, GRID_X, GRID_Y + y * GRID_TILE, ARCADE_COLS * GRID_TILE, 1);
    int food_x = grid_x(snake->pellet), food_y = grid_y(snake->pellet);
    box(scene, 0xf9935e, food_x + 2, food_y + 4, 6, 4);
    box(scene, 0xffba74, food_x + 3, food_y + 3, 4, 6);
    box(scene, 0xb9da85, food_x + 5, food_y + 1, 2, 2);
    int previous_x = 0, previous_y = 0;
    for (unsigned i = snake->length; i > 0; --i) {
        unsigned cell = snake->body[(snake->head + ARCADE_CELLS - i + 1) % ARCADE_CELLS];
        int x = grid_x(cell), y = grid_y(cell);
        uint32_t color = i == 1 ? 0xffd66d : i % 2 ? 0xd28b44 : 0xf3aa4e;
        if (i != snake->length) {
            if (x == previous_x)
                box(scene, color, x + 2, y < previous_y ? y + 8 : previous_y + 8, 6, 4);
            else box(scene, color, x < previous_x ? x + 8 : previous_x + 8, y + 2, 4, 6);
        }
        box(scene, color, x + 1, y + 1, 8, 8);
        if (i == 1) {
            bool horizontal = snake->direction == EAST || snake->direction == WEST;
            int ahead = snake->direction == EAST || snake->direction == SOUTH ? 6 : 2;
            box(scene, 0x32211a, x + (horizontal ? ahead : 2), y + (horizontal ? 2 : ahead), 2, 2);
            box(scene, 0x32211a, x + (horizontal ? ahead : 6), y + (horizontal ? 6 : ahead), 2, 2);
        }
        previous_x = x;
        previous_y = y;
    }
}

static uint32_t maze_board(const scene_t *scene, const arcade_maze_t *maze) {
    static const uint32_t bg[] = {0x201924, 0x182a27, 0x251e33};
    static const uint32_t wall[] = {0x594051, 0x395d53, 0x524668};
    static const uint32_t edge[] = {0x825a70, 0x5f8270, 0x76648c};
    unsigned map = maze->map;
    box(scene, bg[map], 0, 0, SNAKE_BOARD_W, SNAKE_BOARD_H);
    grid_border(scene, edge[map]);
    for (unsigned y = 0; y < ARCADE_ROWS; ++y) {
        for (unsigned x = 0; x < ARCADE_COLS;) {
            unsigned cell = y * ARCADE_COLS + x;
            if (arcade_maze_wall(map, cell)) {
                unsigned start = x++;
                while (x < ARCADE_COLS && arcade_maze_wall(map, y * ARCADE_COLS + x)) ++x;
                int width = (int)(x - start) * GRID_TILE;
                box(scene, wall[map], grid_x(cell), grid_y(cell), width, GRID_TILE);
                box(scene, edge[map], grid_x(cell), grid_y(cell), width, 2);
                continue;
            }
            if (arcade_maze_pellet(maze, cell)) {
                int size = arcade_maze_power(map, cell) ? 4 : 2;
                int pad = (GRID_TILE - size) / 2;
                box(scene, size == 4 ? 0xffc362 : 0xa88679, grid_x(cell) + pad, grid_y(cell) + pad, size, size);
            }
            ++x;
        }
    }
    return bg[map];
}

static void maze_player(const scene_t *scene, int x, int y, unsigned direction, uint32_t bg, bool powered) {
    uint32_t player = powered ? 0xffedb0 : 0xffcb51;
    box(scene, player, x + 1, y + 3, 8, 4);
    box(scene, player, x + 3, y + 1, 4, 8);
    box(scene, player, x + 2, y + 2, 6, 6);
    switch (direction) {
        case EAST: box(scene, bg, x + 6, y + 4, 3, 2); break;
        case WEST: box(scene, bg, x + 1, y + 4, 3, 2); break;
        case NORTH: box(scene, bg, x + 4, y + 1, 2, 3); break;
        case SOUTH: box(scene, bg, x + 4, y + 6, 2, 3); break;
    }
}

static void maze_ghost(const scene_t *scene, int x, int y, uint32_t bg, bool powered) {
    uint32_t color = powered ? 0x64cbbd : 0xef8b87;
    box(scene, color, x + 2, y + 2, 6, 2);
    box(scene, color, x + 1, y + 4, 8, 4);
    box(scene, color, x + 1, y + 8, 2, 1);
    box(scene, color, x + 7, y + 8, 2, 1);
    box(scene, bg, x + 3, y + 4, 1, 2);
    box(scene, bg, x + 6, y + 4, 1, 2);
}

static void play_maze(const scene_t *scene, const arcade_game_t *game) {
    const arcade_maze_t *maze = &game->play.maze;
    uint32_t bg = maze_board(scene, maze);
    bool powered = arcade_maze_powered(game);
    maze_player(scene, grid_x(maze->player), grid_y(maze->player), maze->direction, bg, powered);
    if (game->elapsed >= maze->ghost_return_until)
        maze_ghost(scene, grid_x(maze->ghost), grid_y(maze->ghost), bg, powered);
}

static void preview_maze(const scene_t *scene) {
    enum { STEP_MS = 240 };
    unsigned map = snake_idle_scene(scene->phase);
    arcade_maze_t maze = {.map = (uint8_t)map};
    for (unsigned cell = 0; cell < ARCADE_CELLS; ++cell)
        maze.pellets[cell / 8] |= (uint8_t)(1u << (cell % 8));
    unsigned t = scene->phase % ARCADE_SCENE_MS;
    unsigned step = t / STEP_MS + arcade_maze_patrol_length(map);
    int progress = (int)(t % STEP_MS);
    bool powered = false;
    for (unsigned trail = 0; trail < 7; ++trail) {
        unsigned eaten = arcade_maze_patrol(map, step - trail);
        maze.pellets[eaten / 8] &= (uint8_t)~(1u << (eaten % 8));
        if (arcade_maze_power(map, eaten)) powered = true;
    }
    uint32_t bg = maze_board(scene, &maze);
    unsigned cell = arcade_maze_patrol(map, step), next = arcade_maze_patrol(map, step + 1);
    int x = grid_x(cell), y = grid_y(cell);
    int dx = grid_x(next) - x, dy = grid_y(next) - y;
    unsigned direction = dx > 0 ? EAST : dx < 0 ? WEST : dy > 0 ? SOUTH : NORTH;
    maze_player(scene, x + dx * progress / STEP_MS, y + dy * progress / STEP_MS, direction, bg, powered);
    cell = arcade_maze_patrol(map, step - 5);
    next = arcade_maze_patrol(map, step - 4);
    x = grid_x(cell);
    y = grid_y(cell);
    maze_ghost(scene, x + (grid_x(next) - x) * progress / STEP_MS,
               y + (grid_y(next) - y) * progress / STEP_MS, bg, powered);
    power_rail(scene, 115);
}

static void well_tile(const scene_t *scene, unsigned piece, int x, int y, int size) {
    static const uint32_t candy[] = {0x78d2c9, 0xffcb59, 0xa889e7, 0x8dcc91, 0xf1848b, 0x79a9ec, 0xf5a270};
    box(scene, candy[piece], x, y, size - 1, size - 1);
    box(scene, 0xffead5, x + 1, y + 1, size - 3, 1);
}

static void stack_well(const scene_t *scene, bool preview) {
    static const uint32_t bg[] = {0x241c24, 0x1b2928, 0x292239};
    static const uint32_t edge[] = {0x6d5269, 0x52766c, 0x6e5987};
    unsigned chapter = preview ? snake_idle_scene(scene->phase) : 0;
    int w = STACK_COLS * WELL_TILE, h = STACK_ROWS * WELL_TILE;
    box(scene, bg[chapter], 0, 0, SNAKE_BOARD_W, STACK_FIELD_H);
    box(scene, edge[chapter], WELL_X - 1, WELL_Y - 1, w + 1, h + 1);
    box(scene, 0x17141b, WELL_X, WELL_Y, w - 1, h - 1);
    for (int col = 1; col < STACK_COLS; ++col)
        box(scene, 0x332634, WELL_X + col * WELL_TILE - 1, WELL_Y, 1, h - 1);
    for (int row = 1; row < STACK_ROWS; ++row)
        box(scene, 0x332634, WELL_X, WELL_Y + row * WELL_TILE - 1, w - 1, 1);
    box(scene, 0x211b25, STACK_SIDE_X, 0, SNAKE_BOARD_W - STACK_SIDE_X, STACK_FIELD_H);
    box(scene, 0x554153, STACK_SIDE_X, STACK_PORT_Y - 3, SNAKE_BOARD_W - STACK_SIDE_X, 1);
}

static void next_piece(const scene_t *scene, unsigned piece, int y, bool idle) {
    uint16_t shape = arcade_stack_shape(piece);
    int min_x = 3, max_x = 0, min_y = 3, max_y = 0;
    for (int i = 0; i < 16; ++i) {
        if (!(shape & (1u << i))) continue;
        int x = i % 4, row = i / 4;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (row < min_y) min_y = row;
        if (row > max_y) max_y = row;
    }
    const int tile = 7;
    int x0 = STACK_SIDE_X + (SNAKE_BOARD_W - STACK_SIDE_X - (max_x - min_x + 1) * tile) / 2;
    int y0 = y + (22 - (max_y - min_y + 1) * tile) / 2;
    for (int i = 0; i < 16; ++i) {
        if (!(shape & (1u << i))) continue;
        int x = x0 + (i % 4 - min_x) * tile, row = y0 + (i / 4 - min_y) * tile;
        if (idle) box(scene, 0x916c89, x, row, tile - 1, tile - 1);
        else well_tile(scene, piece, x, row, tile);
    }
}

static void preview_stack(const scene_t *scene) {
    enum { LAYER_MW = 7000 };
    stack_well(scene, true);
    snake_freshness_t fresh = snake_freshness(scene->model, scene->now);
    for (unsigned port = 0; port < SNAKE_PORTS; ++port) {
        int x = WELL_X + (int)port * 2 * WELL_TILE;
        uint32_t mw = fresh == SNAKE_FRESH || fresh == SNAKE_WAIT ? scene->model->sample.port[port].mw : 0u;
        uint32_t color = port_color(scene, port);
        if (port) box(scene, 0x644e5e, x - 1, WELL_Y, 1, STACK_ROWS * WELL_TILE - 1);
        box(scene, mw ? color : 0x483647, x + 2, 187, 2 * WELL_TILE - 5, 3);
        for (int row = 0; row < STACK_ROWS && mw; ++row) {
            int y = WELL_Y + (STACK_ROWS - 1 - row) * WELL_TILE;
            uint32_t amount = mw > LAYER_MW ? LAYER_MW : mw;
            int height = (int)((amount * (WELL_TILE - 1) + LAYER_MW - 1) / LAYER_MW);
            for (int col = 0; col < 2; ++col)
                box(scene, color, x + col * WELL_TILE, y + WELL_TILE - 1 - height, WELL_TILE - 1, height);
            mw -= amount;
        }
    }
    for (int tick = 0; tick <= STACK_ROWS; tick += 5)
        box(scene, 0x886278, 94, WELL_Y + tick * WELL_TILE, tick % 10 ? 2 : 4, 1);
    unsigned t = scene->phase % 3600u;
    next_piece(scene, (scene->phase / 3600u) % STACK_TYPES, 24 + (int)(t < 2300 ? t : 2300) / 116, true);
    box(scene, 0x654b64, STACK_SIDE_X + 23, 66, 46, 1);
}

static void play_stack(const scene_t *scene, const arcade_stack_t *stack) {
    stack_well(scene, false);
    for (int row = 0; row < STACK_ROWS; ++row) {
        for (int col = 0; col < STACK_COLS; ++col) {
            unsigned piece = stack->cells[row][col];
            if (piece) well_tile(scene, piece - 1u, WELL_X + col * WELL_TILE, WELL_Y + row * WELL_TILE, WELL_TILE);
        }
    }
    int landing = arcade_stack_landing(stack);
    for (int i = 0; i < 16; ++i) {
        if (!(stack->shape & (1u << i))) continue;
        int x = WELL_X + (stack->x + i % 4) * WELL_TILE;
        int y = WELL_Y + (stack->y + i / 4) * WELL_TILE;
        int shadow_y = WELL_Y + (landing + i / 4) * WELL_TILE;
        if (landing > stack->y) {
            box(scene, 0x4d3a4d, x, shadow_y, WELL_TILE - 1, WELL_TILE - 1);
            box(scene, 0x906d8b, x + 1, shadow_y + 6, WELL_TILE - 3, 1);
        }
        well_tile(scene, stack->piece, x, y, WELL_TILE);
    }
    box(scene, 0x332635, STACK_SIDE_X + 15, 23, 62, 25);
    next_piece(scene, stack->next, 24, false);
}

static void preview_diy(const scene_t *scene) {
    box(scene, 0x281e2a, 0, 0, SNAKE_BOARD_W, SNAKE_BOARD_H);
    box(scene, 0x3e3040, 14, 10, 164, 100);
    box(scene, 0x211925, 15, 11, 162, 98);
    box(scene, 0xb5798f, 83, 24, 26, 18);
    box(scene, 0x3e3040, 85, 26, 22, 14);
    box(scene, 0xf2bb6d, 89, 28, 4, 4);
    box(scene, 0x83b4a3, 85, 36, 22, 4);
    box(scene, 0x83b4a3, 96, 32, 8, 5);
}

void snake_draw_style(snake_style_t style, const snake_model_t *model, uint32_t now,
                      uint32_t phase, snake_rect_fn rect, void *context) {
    scene_t scene = {model, now, phase, snake_freshness(model, now) == SNAKE_FRESH, rect, context};
    switch (style) {
        case STYLE_BRICKS: bricks(&scene, NULL, NULL, 0); break;
        case STYLE_STACK: preview_stack(&scene); break;
        case STYLE_MAZE: preview_maze(&scene); break;
        case STYLE_STAR: preview_star(&scene); break;
        case STYLE_DIY: preview_diy(&scene); break;
        default: snake_street(&scene); break;
    }
}

void snake_draw_bricks(const snake_model_t *model, const brick_relay_t *relay,
                       uint32_t now, uint32_t phase, snake_rect_fn rect, void *context) {
    scene_t scene = {model, now, phase, snake_freshness(model, now) == SNAKE_FRESH, rect, context};
    bricks(&scene, NULL, relay, 0);
}

static void play_star(const scene_t *scene, const arcade_game_t *game) {
    const arcade_star_t *star = &game->play.star;
    space_backdrop(scene, (game->level - 1u) % ARCADE_MAZES);
    box(scene, 0x4c3e5c, 8, 114, 176, 2);
    for (unsigned i = 0; i < STAR_ENEMIES; ++i) {
        if (!(star->enemies & (1u << i))) continue;
        int x = arcade_star_x(star, i), y = arcade_star_y(star, i);
        uint32_t hull = i < STAR_COLS ? 0x9bd4ce : 0xec8b9c;
        box(scene, hull, x - 4, y - 4, 9, 2);
        box(scene, hull, x - 6, y - 2, 13, 5);
        box(scene, hull, x - 5, y + 3, 2, 2);
        box(scene, hull, x + 4, y + 3, 2, 2);
        box(scene, 0x231b31, x - 3, y - 1, 2, 2);
        box(scene, 0x231b31, x + 2, y - 1, 2, 2);
    }
    for (unsigned i = 0; i < STAR_OWN_SHOTS; ++i) {
        const arcade_shot_t *shot = &star->player_shots[i];
        if (shot->y) box(scene, 0xffe799, shot->x - 1, shot->y, 2, 5);
    }
    for (unsigned i = 0; i < STAR_FOE_SHOTS; ++i) {
        const arcade_shot_t *shot = &star->enemy_shots[i];
        if (shot->y) box(scene, 0xf88e8c, shot->x - 1, shot->y, 3, 4);
    }
    int x = star->ship_x;
    bool shield = game->elapsed - star->safe_at < 750 && game->elapsed / 100u % 2 != 0;
    box(scene, shield ? 0xffefc0 : 0xffd477, x - 2, 96, 5, 9);
    box(scene, 0xffbd63, x - 5, 102, 11, 6);
    box(scene, 0xf79e74, x - 8, 105, 17, 4);
    box(scene, 0xffefc0, x - 1, 98, 3, 3);
}

void snake_draw_play(const arcade_game_t *game, const snake_model_t *model, const brick_relay_t *relay,
                     uint32_t now, snake_rect_fn rect, void *context) {
    scene_t scene = {model, now, 0, snake_freshness(model, now) == SNAKE_FRESH, rect, context};
    switch (game->style) {
        case STYLE_SNAKE: play_snake(&scene, &game->play.snake); break;
        case STYLE_BRICKS: bricks(&scene, &game->play.brick, relay, game->elapsed); break;
        case STYLE_STACK: play_stack(&scene, &game->play.stack); break;
        case STYLE_MAZE: play_maze(&scene, game); break;
        case STYLE_STAR: play_star(&scene, game); break;
        default: break;
    }
    if (game->phase == ARCADE_RESULT || game->phase == ARCADE_PAUSED) {
        int y = game->style == STYLE_STACK ? STACK_PANEL_Y : 33;
        box(&scene, 0x100e15, 10, y, 172, 56);
        box(&scene, 0x5a4245, 12, y + 2, 168, 52);
        box(&scene, 0x25202a, 13, y + 4, 166, 48);
        box(&scene, 0xffc444, 12, y + 2, 168, 2);
    }
}
