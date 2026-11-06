#include "snake_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const uint32_t snake_port_colors[SNAKE_PORTS] = {0xffc444, 0xffa51e, 0xff921e, 0xff6918, 0x2c89fb};

static const uint32_t PAPER = 0xfff0d7;
static const uint32_t QUIET = 0xb7a58e;
static const uint32_t DIM = 0x665544;

static snake_point_t body_point(int head, int distance) {
    int offset = distance % SNAKE_CELL;
    int index = head - distance / SNAKE_CELL;
    snake_point_t a = snake_track(index), b = snake_track(index - 1);
    return (snake_point_t){a.x + (b.x - a.x) * offset / SNAKE_CELL,
                           a.y + (b.y - a.y) * offset / SNAKE_CELL};
}

static snake_point_t direction(int head, int distance) {
    int index = head - distance / SNAKE_CELL;
    snake_point_t a = snake_track(index), b = snake_track(index - 1);
    return (snake_point_t){(b.x - a.x) / SNAKE_CELL, (b.y - a.y) / SNAKE_CELL};
}

static int smaller(int a, int b) { return a < b ? a : b; }

static void strip(int head, int start, int end, uint32_t rgb, snake_rect_fn rect, void *ctx) {
    int distance = start;
    while (distance < end) {
        snake_point_t a = body_point(head, distance);
        snake_point_t d = direction(head, distance);
        int stop = distance + SNAKE_CELL - distance % SNAKE_CELL;
        while (stop < end) {
            snake_point_t next = direction(head, stop);
            if (next.x != d.x || next.y != d.y) break;
            stop += SNAKE_CELL;
        }
        if (stop > end) stop = end;
        snake_point_t b = body_point(head, stop);
        if (d.x) rect(ctx, rgb, smaller(a.x, b.x), a.y - 4, stop - distance, 8);
        else rect(ctx, rgb, a.x - 4, smaller(a.y, b.y), 8, stop - distance);
        if (stop < end) rect(ctx, rgb, b.x - 4, b.y - 4, 8, 8);
        distance = stop;
    }
    if (start % SNAKE_CELL == 0) {
        snake_point_t before = direction(head, start - 1), after = direction(head, start);
        if (before.x != after.x || before.y != after.y) {
            snake_point_t p = body_point(head, start);
            rect(ctx, rgb, p.x - 4, p.y - 4, 8, 8);
        }
    }
}

static uint32_t darken(uint32_t rgb, unsigned percent) {
    return ((((rgb >> 16) & 255u) * percent / 100u) << 16)
        | ((((rgb >> 8) & 255u) * percent / 100u) << 8)
        | ((rgb & 255u) * percent / 100u);
}

static void paint_body(const snake_model_t *m, bool fresh, snake_rect_fn rect, void *ctx) {
    int start = SNAKE_CELL / 2;
    float distance = (float)start;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        distance += m->body[i];
        int end = (int)lroundf(distance);
        if (end == start) continue;
        uint32_t rgb = snake_port_colors[i];
        strip(m->head, start, end, fresh ? rgb : darken(rgb, 37), rect, ctx);
        if (fresh) {
            uint32_t mark = darken(rgb, 79);
            for (int d = ((start + 2 + SNAKE_CELL - 1) / SNAKE_CELL) * SNAKE_CELL;
                 d + 2 <= end; d += SNAKE_CELL) {
                snake_point_t p = body_point(m->head, d);
                rect(ctx, mark, p.x + (i == 0 ? 0 : -1), p.y + (i % 2 ? -1 : 0), 1, 1);
                if (i != 0) rect(ctx, mark, p.x + 1, p.y + (i % 2 ? 1 : 0), 1, 1);
            }
        }
        start = end;
    }
}

static void head_rect(snake_point_t p, snake_point_t d, uint32_t rgb, int x, int y, int w, int h,
                      snake_rect_fn rect, void *ctx) {
    int a = p.x + d.x * x - d.y * y;
    int b = p.y + d.y * x + d.x * y;
    int c = a + d.x * (w - 1) - d.y * (h - 1);
    int e = b + d.y * (w - 1) + d.x * (h - 1);
    rect(ctx, rgb, smaller(a, c), smaller(b, e), d.x ? w : h, d.x ? h : w);
}

static void paint_head(const snake_model_t *m, bool fresh, snake_rect_fn rect, void *ctx) {
    snake_point_t p = snake_track(m->head), next = snake_track(m->head + 1);
    snake_point_t d = {(next.x - p.x) / SNAKE_CELL, (next.y - p.y) / SNAKE_CELL};
    bool resting = fresh && m->zero_streak >= 2 && !m->moving;
    bool open = fresh && m->pellet_ports && (m->head + 1) % SNAKE_TRACK == m->pellet;
    head_rect(p,d,fresh ? 0xbc9360 : DIM,-6,-4,12,8,rect,ctx);
    head_rect(p,d,fresh ? 0xf1d6a4 : DIM,-4,-6,8,12,rect,ctx);
    head_rect(p,d,fresh ? 0xfff0d3 : 0x796652,-5,-4,10,7,rect,ctx);
    head_rect(p,d,0x39291e,0,-4,2,resting ? 1 : 2,rect,ctx);
    head_rect(p,d,0x39291e,0,2,2,resting ? 1 : 2,rect,ctx);
    head_rect(p,d,0x39291e,open ? 3 : 4,open ? -1 : 0,open ? 3 : 2,open ? 2 : 1,rect,ctx);
}

