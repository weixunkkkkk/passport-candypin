#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { SNAKE_PORTS = 5, SNAKE_TRACK = 58, SNAKE_CELL = 12 };
typedef enum { SNAKE_NONE, SNAKE_LAN, SNAKE_MCP, SNAKE_DEMO } snake_source_t;
typedef enum { SNAKE_EMPTY, SNAKE_FRESH, SNAKE_WAIT, SNAKE_LOST } snake_freshness_t;

/* Render order is C1, C2, C3, C4, A regardless of wire order. */
typedef struct {
    uint32_t mw;
    uint32_t session;
    bool attached;
} snake_port_t;

typedef struct {
    snake_port_t port[SNAKE_PORTS];
    snake_source_t source;
    uint32_t at;
    uint32_t interval_ms;
} snake_sample_t;

typedef struct {
    uint8_t idle_streak;
    bool armed;
    bool pulsing;
    uint32_t pulse_at;
} snake_port_anim_t;

typedef struct {
    snake_sample_t sample;
    snake_port_anim_t port[SNAKE_PORTS];
    float body[SNAKE_PORTS];
    float from[SNAKE_PORTS];
    float to[SNAKE_PORTS];
    uint32_t change_at;
    uint16_t change_ms;
    uint32_t step_at;
    uint32_t spark_at;
    uint8_t head;
    uint8_t pellet;
    uint8_t pellet_ports;
    uint8_t spark_color;
    uint8_t zero_streak;
    bool moving;
    bool sparkling;
    bool has_sample;
    bool held;
} snake_model_t;

typedef struct { int x; int y; } snake_point_t;

void snake_reset(snake_model_t *model);
void snake_accept(snake_model_t *model, const snake_sample_t *sample, uint32_t now);
void snake_tick(snake_model_t *model, uint32_t now);
uint32_t snake_total(const snake_sample_t *sample);
snake_freshness_t snake_freshness(const snake_model_t *model, uint32_t now);
snake_point_t snake_track(int index);
bool snake_port_idle(const snake_port_t *port);
void snake_power_text(char text[12], uint32_t mw);
