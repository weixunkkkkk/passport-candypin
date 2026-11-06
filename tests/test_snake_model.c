#include "snake_art.h"
#include "snake_arcade.h"
#include "snake_model.h"
#include "snake_pixels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void receive(snake_model_t *m, uint32_t at, snake_source_t src, uint32_t c1, uint32_t c4, bool plugged) {
    snake_sample_t frame = {.source = src, .at = at};
    frame.port[0] = (snake_port_t){c1, plugged ? 1 : 0, plugged};
    frame.port[3] = (snake_port_t){c4, c4 ? 1 : 0, c4 != 0};
    snake_accept(m, &frame, at);
}

static void near(float actual, float expected) { assert(fabsf(actual - expected) < 0.01f); }

static void balance(void) {
    snake_model_t m;
    snake_reset(&m);
    receive(&m, 100, SNAKE_LAN, 20000, 40000, true);
    near(m.body[0], 48);
    near(m.body[3], 96);
    receive(&m, 1000, SNAKE_LAN, 10000, 50000, true);
    assert(snake_total(&m.sample) == 60000 && !m.pellet_ports);
    for (uint32_t t = 1000; t <= 1350; t += 5) {
        snake_tick(&m, t);
        near(m.body[0] + m.body[3], 144);
    }
    near(m.body[0], 24);
    near(m.body[3], 120);
}

static void plug_and_unplug(void) {
    snake_model_t m;
    snake_reset(&m);
    receive(&m, 1, SNAKE_DEMO, 0, 0, false);
    receive(&m, 500, SNAKE_DEMO, 0, 0, true);
    assert(m.port[0].armed && m.port[0].pulsing && !m.pellet_ports);
    receive(&m, 1000, SNAKE_DEMO, 140000, 0, true);
    assert(m.pellet_ports == 1 && m.body[0] == 0);
    snake_tick(&m, 1000);
    snake_tick(&m, 1220);
    assert(m.pellet_ports == 1);
    snake_tick(&m, 1440);
    assert(!m.pellet_ports && m.sparkling && m.change_ms == 500);
    snake_tick(&m, 1690);
    near(m.body[0], 168);
    snake_tick(&m, 1940);
    near(m.body[0], 336);
    receive(&m, 2000, SNAKE_DEMO, 0, 0, false);
    assert(m.port[0].idle_streak == 1);
    snake_tick(&m, 2500);
    near(m.body[0], 0);
    receive(&m, 3000, SNAKE_DEMO, 0, 0, false);
    assert(m.port[0].idle_streak == 2 && m.zero_streak == 2);

    receive(&m, 3100, SNAKE_DEMO, 20000, 0, true);
    assert(m.pellet_ports == 1);
    receive(&m, 3200, SNAKE_DEMO, 0, 0, false);
    assert(!m.pellet_ports && !m.change_ms);
}

static void stale_and_recover(void) {
    snake_model_t m;
    snake_reset(&m);
    receive(&m, 100, SNAKE_LAN, 20000, 0, true);
    snake_tick(&m, 500);
    int head = m.head;
    assert(snake_freshness(&m, 4099) == SNAKE_FRESH);
    snake_tick(&m, 4100);
    assert(snake_freshness(&m, 4100) == SNAKE_WAIT && m.held);
    snake_tick(&m, 10100);
    assert(snake_freshness(&m, 10100) == SNAKE_LOST && m.head == head);
    receive(&m, 10200, SNAKE_MCP, 0, 60000, false);
    assert(!m.pellet_ports && m.change_ms == 350);
    snake_tick(&m, 10550);
    near(m.body[0], 0);
    near(m.body[3], 144);
}

