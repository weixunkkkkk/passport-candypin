#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { SNAKE_DEEP_IDLE_MS = 60000 };

static inline bool snake_power_deep_due(bool dark, uint32_t elapsed, bool save, bool suspended, bool usb) {
    return dark && elapsed >= SNAKE_DEEP_IDLE_MS && save && suspended && !usb;
}

void snake_power_init(void);
void snake_power_screen_off(bool off);
void snake_power_poll(void);
