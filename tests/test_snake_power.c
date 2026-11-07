#include "snake_power.h"
#include "snake_prefs.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint8_t legacy[] = {0x58, 2, STYLE_MAZE, 60, 1, 3}; // v0.5.4, 600 s timeout.
    snake_prefs_t prefs;
    assert(sizeof prefs == 8 && offsetof(snake_prefs_t, standby) == sizeof legacy);
    assert(snake_prefs_decode(legacy, sizeof legacy, &prefs));
    assert(prefs.sleep_seconds == 600 && prefs.style == STYLE_MAZE);
    assert(prefs.brightness == 60 && prefs.lan_seconds == 1 && prefs.mcp_seconds == 3);
    assert(prefs.standby == STANDBY_SAVE);
    prefs.standby = STANDBY_LIVE;
    snake_prefs_t restored;
    assert(snake_prefs_decode(&prefs, sizeof prefs, &restored));
    assert(restored.standby == STANDBY_LIVE);
    assert(!snake_prefs_decode(legacy, sizeof legacy - 1, &restored));
    prefs.standby = 2;
    assert(!snake_prefs_valid(&prefs));
    assert(!snake_prefs_decode(&prefs, sizeof prefs, &restored));

    assert(!snake_power_deep_due(true, SNAKE_DEEP_IDLE_MS - 1, true, true, false));
    assert(snake_power_deep_due(true, SNAKE_DEEP_IDLE_MS, true, true, false));
    assert(!snake_power_deep_due(false, SNAKE_DEEP_IDLE_MS, true, true, false));
    assert(!snake_power_deep_due(true, SNAKE_DEEP_IDLE_MS, false, true, false));
    assert(!snake_power_deep_due(true, SNAKE_DEEP_IDLE_MS, true, false, false));
    assert(!snake_power_deep_due(true, SNAKE_DEEP_IDLE_MS, true, true, true));
    puts("Standby policy and legacy settings: PASS");
}