static void adjustable_cadence(void) {
    snake_model_t m;
    snake_reset(&m);
    snake_sample_t sample = {.source = SNAKE_LAN, .at = 100, .interval_ms = 5000};
    snake_accept(&m, &sample, 100);
    assert(snake_freshness(&m, 15099) == SNAKE_FRESH);
    assert(snake_freshness(&m, 15100) == SNAKE_WAIT);
    assert(snake_freshness(&m, 40100) == SNAKE_LOST);
    snake_prefs_t prefs = snake_prefs_default();
    assert(snake_prefs_valid(&prefs));
    prefs.sleep_seconds = 0;
    assert(snake_prefs_valid(&prefs));
    prefs.mcp_seconds = 1;
    assert(!snake_prefs_valid(&prefs));
}

typedef struct { int w; int h; unsigned drawn; } clip_t;

static void in_bounds(void *context, uint32_t color, int x, int y, int w, int h) {
    clip_t *clip = context;
    (void)color;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= clip->w && y + h <= clip->h);
    ++clip->drawn;
}

static void draw_all_positions(void) {
    for (int i = 0; i < SNAKE_TRACK; ++i) {
        snake_point_t p = snake_track(i), q = snake_track(i + 1);
        assert(abs(p.x - q.x) + abs(p.y - q.y) == SNAKE_CELL);
        for (int j = i + 1; j < SNAKE_TRACK; ++j) {
            snake_point_t other = snake_track(j);
            assert(other.x != p.x || other.y != p.y);
        }
    }
    snake_model_t m;
    snake_reset(&m);
    receive(&m, 1, SNAKE_DEMO, 140000, 60000, true);
    clip_t game = {192, 120, 0}, numbers = {240, 90, 0};
    for (unsigned h = 0; h < SNAKE_TRACK; ++h) {
        m.head = (uint8_t)h;
        m.pellet = (uint8_t)((h + 2) % SNAKE_TRACK);
        m.pellet_ports = 5;
        m.sparkling = true;
        m.spark_color = 0;
        m.spark_at = 1;
        for (unsigned style = 0; style < STYLE_COUNT; ++style) {
            game.h = style == STYLE_STACK ? STACK_FIELD_H : SNAKE_BOARD_H;
            snake_draw_style((snake_style_t)style, &m, 120, h * 150u, in_bounds, &game);
        }
    }
    snake_draw_watts(&m, 120, in_bounds, &numbers);
    assert(game.drawn > 1000 && numbers.drawn > 10);
}

static void advance(arcade_game_t *game, uint32_t *now, unsigned duration) {
    while (duration) {
        unsigned step = duration > 40 ? 40 : duration;
        *now += step;
        arcade_update(game, *now);
        duration -= step;
    }
}

static void snake_challenge(void) {
    uint32_t now = UINT32_MAX - 1000u;
    arcade_game_t game;
    arcade_start(&game, STYLE_SNAKE, now);
    advance(&game, &now, 1800);
    assert(game.score == 1 && game.play.snake.length == 4);
    assert(arcade_press(&game, ARCADE_LEFT, now));
    advance(&game, &now, 450);
    assert(game.play.snake.direction == NORTH);
    assert(arcade_press(&game, ARCADE_OK, now) && game.phase == ARCADE_PAUSED);
    uint32_t frozen = game.elapsed;
    advance(&game, &now, 30000);
    assert(game.elapsed == frozen);
    assert(arcade_press(&game, ARCADE_OK, now));
    advance(&game, &now, 2400);
    assert(game.phase == ARCADE_RESULT && game.lives == 0);
    frozen = game.elapsed;
    advance(&game, &now, 120000);
    assert(game.phase == ARCADE_RESULT && game.elapsed == frozen && game.score >= 1);
    assert(arcade_press(&game, ARCADE_OK, now));
    assert(game.phase == ARCADE_PLAYING && game.score == 0 && game.play.snake.length == 3);
}

