#include "snake_app.h"
#include "snake_net.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static void reply(const char *text) {
    usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(100));
}

static int digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool hex_text(const char *hex, char *dest, size_t capacity) {
    size_t size = strlen(hex);
    if (!size || size % 2 || size / 2 >= capacity) return false;
    for (size_t i = 0; i < size / 2; ++i) {
        int high = digit(hex[i * 2]), low = digit(hex[i * 2 + 1]);
        if (high < 0 || low < 0 || !(high | low)) return false;
        dest[i] = (char)(high * 16 + low);
    }
    return true;
}

static bool read_options(const char *input, snake_prefs_t *prefs) {
    unsigned long values[5];
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i) {
        char *end;
        values[i] = strtoul(input, &end, 10);
        if (end == input || (i == 4 ? *end != '\0' : *end != ' ')) return false;
        input = end + 1;
    }
    if (values[0] >= STYLE_COUNT || values[1] > BRIGHT_MAX || values[2] > SLEEP_MAX_SECONDS ||
        values[3] > SAMPLE_MAX_SECONDS || values[4] > SAMPLE_MAX_SECONDS) return false;
    *prefs = (snake_prefs_t){.style = (uint8_t)values[0], .brightness = (uint8_t)values[1],
        .sleep_seconds = (uint16_t)values[2], .lan_seconds = (uint8_t)values[3],
        .mcp_seconds = (uint8_t)values[4]};
    return snake_prefs_valid(prefs);
}

static void command(char *line) {
    if (strncmp(line, "PS ", 3)) return;
    char *arg = line + 3;
    char *payload = strchr(arg, ' ');
    if (payload) *payload++ = '\0';
    if (!strcmp(arg, "STATUS") && !payload) {
        char out[256];
        snake_app_status(out, sizeof out);
        reply(out);
        return;
    }
    if (!strcmp(arg, "NET") && !payload) {
        snake_net_view_t view;
        char out[120];
        if (!snake_net_view(&view)) { reply("PS ERR unavailable\n"); return; }
        snprintf(out, sizeof out, "PS NET wifi=%u mode=%u source=%u mcp=%u lan=%u saved=%u note=%u setup=%u aps=%u\n",
            view.wifi, view.mode, view.source, view.mcp_set, view.lan_ip[0] != '\0', view.saved_wifi,
            view.note, view.setup, view.ap_count);
        reply(out);
        return;
    }
    if (!strcmp(arg, "OPTIONS") && !payload) {
        snake_net_view_t view;
        char out[128];
        if (!snake_net_view(&view)) { reply("PS ERR unavailable\n"); return; }
        snake_sample_t sample;
        bool has_sample = snake_net_latest(&sample);
        snprintf(out, sizeof out, "PS OPTIONS style=%u brightness=%u sleep=%u lan=%u mcp=%u sample_at=%lu\n",
            view.prefs.style, view.prefs.brightness, view.prefs.sleep_seconds, view.prefs.lan_seconds,
            view.prefs.mcp_seconds, has_sample ? (unsigned long)sample.at : 0ul);
        reply(out);
        return;
    }
    bool accepted = false;
    if ((!strcmp(arg, "DEMO") || !strcmp(arg, "LIVE")) && !payload) {
        accepted = snake_app_press(!strcmp(arg, "DEMO") ? KEY_DEMO : KEY_LIVE);
    } else if (!strcmp(arg, "TAP") && !payload) {
        accepted = snake_app_press(KEY_OK);
    } else if (!strcmp(arg, "KEY") && payload) {
        static const struct { const char *name; input_t key; } keys[] = {
            {"up", KEY_UP}, {"down", KEY_DOWN}, {"ok", KEY_OK}, {"back", KEY_BACK},
            {"pause", KEY_UP_FAST}, {"drop", KEY_DOWN_FAST}
        };
        for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
            if (!strcmp(payload, keys[i].name)) {
                accepted = snake_app_press(keys[i].key);
                break;
            }
        }
    } else if (!strcmp(arg, "RELAY") && !payload) {
        accepted = snake_app_press(KEY_RELAY);
    } else if (!strcmp(arg, "TIME") && payload) {
        char *end;
        long long value = strtoll(payload, &end, 10);
        if (!*end && value >= 1735689600LL && value <= 4102444800LL) {
            struct timeval time = {.tv_sec = (time_t)value};
            accepted = settimeofday(&time, NULL) == 0;
        }
    } else {
        snake_net_cmd_t cmd = {0};
        bool valid = false;
        if (!strcmp(arg, "LAN") && payload) {
            unsigned port = 80;
            char *separator = strchr(payload, ' ');
            if (separator) {
                *separator++ = '\0';
                char *end;
                unsigned long number = strtoul(separator, &end, 10);
                port = *end || number > 65535 ? 0 : (unsigned)number;
            }
            if (port && (!strcmp(payload, "-") || snake_lan_ip_valid(payload))) {
                cmd.action = NET_IP;
                if (strcmp(payload, "-")) strcpy(cmd.value.ip.address, payload);
                cmd.value.ip.port = (uint16_t)port;
                valid = true;
            }
        } else if (!strcmp(arg, "MCP") && payload) {
            if (!strcmp(payload, "-") || snake_mcp_url_valid(payload)) {
                cmd.action = NET_MCP;
                if (strcmp(payload, "-")) strcpy(cmd.value.mcp_url, payload);
                valid = true;
            }
        } else if (!strcmp(arg, "MODE") && payload) {
            cmd.action = NET_MODE;
            if (!strcmp(payload, "auto")) { cmd.value.mode = MODE_AUTO; valid = true; }
            else if (!strcmp(payload, "lan")) { cmd.value.mode = MODE_LAN; valid = true; }
            else if (!strcmp(payload, "mcp")) { cmd.value.mode = MODE_MCP; valid = true; }
        } else if (!strcmp(arg, "WIFI") && payload) {
            char *pass = strchr(payload, ' ');
            if (pass) {
                *pass++ = '\0';
                cmd.action = NET_JOIN;
                valid = hex_text(payload, cmd.value.join.ssid, sizeof cmd.value.join.ssid) &&
                    (!strcmp(pass, "-") || hex_text(pass, cmd.value.join.password, sizeof cmd.value.join.password));
            }
        } else if (!strcmp(arg, "OPT") && payload) {
            cmd.action = NET_PREFS;
            valid = read_options(payload, &cmd.value.prefs);
        }
        if (valid) accepted = snake_net_send(&cmd);
        explicit_bzero(&cmd, sizeof cmd);
    }
    reply(accepted ? "PS OK\n" : "PS ERR invalid or busy\n");
}

static void worker(void *unused) {
    (void)unused;
    char line[280];
    size_t count = 0;
    bool over = false;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            if (!over) {
                line[count] = '\0';
                command(line);
            }
            explicit_bzero(line, sizeof line);
            count = 0;
            over = false;
        } else if (!over && c >= 32 && c < 127 && count + 1 < sizeof line) {
            line[count++] = (char)c;
        } else over = true;
    }
}

void snake_usb_start(void) {
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 1024, .rx_buffer_size = 512};
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) return;
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(worker, "snake-usb", 3400, NULL, 3, NULL) != pdPASS)
        ESP_LOGW("candypin", "USB commands unavailable");
}