void snake_draw_game(const snake_model_t *m, uint32_t now, snake_rect_fn rect, void *ctx) {
    snake_freshness_t freshness = snake_freshness(m, now);
    bool fresh = freshness == SNAKE_FRESH;
    paint_body(m, fresh, rect, ctx);
    if (fresh && m->pellet_ports) {
        snake_point_t p = snake_track(m->pellet);
        rect(ctx, 0xf2dbad, p.x - 3, p.y - 5, 6, 10);
        rect(ctx, 0xf2dbad, p.x - 5, p.y - 3, 10, 6);
        int n = __builtin_popcount((unsigned)m->pellet_ports), stripe = 0;
        for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
            if (!(m->pellet_ports & (1u << i))) continue;
            int x = stripe * 8 / n;
            int next = ++stripe * 8 / n;
            rect(ctx, snake_port_colors[i], p.x - 4 + x, p.y - 3, next - x, 6);
        }
        if ((now / 100) % 2 == 0) rect(ctx, 0xfff8d4, p.x + 4, p.y - 5, 2, 2);
    }
    paint_head(m, fresh, rect, ctx);
    if (fresh && m->sparkling) {
        snake_point_t p = snake_track(m->head);
        int span = 5 + (int)(now - m->spark_at) * 6 / 120;
        rect(ctx, PAPER, p.x - span, p.y - span, 2, 2);
        rect(ctx, snake_port_colors[m->spark_color], p.x + span, p.y - span, 2, 2);
        rect(ctx, snake_port_colors[m->spark_color], p.x - span, p.y + span, 2, 2);
        rect(ctx, PAPER, p.x + span, p.y + span, 2, 2);
    }
    if (fresh && m->zero_streak >= 2 && !m->moving && now % 4000 < 900) {
        snake_point_t p = snake_track(m->head);
        rect(ctx, QUIET, p.x+8,p.y-10,5,1);
        rect(ctx, QUIET, p.x+10,p.y-9,2,2);
        rect(ctx, QUIET, p.x+8,p.y-7,5,1);
    }
}

typedef struct { int w; uint8_t row[7]; } glyph_t;
static const glyph_t glyph[] = {
    {5,{14,27,27,27,27,27,14}}, {5,{4,12,4,4,4,4,14}}, {5,{14,27,3,6,12,24,31}},
    {5,{30,3,3,14,3,3,30}}, {5,{6,14,30,22,31,6,6}}, {5,{31,24,24,30,3,27,14}},
    {5,{14,24,24,30,27,27,14}}, {5,{31,3,6,12,12,12,12}},
    {5,{14,27,27,14,27,27,14}}, {5,{14,27,27,15,3,3,14}}, {2,{0,0,0,0,0,3,3}},
    {4,{0,3,6,12,6,3,0}}, {5,{0,0,0,31,0,0,0}}, {5,{17,17,17,21,21,21,10}}
};

static const glyph_t *glyph_for(char c) {
    if (c >= '0' && c <= '9') return &glyph[c - '0'];
    if (c == '.') return &glyph[10];
    if (c == '<') return &glyph[11];
    if (c == 'W') return &glyph[13];
    return &glyph[12];
}

static void draw_glyph(const glyph_t *g, uint32_t rgb, int x, int y, int scale, snake_rect_fn rect, void *ctx) {
    for (int r = 0; r < 7; ++r) {
        for (int col = 0; col < g->w;) {
            int start = col;
            while (col < g->w && (g->row[r] & (1u << (g->w - 1 - col)))) ++col;
            if (col > start) rect(ctx, rgb, x + start * scale, y + r * scale, (col - start) * scale, scale);
            else ++col;
        }
    }
}

void snake_draw_watts(const snake_model_t *m, uint32_t now, snake_rect_fn rect, void *ctx) {
    snake_freshness_t freshness = snake_freshness(m, now);
    char digits[12];
    if (freshness == SNAKE_LOST || freshness == SNAKE_EMPTY) strcpy(digits, "-");
    else snake_power_text(digits, snake_total(&m->sample));
    int scale = strlen(digits) > 4 ? 5 : 6;
    int width = -1;
    for (const char *p = digits; *p; ++p) width += glyph_for(*p)->w + 1;
    int x = (240 - width * scale - 20) / 2;
    for (const char *p = digits; *p; ++p) {
        const glyph_t *g = glyph_for(*p);
        draw_glyph(g, freshness == SNAKE_FRESH ? PAPER : QUIET, x, 42 + (6 - scale) * 7, scale, rect, ctx);
        x += (g->w + 1) * scale;
    }
    draw_glyph(glyph_for('W'), 0xd9b785, x - scale + 10, 70, 2, rect, ctx);
}