static void brick_challenge(void) {
    uint32_t now = 1;
    arcade_game_t game;
    arcade_start(&game, STYLE_BRICKS, now);
    assert(game.lives == 3);
    for (unsigned i = 0; i < 1200; ++i) {
        advance(&game, &now, 40);
        if (brick_game_hint(&game.play.brick, game.elapsed)) arcade_press(&game, ARCADE_OK, now);
    }
    assert(game.phase == ARCADE_PLAYING && game.elapsed == 48000);
    assert(game.score >= 10 && game.level >= 3 && game.lives == 3);
    unsigned score = game.score;
    advance(&game, &now, 30000);
    assert(game.phase == ARCADE_RESULT && game.lives == 0 && game.score == score);
    advance(&game, &now, 90000);
    assert(game.phase == ARCADE_RESULT);
    assert(arcade_press(&game, ARCADE_OK, now) && game.score == 0 && game.lives == 3);
}

static void stack_challenge(void) {
    uint32_t now = 1;
    arcade_game_t game;
    arcade_start(&game, STYLE_STACK, now);
    arcade_stack_t *stack = &game.play.stack;
    stack->piece = 0;
    stack->shape = arcade_stack_shape(0);
    uint16_t flat = stack->shape;
    for (unsigned i = 0; i < 4; ++i) assert(arcade_press(&game, ARCADE_OK, now));
    assert(stack->shape == flat);
    assert(arcade_press(&game, ARCADE_LEFT, now) && stack->x == 2);
    assert(arcade_press(&game, ARCADE_RIGHT, now) && stack->x == 3);
    advance(&game, &now, 800);
    assert(stack->y == 1);
    for (int i = 0; i < STACK_COLS; ++i) {
        if (i < 3 || i >= 7) stack->cells[STACK_ROWS - 1][i] = 2;
    }
    assert(arcade_press(&game, ARCADE_DROP, now));
    assert(stack->lines == 1 && game.score >= 100);
    for (unsigned i = 0; i < STACK_COLS; ++i) assert(stack->cells[STACK_ROWS - 1][i] == 0);
    for (unsigned i = 0; i < STACK_ROWS && game.phase == ARCADE_PLAYING; ++i)
        arcade_press(&game, ARCADE_DROP, ++now);
    assert(game.phase == ARCADE_RESULT);
    assert(!arcade_press(&game, ARCADE_OK, now));
    advance(&game, &now, 120000);
    assert(arcade_press(&game, ARCADE_OK, now) && stack->lines == 0 && game.score == 0);
}

static unsigned check_maze_map(unsigned map) {
    bool seen[ARCADE_CELLS] = {0};
    unsigned start = arcade_maze_patrol(map, 0);
    assert(start == ARCADE_COLS + 1);
    seen[start] = true;
    for (unsigned pass = 0; pass < ARCADE_CELLS; ++pass) {
        for (int cell = 0; cell < ARCADE_CELLS; ++cell) {
            if (!seen[cell]) continue;
            const int adjacent[] = {cell - 1, cell + 1, cell - ARCADE_COLS, cell + ARCADE_COLS};
            for (unsigned i = 0; i < 4; ++i)
                if (!arcade_maze_wall(map, (unsigned)adjacent[i])) seen[adjacent[i]] = true;
        }
    }
    unsigned open = 0, power = 0;
    for (unsigned cell = 0; cell < ARCADE_CELLS; ++cell) {
        assert(seen[cell] == !arcade_maze_wall(map, cell));
        open += seen[cell];
        power += arcade_maze_power(map, cell);
    }
    assert(power == 2 && seen[7 * ARCADE_COLS + 15]);
    unsigned length = arcade_maze_patrol_length(map);
    assert(length >= 20 && arcade_maze_patrol(map, length) == start);
    for (unsigned step = 0; step < length; ++step) {
        int cell = (int)arcade_maze_patrol(map, step);
        int next = (int)arcade_maze_patrol(map, step + 1);
        assert(!arcade_maze_wall(map, (unsigned)cell));
        assert(abs(cell / ARCADE_COLS - next / ARCADE_COLS) +
               abs(cell % ARCADE_COLS - next % ARCADE_COLS) == 1);
    }
    return open - 1u;
}

