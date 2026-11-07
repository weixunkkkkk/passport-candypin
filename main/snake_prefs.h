#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum { STYLE_SNAKE, STYLE_BRICKS, STYLE_STACK, STYLE_MAZE, STYLE_STAR, STYLE_DIY, STYLE_COUNT } snake_style_t;
typedef enum { STANDBY_SAVE, STANDBY_LIVE } snake_standby_t;

enum {
    LAN_MIN_SECONDS = 1, MCP_MIN_SECONDS = 3, SAMPLE_MAX_SECONDS = 60,
    BRIGHT_MIN = 10, BRIGHT_MAX = 100, SLEEP_MAX_SECONDS = 1800
};

typedef struct {
    uint16_t sleep_seconds;
    uint8_t style;
    uint8_t brightness;
    uint8_t lan_seconds;
    uint8_t mcp_seconds;
    uint16_t standby;
} snake_prefs_t;

static inline snake_prefs_t snake_prefs_default(void) {
    return (snake_prefs_t){.sleep_seconds = 120, .style = STYLE_SNAKE, .brightness = 60,
                           .lan_seconds = 1, .mcp_seconds = 3};
}

static inline bool snake_prefs_valid(const snake_prefs_t *p) {
    return p->style < STYLE_COUNT && p->brightness >= BRIGHT_MIN && p->brightness <= BRIGHT_MAX &&
        p->sleep_seconds <= SLEEP_MAX_SECONDS && p->lan_seconds >= LAN_MIN_SECONDS &&
        p->lan_seconds <= SAMPLE_MAX_SECONDS && p->mcp_seconds >= MCP_MIN_SECONDS &&
        p->mcp_seconds <= SAMPLE_MAX_SECONDS && p->standby <= STANDBY_LIVE;
}

static inline bool snake_prefs_decode(const void *data, size_t size, snake_prefs_t *out) {
    snake_prefs_t prefs = snake_prefs_default();
    if (size != offsetof(snake_prefs_t, standby) && size != sizeof prefs) return false;
    memcpy(&prefs, data, size);
    if (!snake_prefs_valid(&prefs)) return false;
    *out = prefs;
    return true;
}
