#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum { KEY_UP, KEY_DOWN, KEY_OK, KEY_BACK, KEY_UP_FAST, KEY_DOWN_FAST,
               KEY_DEMO, KEY_LIVE, KEY_RELAY } input_t;

bool snake_app_press(input_t key);
void snake_app_status(char *message, size_t capacity);
void snake_usb_start(void);