static void maze_challenge(void) {
    uint32_t now = 1;
    arcade_game_t game;
    arcade_start(&game, STYLE_MAZE, now);
    arcade_maze_t *maze = &game.play.maze;
    unsigned counts[ARCADE_MAZES];
    for (unsigned map = 0; map < ARCADE_MAZES; ++map) counts[map] = check_maze_map(map);
    unsigned remaining = maze->remaining;
    assert(maze->map == 0 && remaining == counts[0]);
    arcade_press(&game, ARCADE_RIGHT, now);
    advance(&game, &now, 320);
    assert(maze->player == 2 * ARCADE_COLS + 1 && maze->remaining == remaining - 1 && game.score == 1);
    arcade_press(&game, ARCADE_OK, now);
    advance(&game, &now, 320);
    assert(maze->player == ARCADE_COLS + 1 && game.score == 1);

    maze->player = 6 * ARCADE_COLS + 1;
    maze->direction = maze->next_direction = SOUTH;
    advance(&game, &now, 320);
    assert(arcade_maze_powered(&game) && maze->remaining == remaining - 2);
    unsigned score = game.score;
    maze->ghost = maze->player;
    advance(&game, &now, 320);
    assert(game.score == score + 25 && game.lives == 3);
    maze->power_until = 0;
    maze->ghost_return_until = 0;
    maze->safe_at = 0;
    advance(&game, &now, 800);
    maze->ghost = maze->player;
    advance(&game, &now, 320);
    assert(game.lives == 2 && maze->remaining == remaining - 2);
    for (unsigned level = 1; level <= ARCADE_MAZES; ++level) {
        unsigned last = maze->player + 1u;
        memset(maze->pellets, 0, sizeof maze->pellets);
        maze->pellets[last / 8] = 1u << (last % 8);
        maze->remaining = 1;
        unsigned before = game.score;
        advance(&game, &now, 320);
        unsigned map = level % ARCADE_MAZES;
        assert(game.level == level + 1 && maze->map == map && maze->remaining == counts[map]);
        assert(game.lives == 2 && game.score == before + 51);
    }

}

static void star_challenge(void) {
    uint32_t now = 1;
    arcade_game_t game;
    arcade_start(&game, STYLE_STAR, now);
    arcade_star_t *star = &game.play.star;
    assert(arcade_press(&game, ARCADE_RIGHT, now) && star->ship_x == 108);
    assert(arcade_press(&game, ARCADE_LEFT, now) && star->ship_x == 96);
    assert(arcade_press(&game, ARCADE_OK, now) && star->player_shots[0].y == STAR_SHOT_Y);
    advance(&game, &now, 600);
    assert(game.score == 10);
    star->enemies = 1;
    star->ship_x = (uint8_t)(arcade_star_x(star, 0) + 13);
    arcade_press(&game, ARCADE_OK, now);
    advance(&game, &now, 1400);
    assert(game.level == 2 && game.score == 70);
    for (unsigned i = 0; i < 3; ++i) {
        star->fleet_y = 82;
        advance(&game, &now, 40);
    }
    assert(game.phase == ARCADE_RESULT && game.lives == 0);
    advance(&game, &now, 120000);
    assert(game.phase == ARCADE_RESULT && game.score == 70);
}

static void draw_all_games(void) {
    snake_model_t model;
    snake_reset(&model);
    receive(&model, 1, SNAKE_LAN, 140000, 40000, true);
    clip_t board = {SNAKE_BOARD_W, SNAKE_BOARD_H, 0};
    uint32_t random = 873;
    for (unsigned style = 0; style < STYLE_DIY; ++style) {
        uint32_t now = 1;
        arcade_game_t game;
        arcade_start(&game, (snake_style_t)style, now);
        board.h = style == STYLE_STACK ? STACK_FIELD_H : SNAKE_BOARD_H;
        for (unsigned i = 0; i < 2400; ++i) {
            random = random * 1664525u + 1013904223u;
            advance(&game, &now, 40);
            if (game.phase == ARCADE_RESULT && i % 30 == 0) arcade_press(&game, ARCADE_OK, now);
            else if ((random >> 28) < 4) arcade_press(&game, (arcade_key_t)((random >> 16) % 5), now);
            if (i % 8 == 0) snake_draw_play(&game, &model, NULL, now, in_bounds, &board);
        }
    }
    assert(board.drawn > 5000);
}

typedef struct {
    clip_t bounds;
    uint32_t signature;
    unsigned stack_pixels[SNAKE_PORTS];
} idle_frame_t;

static void sample_idle(void *ctx, uint32_t color, int x, int y, int w, int h) {
    idle_frame_t *frame = ctx;
    in_bounds(&frame->bounds, color, x, y, w, h);
    frame->signature = 31u * frame->signature + color + (unsigned)(x + 7 * y + 11 * w + 13 * h);
    for (unsigned p = 0; p < SNAKE_PORTS; ++p) {
        int column = 4 + (int)p * 18;
        if (y + h <= 183 && x <= column && x + w > column && color == snake_port_colors[p])
            frame->stack_pixels[p] += (unsigned)h;
    }
}

static idle_frame_t idle_frame(snake_style_t style, snake_model_t *model, uint32_t now, uint32_t phase) {
    idle_frame_t frame = {.bounds = {SNAKE_BOARD_W, style == STYLE_STACK ? STACK_FIELD_H : SNAKE_BOARD_H, 0}};
    snake_draw_style(style, model, now, phase, sample_idle, &frame);
    return frame;
}

static void idle_scenes(void) {
    snake_model_t model;
    snake_reset(&model);
    for (unsigned style = 0; style < STYLE_COUNT; ++style) {
        idle_frame_t first = idle_frame((snake_style_t)style, &model, 1, 0);
        idle_frame_t motion = idle_frame((snake_style_t)style, &model, 1, 2400);
        idle_frame_t second = idle_frame((snake_style_t)style, &model, 1, ARCADE_SCENE_MS);
        idle_frame_t third = idle_frame((snake_style_t)style, &model, 1, 2 * ARCADE_SCENE_MS);
        if (style != STYLE_DIY) assert(first.signature != motion.signature && second.signature != third.signature);
        for (uint32_t phase = 0; phase < 3 * ARCADE_SCENE_MS; phase += 120)
            idle_frame((snake_style_t)style, &model, 1, phase);
    }

    receive(&model, 100, SNAKE_LAN, 140000, 0, true);
    idle_frame_t full = idle_frame(STYLE_STACK, &model, 100, 0);
    assert(full.stack_pixels[0] == 20 * 8 && full.stack_pixels[3] == 0);
    receive(&model, 200, SNAKE_LAN, 20000, 40000, true);
    idle_frame_t before = idle_frame(STYLE_STACK, &model, 200, 0);
    receive(&model, 300, SNAKE_LAN, 10000, 50000, true);
    idle_frame_t after = idle_frame(STYLE_STACK, &model, 300, 0);
    assert(snake_total(&model.sample) == 60000);
    assert(before.stack_pixels[0] > after.stack_pixels[0] && before.stack_pixels[3] < after.stack_pixels[3]);
    idle_frame_t lost = idle_frame(STYLE_STACK, &model, 11000, 0);
    for (unsigned p = 0; p < SNAKE_PORTS; ++p) assert(lost.stack_pixels[p] == 0);
}

static void pixel_album(void) {
    uint8_t packet[DIY_FRAME_BYTES] = {0};
    memcpy(packet, "PX01", DIY_HEADER);
    packet[DIY_HEADER] = 0x20;
    packet[DIY_HEADER + 1] = 0xf8;
    assert(snake_pixels_valid(packet, sizeof packet));
    assert(!snake_pixels_valid(packet, sizeof packet - 1));
    packet[DIY_INDEX] = DIY_COLORS;
    assert(!snake_pixels_valid(packet, sizeof packet));
    packet[DIY_INDEX] = 0;
    uint16_t image[DIY_COLS * DIY_ROWS];
    for (unsigned slot = 0; slot < DIY_SLOTS; ++slot) {
        for (unsigned visible = 0; visible <= DIY_TILES; ++visible) {
            snake_pixels_decode(packet, slot, visible, image, DIY_COLS);
            unsigned count = 0;
            for (unsigned i = 0; i < DIY_COLS * DIY_ROWS; ++i) count += image[i] == 0xf820;
            assert(count == visible * 36);
        }
    }
    assert(snake_pixels_next(0, 0) == 0);
    assert(snake_pixels_next(0, 5) == 2);
    assert(snake_pixels_next(2, 5) == 0);
    assert(snake_pixels_next(2, 4) == 2);
}

static void collect_brick_row(void *context, uint32_t color, int x, int y, int w, int h) {
    uint32_t *row = context;
    assert(x >= 0 && w > 0 && x + w <= SNAKE_BOARD_W);
    if (y <= 103 && y + h > 103)
        for (int i = x; i < x + w; ++i) row[i] = color;
}

static void brick_power_relay(void) {
    snake_sample_t before = brick_replay_sample(0, 200);
    snake_sample_t after = brick_replay_sample(1, 2400);
    brick_relay_t relay = {0};
    assert(!brick_relay_observe(&relay, &before, &before, 2400));
    snake_sample_t changed = after;
    changed.port[3].mw += 2000;
    assert(!brick_relay_observe(&relay, &before, &changed, 2400));
    changed = after;
    changed.port[0].session++;
    assert(!brick_relay_observe(&relay, &before, &changed, 2400));
    changed = after;
    changed.source = SNAKE_MCP;
    assert(!brick_relay_observe(&relay, &before, &changed, 2400));
    assert(brick_relay_observe(&relay, &before, &after, 2400));
    assert(relay.from == 0 && relay.to == 3);
    assert(brick_relay_active(&relay, 5999) && !brick_relay_active(&relay, 6000));

    snake_model_t model;
    snake_reset(&model);
    snake_accept(&model, &before, 200);
    snake_accept(&model, &after, 2400);
    model.change_ms = BRICK_RELAY_MORPH_MS;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t now = 2400 + i * BRICK_RELAY_MORPH_MS / 2;
        uint32_t row[SNAKE_BOARD_W] = {0};
        snake_tick(&model, now);
        assert(snake_total(&model.sample) == 60000);
        snake_draw_bricks(&model, &relay, now, 0, collect_brick_row, row);
        unsigned yellow = 0, orange = 0;
        for (unsigned x = 0; x < SNAKE_BOARD_W; ++x) {
            if (row[x] == snake_port_colors[0]) ++yellow;
            if (row[x] == snake_port_colors[3]) ++orange;
        }
        assert(yellow == 40 - 10 * i && orange == 80 + 10 * i);
    }
}

int main(void) {
    balance();
    plug_and_unplug();
    stale_and_recover();
    adjustable_cadence();
    draw_all_positions();
    snake_challenge();
    brick_challenge();
    stack_challenge();
    maze_challenge();
    star_challenge();
    draw_all_games();
    idle_scenes();
    pixel_album();
    brick_power_relay();
    char readout[12];
    snake_power_text(readout, 30);
    assert(!strcmp(readout, "<0.1"));
    snake_power_text(readout, 140000);
    assert(!strcmp(readout, "140.0"));
    puts("Power arcade timing, scoring, relay and render bounds: PASS");
    return 0;
}
