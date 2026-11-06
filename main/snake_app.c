#include "snake_app.h"
#include "snake_album.h"
#include "snake_arcade.h"
#include "snake_art.h"
#include "snake_net.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(snake_font);

typedef enum { CARD, MENU, MODES, SCAN, PASSWORD, CHARACTERS, ADDRESS, JOINING, HOTSPOT,
               STYLES, PREFS, ADJUST, CONNECTIONS, SAMPLING, DEMOS } page_t;
static const uint32_t INK = 0x191612, PAPER = 0xfff0d7, QUIET = 0xb7a58e;
static const uint32_t ACCENT = 0xffc444, RULE = 0x3d3226;
static const char *const mode_names[] = {"自动", "局域网", "MCP"};
static const char *const port_names[] = {"C1", "C2", "C3", "C4", "A"};
static const char *const style_names[] = {"充电蛇", "跳砖吃币", "叠方块", "迷宫吃豆", "星舰防线", "DIY 像素画"};
static const char *const controls[] = {
    "上左转 下右转 · OK 暂停", "亮时按 OK 跳 · 长按上暂停", "上左 下右 · OK 转 · 长下速落",
    "上左转 下右转 · OK 掉头", "上左 下右 · OK 开火"
};
static const page_t categories[] = {CONNECTIONS, PREFS, STYLES, DEMOS};
static const char *const category_names[] = {"联网与读数", "屏幕与休眠", "卡片与照片", "玩法演示"};
static const char *const connection_names[] = {"数据来源", "扫描 Wi-Fi", "热点设置", "局域网地址", "采样频率"};
static const char *const demo_names[] = {"插拔与功率变化", "60 W 功率接力"};
static const char *const maze_names[ARCADE_MAZES] = {"糖霜花园", "薄荷回廊", "莓果仓库"};
static const char *const pref_names[] = {"屏幕亮度", "息屏时间", "局域网采样", "MCP 采样"};
static const uint16_t sleep_choices[] = {0, 15, 30, 60, 120, 180, 300, 600, 1800};
static const char *const alphabets[] = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789", " !\"#$%&'()*+,-./:;<=>?@[\\]^_" "\x60" "{|}~"};
static const char *const group_names[] = {"小写 a-z", "大写 A-Z", "数字 0-9", "符号", "删除一位", "连接 Wi-Fi", "返回"};

static QueueHandle_t inputs;
static snake_net_view_t net;
static snake_model_t model;
static arcade_game_t game;
static bool game_dirty;
static uint32_t best[STYLE_COUNT];
static brick_relay_t brick_relay;
static page_t page;
static unsigned cursor, group;
static char ssid[33], secret[65], address[16];
static bool protected_wifi, hotspot_diy;
static page_t hotspot_return = CONNECTIONS;
static page_t adjust_return;
static const char *error_text;
static uint32_t join_revision;
static uint32_t settings_revision;
static bool dirty_settings = true;
static bool demo;
static bool relay_demo;
static uint32_t demo_at;
static unsigned demo_step;
static snake_prefs_t prefs, edit;
static bool prefs_pending;
static unsigned editing;
static bool screen_dark;
static uint32_t last_input, last_motion, idle_since, art_phase;
static uint32_t card_now, shown_round_age;
static int battery_pct = -1;
static uint32_t shown_watts;
static snake_source_t shown_source;
static unsigned drawn_frames;
static uint8_t shown_style;
static bool shown_dark;
static bool fast_card_input;
static uint8_t shown_round, shown_lives;
static uint16_t shown_level;
static uint32_t shown_score;

// A duplicated edge keeps LVGL's pixel-center sampling aligned to the 4x artwork grid.
enum { DIY_PAD_W = DIY_COLS + 2, DIY_PAD_H = DIY_ROWS + 2 };
static uint16_t diy_pixels[DIY_PAD_H * DIY_PAD_W];
static unsigned diy_slot, diy_shown_slot = DIY_SLOTS, diy_revealed;
static uint32_t diy_revision, diy_reveal_at;
static bool diy_ready;
static const lv_image_dsc_t diy_image = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = DIY_PAD_W, .h = DIY_PAD_H, .stride = DIY_PAD_W * 2},
    .data_size = sizeof diy_pixels, .data = (const uint8_t *)diy_pixels,
};
static bool shown_relay;

static lv_obj_t *card, *settings, *field, *hero, *board_rule, *card_title, *diy_view;
static lv_obj_t *route_label, *battery_label, *legend, *game_hud, *game_detail, *card_action, *card_action_key;
static lv_obj_t *stack_lines;
static lv_obj_t *port_dot[SNAKE_PORTS], *port_tag[SNAKE_PORTS], *port_power[SNAKE_PORTS];
static lv_obj_t *port_state[SNAKE_PORTS], *port_focus[SNAKE_PORTS];
static lv_obj_t *title, *line1, *line2, *footer, *settings_rule, *gauge, *gauge_fill;
enum { ROWS = 6 };
static lv_obj_t *row_bg[ROWS], *row_label[ROWS], *row_detail[ROWS], *row_arrow[ROWS];

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static snake_style_t card_style(void) {
    if (relay_demo) return STYLE_BRICKS;
    return demo && prefs.style == STYLE_DIY ? STYLE_SNAKE : (snake_style_t)prefs.style;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opacity) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opacity, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, uint32_t color, lv_text_align_t align) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_style_text_font(obj, &snake_font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, "");
    return obj;
}

static void text(lv_obj_t *obj, const char *value) {
    if (strcmp(lv_label_get_text(obj), value)) lv_label_set_text(obj, value);
}

static void visible(lv_obj_t *obj, bool on) {
    if (on) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

typedef struct { lv_layer_t *layer; int x; int y; } draw_area_t;

static void rect(void *context, uint32_t rgb, int x, int y, int w, int h) {
    draw_area_t *a = context;
    lv_area_t area = {a->x + x, a->y + y, a->x + x + w - 1, a->y + y + h - 1};
    if (area.x2 < a->layer->_clip_area.x1 || area.x1 > a->layer->_clip_area.x2 ||
        area.y2 < a->layer->_clip_area.y1 || area.y1 > a->layer->_clip_area.y2) return;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.opa = LV_OPA_COVER;
    fill.color = lv_color_hex(rgb);
    lv_draw_fill(a->layer, &fill, &area);
}

static void draw(lv_event_t *event) {
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    draw_area_t area = {lv_event_get_layer(event), coords.x1, coords.y1};
    if (obj == hero) {
        // The digit layout uses screen coordinates. Hero clips to its own y=40..88 band.
        area.y -= 40;
        snake_draw_watts(&model, card_now, rect, &area);
    } else {
        if (game.phase != ARCADE_IDLE)
            snake_draw_play(&game, &model, &brick_relay, card_now, rect, &area);
        else if (card_style() == STYLE_BRICKS)
            snake_draw_bricks(&model, &brick_relay, card_now, art_phase, rect, &area);
        else snake_draw_style(card_style(), &model, card_now, art_phase, rect, &area);
    }
}

static bool update_album(uint32_t now, bool entering) {
    uint32_t revision = snake_album_revision();
    unsigned mask = snake_album_mask();
    if (revision != diy_revision && revision) diy_slot = snake_album_latest();
    if (!(mask & (1u << diy_slot))) diy_slot = snake_pixels_next(diy_slot, mask);
    if (entering) diy_shown_slot = DIY_SLOTS;
    bool reload = revision != diy_revision || diy_slot != diy_shown_slot;
    unsigned reveal = reload ? 1u : 1u + (now - diy_reveal_at) / 36u;
    if (reveal > DIY_TILES) reveal = DIY_TILES;
    if (!reload && (!diy_ready || reveal == diy_revealed)) return false;
    snake_album_view_t view = snake_album_decode(diy_slot, reveal, diy_pixels + DIY_PAD_W + 1, DIY_PAD_W);
    if (view == DIY_BUSY) return false;
    diy_ready = view == DIY_READY;
    if (diy_ready) {
        for (unsigned row = 1; row <= DIY_ROWS; ++row) {
            uint16_t *line = diy_pixels + row * DIY_PAD_W;
            line[0] = line[1];
            line[DIY_PAD_W - 1] = line[DIY_COLS];
        }
        const size_t row_bytes = DIY_PAD_W * sizeof *diy_pixels;
        memcpy(diy_pixels, diy_pixels + DIY_PAD_W, row_bytes);
        memcpy(diy_pixels + (DIY_PAD_H - 1) * DIY_PAD_W, diy_pixels + DIY_ROWS * DIY_PAD_W, row_bytes);
    }
    diy_revision = revision;
    diy_shown_slot = diy_slot;
    diy_revealed = reveal;
    if (reload) diy_reveal_at = now;
    lv_image_cache_drop(&diy_image);
    return true;
}

static void create_ui(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(INK), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    card = box(root, 0, 0, 240, 320, INK, LV_OPA_COVER);
    box(card, 21, 22, 3, 3, ACCENT, LV_OPA_COVER);
    box(card, 24, 19, 3, 3, ACCENT, LV_OPA_COVER);
    box(card, 27, 22, 3, 3, ACCENT, LV_OPA_COVER);
    card_title = label(card, 36, 15, 92, PAPER, LV_TEXT_ALIGN_LEFT);
    route_label = label(card, 115, 15, 70, QUIET, LV_TEXT_ALIGN_RIGHT);
    battery_label = label(card, 188, 15, 31, QUIET, LV_TEXT_ALIGN_RIGHT);
    hero = box(card, 0, 40, 240, 49, INK, LV_OPA_TRANSP);
    lv_obj_add_event_cb(hero, draw, LV_EVENT_DRAW_MAIN, NULL);
    box(card, 24, 94, 192, 1, RULE, LV_OPA_COVER);
    field = box(card, 24, 98, SNAKE_BOARD_W, SNAKE_BOARD_H, INK, LV_OPA_TRANSP);
    lv_obj_add_event_cb(field, draw, LV_EVENT_DRAW_MAIN, NULL);
    diy_view = lv_image_create(field);
    lv_obj_remove_flag(diy_view, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(diy_view, &diy_image);
    lv_obj_set_pos(diy_view, -2, -2);
    lv_image_set_pivot(diy_view, 0, 0);
    lv_image_set_scale(diy_view, 4 * LV_SCALE_NONE);
    lv_image_set_antialias(diy_view, false);
    visible(diy_view, false);
    game_hud = label(field, 4, 3, SNAKE_BOARD_W - 8, PAPER, LV_TEXT_ALIGN_CENTER);
    game_detail = label(field, 4, 65, SNAKE_BOARD_W - 8, QUIET, LV_TEXT_ALIGN_CENTER);
    visible(game_hud, false);
    visible(game_detail, false);
    stack_lines = label(field, STACK_SIDE_X + 2, 63, SNAKE_BOARD_W - STACK_SIDE_X - 4, QUIET, LV_TEXT_ALIGN_CENTER);
    visible(stack_lines, false);
    board_rule = box(card, 24, 222, 192, 1, RULE, LV_OPA_COVER);
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        int x = 24 + (int)(i % 3) * 65, y = 229 + (int)(i / 3) * 31;
        port_focus[i] = box(card, x - 2, y, 61, 27, 0x352719, LV_OPA_COVER);
        port_dot[i] = box(card, x + 2, y + 5, 5, 5, RULE, LV_OPA_COVER);
        port_tag[i] = label(card, x + 10, y, 21, QUIET, LV_TEXT_ALIGN_LEFT);
        text(port_tag[i], port_names[i]);
        port_state[i] = label(card, x + 28, y, 34, QUIET, LV_TEXT_ALIGN_LEFT);
        port_power[i] = label(card, x + 2, y + 13, 60, PAPER, LV_TEXT_ALIGN_LEFT);
    }
    box(card, 33, 296, 174, 1, RULE, LV_OPA_COVER);
    card_action_key = label(card, 153, 258, 62, QUIET, LV_TEXT_ALIGN_CENTER);
    card_action = label(card, 153, 273, 62, PAPER, LV_TEXT_ALIGN_CENTER);
    legend = label(card, 15, 295, 210, QUIET, LV_TEXT_ALIGN_CENTER);

    settings = box(root, 0, 0, 240, 320, INK, LV_OPA_COVER);
    box(settings, 19, 24, 5, 5, ACCENT, LV_OPA_COVER);
    title = label(settings, 31, 14, 188, PAPER, LV_TEXT_ALIGN_LEFT);
    line1 = label(settings, 19, 38, 202, QUIET, LV_TEXT_ALIGN_LEFT);
    line2 = label(settings, 19, 55, 202, QUIET, LV_TEXT_ALIGN_LEFT);
    footer = label(settings, 8, 295, 224, QUIET, LV_TEXT_ALIGN_CENTER);
    settings_rule = box(settings, 19, 78, 202, 1, RULE, LV_OPA_COVER);
    for (unsigned i = 0; i < ROWS; ++i) {
        row_bg[i] = box(settings, 18, 84 + (int)i * 33, 204, 29, RULE, LV_OPA_COVER);
        lv_obj_set_style_radius(row_bg[i], 6, 0);
        lv_obj_set_style_border_width(row_bg[i], 1, 0);
        row_label[i] = label(row_bg[i], 12, 1, 175, PAPER, LV_TEXT_ALIGN_LEFT);
        row_detail[i] = label(row_bg[i], 12, 19, 164, QUIET, LV_TEXT_ALIGN_LEFT);
        row_arrow[i] = label(row_bg[i], 179, 11, 12, ACCENT, LV_TEXT_ALIGN_CENTER);
        text(row_arrow[i], ">");
    }
    gauge = box(settings, 43, 152, 154, 8, RULE, LV_OPA_COVER);
    gauge_fill = box(gauge, 0, 0, 154, 8, ACCENT, LV_OPA_COVER);
    visible(gauge, false);
    visible(settings, false);
}

static void safe_ssid(char out[40], const char *input) {
    size_t written = 0;
    const uint8_t *p = (const uint8_t *)input;
    while (*p && written < 36) {
        size_t n = 1;
        uint32_t cp = *p;
        if ((p[0] & 0xe0) == 0xc0) { n = 2; cp = *p & 0x1f; }
        else if ((p[0] & 0xf0) == 0xe0) { n = 3; cp = *p & 0x0f; }
        else if ((p[0] & 0xf8) == 0xf0) { n = 4; cp = *p & 7; }
        else if (*p >= 0x80) cp = 0;
        size_t i;
        for (i = 1; i < n && p[i] && (p[i] & 0xc0) == 0x80; ++i)
            cp = (cp << 6) | (p[i] & 0x3f);
        if (i != n) { n = i; cp = 0; }
        lv_font_glyph_dsc_t glyph_info;
        if (cp >= 32 && snake_font.get_glyph_dsc(&snake_font, &glyph_info, cp, 0)) {
            memcpy(out + written, p, n);
            written += n;
        } else out[written++] = '?';
        p += n;
    }
    out[written] = '\0';
}

static void show_page(page_t next) {
    if (page == CARD && next != CARD) game.phase = ARCADE_IDLE;
    page = next;
    cursor = 0;
    error_text = NULL;
    dirty_settings = true;
    visible(card, page == CARD);
    visible(settings, page != CARD);
    __atomic_store_n(&settings_revision, (uint32_t)page, __ATOMIC_RELAXED);
}

static unsigned options(void) {
    switch (page) {
        case MENU: return sizeof categories / sizeof categories[0];
        case CONNECTIONS: return sizeof connection_names / sizeof connection_names[0] + 1u;
        case DEMOS: return sizeof demo_names / sizeof demo_names[0];
        case MODES: return 3;
        case SCAN: return net.scanning ? 0 : net.ap_count + 1;
        case PASSWORD: return sizeof group_names / sizeof group_names[0];
        case CHARACTERS: return strlen(alphabets[group]);
        case ADDRESS: return 15;
        case HOTSPOT: return 3;
        case STYLES: return STYLE_COUNT;
        case PREFS: case SAMPLING: return 2;
        default: return 0;
    }
}

static void pref_value(char value[24], const snake_prefs_t *settings, unsigned field) {
    switch (field) {
        case 0: snprintf(value, 24, "%u%%", (unsigned)settings->brightness); break;
        case 1:
            if (settings->sleep_seconds == 0) strcpy(value, "常亮");
            else if (settings->sleep_seconds % 60 == 0)
                snprintf(value, 24, "%u 分钟", settings->sleep_seconds / 60u);
            else snprintf(value, 24, "%u 秒", (unsigned)settings->sleep_seconds);
            break;
        case 2: snprintf(value, 24, "%u 秒", (unsigned)settings->lan_seconds); break;
        default: snprintf(value, 24, "%u 秒", (unsigned)settings->mcp_seconds); break;
    }
}

static void option_text(char buf[80], unsigned i) {
    switch (page) {
        case MENU: strcpy(buf, category_names[i]); break;
        case CONNECTIONS:
            if (i == 0) snprintf(buf, 80, "数据来源  %s", mode_names[net.mode]);
            else if (i < sizeof connection_names / sizeof connection_names[0]) strcpy(buf, connection_names[i]);
            else strcpy(buf, net.wifi == WIFI_READY ? "断开 Wi-Fi" : "重连 Wi-Fi");
            break;
        case DEMOS: strcpy(buf, demo_names[i]); break;
        case MODES: snprintf(buf, 80, "%s%s", mode_names[i], i == net.mode ? "  · 已选" : ""); break;
        case SCAN:
            if (i == net.ap_count) strcpy(buf, "重新扫描");
            else {
                const snake_ap_t *ap = &net.ap[i];
                char name[40];
                safe_ssid(name, ap->ssid);
                const char *flag = !ap->supported ? "不支持" :
                    net.wifi == WIFI_READY && !strcmp(ap->ssid, net.ssid) ? "已连接" :
                    ap->secure ? "加密" : "开放";
                snprintf(buf, 80, "%s  %s", name, flag);
            }
            break;
        case PASSWORD: snprintf(buf, 80, "%s", group_names[i]); break;
        case CHARACTERS:
            if (alphabets[group][i] == ' ') strcpy(buf, "空格");
            else snprintf(buf, 80, "%c", alphabets[group][i]);
            break;
        case ADDRESS:
            if (i <= 10) snprintf(buf, 80, "%c", i == 10 ? '.' : '0' + (int)i);
            else snprintf(buf, 80, "%s", i == 11 ? "删除一位" : i == 12 ? "清空" : i == 13 ? "保存" : "返回");
            break;
        case HOTSPOT:
            if (i == 0) strcpy(buf, "手机连接上方热点");
            else if (i == 1) strcpy(buf, hotspot_diy ? "192.168.4.1/diy" : "192.168.4.1");
            else strcpy(buf, hotspot_diy ? "选照片 · 预览 · 保存" :
                        net.setup_joined ? "Wi-Fi 已连接" : "选 Wi-Fi 或填写 MCP 地址");
            break;
        case STYLES: snprintf(buf, 80, "%s%s", style_names[i], i == prefs.style ? "  · 已选" : ""); break;
        case PREFS: case SAMPLING: strcpy(buf, pref_names[i + (page == SAMPLING ? 2u : 0u)]); break;
        default: buf[0] = '\0'; break;
    }
}

static const char *source_name(snake_source_t source) {
    return source == SNAKE_LAN ? "局域网" : source == SNAKE_MCP ? "MCP" : source == SNAKE_DEMO ? "演示" : "等待读数";
}

static void category_status(char buf[80], unsigned index) {
    char sleep[24];
    switch (index) {
        case 0:
            snprintf(buf, 80, "%s · %s", net.wifi == WIFI_READY ? "Wi-Fi 已连接" : "Wi-Fi 未连接", mode_names[net.mode]);
            break;
        case 1:
            pref_value(sleep, &prefs, 1);
            snprintf(buf, 80, "亮度 %u%% · %s", prefs.brightness, sleep);
            break;
        case 2: snprintf(buf, 80, "当前  %s", style_names[prefs.style]); break;
        default: strcpy(buf, "插拔变化 · 60 W 接力"); break;
    }
}

static void paint_settings(void) {
    const char *heading = "CandyPin 设置", *hint = "上下选 · OK 确认 · 长按 OK 返回";
    char one[80] = "", two[80] = "", name[40];
    switch (page) {
        case MENU:
            strcpy(one, "上下选择 · 短按 OK 进入");
            hint = "长按 OK 返回功率卡";
            if (net.note == NET_NOTE_SAVE_FAILED) strcpy(two, "保存失败");
            break;
        case CONNECTIONS:
            heading = "联网与读数";
            safe_ssid(name, net.ssid);
            snprintf(one, sizeof one, "Wi-Fi  %s", net.wifi == WIFI_READY ? name : "未连接");
            snprintf(two, sizeof two, "读数  %s", source_name(net.source));
            break;
        case DEMOS:
            heading = "玩法演示";
            strcpy(one, "观察瓦数和端口颜色的变化");
            break;
        case MODES:
            heading = "数据模式";
            strcpy(one, "自动优先局域网，失败转 MCP");
            snprintf(two, sizeof two, "MCP %s", net.mcp_set ? "已配置" : "待配置");
            break;
        case SCAN:
            heading = "扫描 Wi-Fi";
            if (net.scanning) strcpy(one, "正在扫描…");
            else if (net.note == NET_NOTE_SCAN_FAILED) strcpy(one, "扫描失败");
            else snprintf(one, sizeof one, "找到 %u 个网络", (unsigned)net.ap_count);
            break;
        case PASSWORD:
        case CHARACTERS:
            heading = "输入 Wi-Fi 密码";
            safe_ssid(one, ssid);
            snprintf(two, sizeof two, "已输入 %u 位", (unsigned)strlen(secret));
            break;
        case ADDRESS:
            heading = "修改 CP-IP";
            snprintf(one, sizeof one, "%s", address[0] ? address : "输入 IPv4 地址");
            strcpy(two, "输入数字和点，再选保存");
            break;
        case JOINING:
            heading = "正在连接";
            safe_ssid(one, ssid);
            strcpy(two, "等待 Wi-Fi 分配地址");
            break;
        case HOTSPOT:
            heading = hotspot_diy ? "DIY 照片上传" : "热点设置";
            hint = "长按 OK 关闭热点";
            if (net.setup) {
                snprintf(one, sizeof one, "热点名  %s", net.setup_ssid);
                snprintf(two, sizeof two, "热点密码  %s", net.setup_key);
            } else if (net.note == NET_NOTE_SETUP_FAILED) strcpy(one, "热点启动失败");
            else strcpy(one, "正在启动热点…");
            break;
        case STYLES:
            heading = "卡片与照片";
            strcpy(one, "选择功率卡，也可切到 DIY");
            strcpy(two, "在 DIY 卡短按 OK 上传");
            break;
        case PREFS:
            heading = "屏幕与休眠";
            strcpy(one, "息屏关闭液晶和背光");
            strcpy(two, "任意键唤醒 · 后台继续读数");
            break;
        case SAMPLING:
            heading = "采样频率";
            strcpy(one, "局域网和 MCP 分别设置");
            break;
        case ADJUST:
            heading = pref_names[editing];
            hint = "上下调 · OK 保存 · 长按 OK 取消";
            strcpy(one, editing >= 2 ? "长按上下键一次调 5 秒" : "短按上下键调整");
            break;
        default: break;
    }
    text(title, heading);
    text(line1, one);
    text(line2, error_text ? error_text : two);
    text(footer, hint);
    bool root = page == MENU, adjust = page == ADJUST, values = page == PREFS || page == SAMPLING;
    lv_obj_set_y(settings_rule, root ? 66 : 78);
    visible(gauge, adjust);
    unsigned count = options();
    if (!count) cursor = 0;
    else if (cursor >= count) cursor = count - 1;
    unsigned start = cursor >= ROWS ? cursor - ROWS + 1 : 0;
    for (unsigned r = 0; r < ROWS; ++r) {
        unsigned i = start + r;
        bool adjust_card = adjust && r == 0;
        visible(row_bg[r], i < count || adjust_card);
        if (i >= count && !adjust_card) continue;
        lv_obj_set_y(row_bg[r], adjust_card ? 102 : root ? 74 + (int)r * 53 : 84 + (int)r * 33);
        lv_obj_set_height(row_bg[r], adjust_card ? 76 : root ? 47 : 29);
        lv_obj_set_pos(row_label[r], 12, adjust_card ? 9 : root ? 1 : 0);
        lv_obj_set_width(row_label[r], values ? 91 : 178);
        lv_obj_set_style_text_align(row_label[r], adjust_card ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
        visible(row_detail[r], root || values);
        visible(row_arrow[r], root);
        char str[80];
        if (adjust_card) {
            char value[24];
            pref_value(value, &edit, editing);
            snprintf(str, sizeof str, "%s", value);
            unsigned fill;
            if (editing == 0) fill = edit.brightness * 154u / BRIGHT_MAX;
            else if (editing == 1) {
                unsigned slot = 0;
                unsigned n = sizeof sleep_choices / sizeof sleep_choices[0];
                while (slot + 1 < n && sleep_choices[slot] < edit.sleep_seconds) ++slot;
                fill = slot * 154u / (n - 1u);
            } else {
                unsigned seconds = editing == 2 ? edit.lan_seconds : edit.mcp_seconds;
                fill = seconds * 154u / SAMPLE_MAX_SECONDS;
            }
            lv_obj_set_width(gauge_fill, fill ? fill : 1);
        } else option_text(str, i);
        bool selected = adjust_card || (i == cursor && page != HOTSPOT);
        lv_obj_set_style_bg_color(row_bg[r], lv_color_hex(selected ? 0x453123 : 0x2a211b), 0);
        lv_obj_set_style_border_color(row_bg[r], lv_color_hex(selected ? ACCENT : 0x3c2f28), 0);
        lv_obj_set_style_text_color(row_label[r], lv_color_hex(selected ? 0xffdb8f : PAPER), 0);
        text(row_label[r], str);
        if (root || values) {
            lv_obj_set_pos(row_detail[r], root ? 12 : 100, root ? 19 : 0);
            lv_obj_set_width(row_detail[r], root ? 163 : 88);
            lv_obj_set_style_text_align(row_detail[r], root ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
            if (root) category_status(str, i);
            else {
                char value[24];
                pref_value(value, &prefs, i + (page == SAMPLING ? 2u : 0u));
                snprintf(str, sizeof str, "%s", value);
            }
            text(row_detail[r], str);
            lv_obj_set_style_text_color(row_arrow[r], lv_color_hex(selected ? ACCENT : QUIET), 0);
        }
    }
    dirty_settings = false;
}

static void layout_card(bool stack) {
    lv_obj_set_height(field, stack ? STACK_FIELD_H : SNAKE_BOARD_H);
    visible(board_rule, !stack);
    visible(card_action_key, !stack);
    visible(card_action, !stack);
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        int x = stack ? 24 + STACK_SIDE_X : 24 + (int)(i % 3) * 65;
        int y = stack ? 98 + STACK_PORT_Y + (int)i * STACK_PORT_STEP : 229 + (int)(i / 3) * 31;
        lv_obj_set_pos(port_focus[i], x + (stack ? 2 : -2), y + (stack ? 4 : 0));
        lv_obj_set_size(port_focus[i], stack ? 88 : 61, stack ? 18 : 27);
        lv_obj_set_pos(port_dot[i], x + (stack ? 6 : 2), y + (stack ? 12 : 5));
        lv_obj_set_pos(port_tag[i], x + (stack ? 14 : 10), y);
        visible(port_state[i], !stack);
        lv_obj_set_pos(port_power[i], x + (stack ? 35 : 2), y + (stack ? 0 : 13));
        lv_obj_set_width(port_power[i], stack ? 55 : 60);
        lv_obj_set_style_text_align(port_power[i], stack ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    }
}

static void paint_card(uint32_t now) {
    static bool primed;
    static uint8_t last_head, last_pellet, last_spark, last_sleep, last_blink;
    static uint32_t last_total, last_port_mw[SNAKE_PORTS];
    static uint8_t last_style = STYLE_COUNT;
    static snake_freshness_t last_fresh;
    static arcade_phase_t last_round;
    static bool last_relay;
    static float last_body[SNAKE_PORTS];
    card_now = now;
    snake_style_t style = card_style();
    snake_freshness_t fresh = snake_freshness(&model, now);
    bool live = fresh == SNAKE_FRESH;
    arcade_phase_t round = game.phase;
    bool relaying = style == STYLE_BRICKS && live && brick_relay_active(&brick_relay, now);
    uint32_t total = snake_total(&model.sample);
    uint8_t spark = model.sparkling ? (uint8_t)((now - model.spark_at) / 20 + 1) : 0;
    uint8_t sleep = model.zero_streak >= 2 && !model.moving ? (now % 4000 < 900 ? 2 : 1) : 0;
    uint8_t blink = model.pellet_ports ? (uint8_t)((now / 100) % 2) : 0;
    bool changed_style = last_style != style;
    bool changed_round = last_round != round;
    bool stack = style == STYLE_STACK, diy = style == STYLE_DIY;
    bool album_change = diy && update_album(now, changed_style);
    visible(diy_view, diy && diy_ready);
    if (changed_style || changed_round) {
        idle_since = last_motion = now;
        art_phase = 0;
    }
    bool motion = round == ARCADE_IDLE && !diy && now - last_motion >= (stack ? 240u : 120u);
    if (motion) {
        last_motion = now;
        art_phase = now - idle_since;
    }
    if (changed_style) {
        text(card_title, style_names[style]);
        layout_card(stack);
    }
    bool ports_changed = false;
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        if (last_port_mw[i] != model.sample.port[i].mw) ports_changed = true;
        last_port_mw[i] = model.sample.port[i].mw;
    }
    if (!primed || last_total != total || last_fresh != fresh) lv_obj_invalidate(hero);
    bool power_art = (round == ARCADE_IDLE && !stack && !diy) || style == STYLE_BRICKS;
    bool stack_power = stack && round == ARCADE_IDLE && (ports_changed || last_fresh != fresh);
    if (!primed || changed_style || changed_round || game_dirty || motion || stack_power || album_change ||
        (power_art && (last_head != model.head || last_fresh != fresh || last_pellet != model.pellet_ports ||
         last_spark != spark || last_sleep != sleep || last_blink != blink || last_relay != relaying ||
         (relaying && now - brick_relay.at < BRICK_RELAY_MORPH_MS) ||
         memcmp(last_body, model.body, sizeof last_body)))) {
        lv_obj_invalidate(field);
        __atomic_add_fetch(&drawn_frames, 1, __ATOMIC_RELAXED);
    }
    game_dirty = false;
    primed = true;
    last_total = total;
    last_fresh = fresh;
    last_head = model.head;
    last_pellet = model.pellet_ports;
    last_spark = spark;
    last_sleep = sleep;
    last_blink = blink;
    last_style = style;
    last_round = round;
    last_relay = relaying;
    memcpy(last_body, model.body, sizeof last_body);
    text(route_label, demo ? "演示" : live ? source_name(model.sample.source) :
        fresh == SNAKE_LOST ? "连接中断" : fresh == SNAKE_WAIT ? "等待更新" : "等待读数");
    int battery = __atomic_load_n(&battery_pct, __ATOMIC_RELAXED);
    char percent[8] = "";
    if (battery >= 0 && battery <= 100) snprintf(percent, sizeof percent, "%d%%", battery);
    text(battery_label, percent);
    for (unsigned i = 0; i < SNAKE_PORTS; ++i) {
        const snake_port_t *p = &model.sample.port[i];
        uint32_t dot = live && p->mw ? snake_port_colors[i] : p->attached ? 0xa28865 : 0x65533f;
        lv_color_t next = lv_color_hex(dot);
        if (!lv_color_eq(lv_obj_get_style_bg_color(port_dot[i], 0), next))
            lv_obj_set_style_bg_color(port_dot[i], next, 0);
        visible(port_focus[i], live && model.port[i].pulsing);
        bool pending = live && !p->attached && model.port[i].idle_streak < 2;
        const char *state = live && p->attached && !p->mw ? "接入" : pending ? "确认" : "";
        text(port_state[i], state);
        char value[20];
        if (fresh == SNAKE_EMPTY || fresh == SNAKE_LOST) strcpy(value, "—");
        else if (snake_port_idle(p) && !pending) strcpy(value, "空闲");
        else {
            snake_power_text(value, p->mw);
            strcat(value, " W");
        }
        text(port_power[i], value);
    }
    const char *bottom = demo ? "短按 OK 退出演示" : "上下选 · OK开局 · 长按上换景";
    char hud[72], detail[48], relay_hint[52];
    unsigned long score = round == ARCADE_IDLE ? 0 : game.score;
    if (score > best[style]) best[style] = (uint32_t)score;
    bool active_stack = stack && round == ARCADE_PLAYING;
    bool idle_stack = stack && round == ARCADE_IDLE;
    bool idle_maze = style == STYLE_MAZE && round == ARCADE_IDLE;
    bool overlay = round == ARCADE_RESULT || round == ARCADE_PAUSED;
    bool diy_empty = diy && !diy_ready;
    visible(game_hud, round != ARCADE_IDLE || idle_maze || diy_empty);
    visible(game_detail, active_stack || idle_stack || overlay || diy_empty);
    visible(stack_lines, active_stack || idle_stack);
    text(card_action_key, demo ? "" : "长按 OK");
    text(card_action, demo ? "" : round == ARCADE_IDLE ? "设置" : "退出");
    if (diy) {
        bottom = "上下选款 · OK 上传照片";
        if (diy_ready) {
            snprintf(detail, sizeof detail, "换图 %u", diy_slot + 1u);
            text(card_action_key, "长按上");
            text(card_action, detail);
        } else {
            text(game_hud, "DIY 像素画");
            text(game_detail, "短按 OK 打开上传");
        }
    }
    if (changed_round || changed_style) {
        int panel_y = stack ? STACK_PANEL_Y : 33;
        int side_x = STACK_SIDE_X + 2, side_w = SNAKE_BOARD_W - STACK_SIDE_X - 4;
        lv_obj_set_pos(game_hud, active_stack ? side_x : 4, overlay ? panel_y + 11 : active_stack ? 46 : diy ? 47 : 2);
        lv_obj_set_width(game_hud, active_stack ? side_w : SNAKE_BOARD_W - 8);
        lv_obj_set_pos(game_detail, active_stack || idle_stack ? side_x : 4, overlay ? panel_y + 31 : diy ? 68 : 1);
        lv_obj_set_width(game_detail, active_stack || idle_stack ? side_w : SNAKE_BOARD_W - 8);
    }
    if (idle_stack) {
        text(game_detail, "每层 7 W");
        text(stack_lines, "两列一口");
    }
    if (idle_maze) text(game_hud, maze_names[snake_idle_scene(art_phase)]);
    if (round == ARCADE_RESULT) {
        snprintf(hud, sizeof hud, style == STYLE_BRICKS ? "本局 %lu 枚金币" : "本局 %lu 分", score);
        snprintf(detail, sizeof detail, "开机最高 %lu", (unsigned long)best[style]);
        text(game_hud, hud);
        text(game_detail, detail);
        bottom = "OK 再玩 · 长按 OK 返回";
    } else if (round == ARCADE_PAUSED) {
        text(game_hud, "已暂停");
        text(game_detail, "短按 OK 继续");
        bottom = "长按 OK 回功率页";
    } else if (round == ARCADE_PLAYING) {
        if (active_stack) {
            if (score >= 100000000ul) snprintf(hud, sizeof hud, "%lu万分", score / 10000ul);
            else snprintf(hud, sizeof hud, "%lu 分", score);
            text(game_detail, "下一块");
            snprintf(detail, sizeof detail, "消行 %u", game.play.stack.lines);
            text(stack_lines, detail);
        } else if (style == STYLE_BRICKS) {
            snprintf(hud, sizeof hud, "金币 %lu  %u关 %u命", score, game.level, game.lives);
        } else if (style == STYLE_MAZE) {
            snprintf(hud, sizeof hud, "%lu 分  余%u豆  %u命", score, game.play.maze.remaining, game.lives);
        } else {
            snprintf(hud, sizeof hud, "%lu 分  %u关  %u命", score, game.level, game.lives);
        }
        text(game_hud, hud);
        bottom = controls[style];
    } else if (relaying) {
        snprintf(relay_hint, sizeof relay_hint, "接力 %s > %s · OK %s",
                 port_names[brick_relay.from], port_names[brick_relay.to], demo ? "返回" : "开局");
        bottom = relay_hint;
    } else if (relay_demo) bottom = "60 W 接力演示 · OK 返回";
    text(legend, bottom);
    __atomic_store_n(&shown_style, style, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_round, round, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_score, (uint32_t)score, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_level, round == ARCADE_IDLE ? 0 : game.level, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_lives, round == ARCADE_IDLE ? 0 : game.lives, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_round_age, round == ARCADE_IDLE ? 0 : game.elapsed, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_relay, relaying, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_watts, model.has_sample ? total : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&shown_source, live ? model.sample.source : SNAKE_NONE, __ATOMIC_RELAXED);
}

static const struct { uint16_t at; uint32_t c1; uint32_t c4; bool attached_c1; } scene[] = {
    {0,0,0,false}, {500,0,0,true}, {1600,20000,0,true}, {3400,20000,40000,true},
    {5300,10000,50000,true}, {7300,0,50000,false}, {8650,0,0,false},
    {9750,0,0,false}, {10800,140000,0,true}
};

static void set_demo(bool enabled, uint32_t now) {
    demo = enabled;
    relay_demo = false;
    demo_at = now;
    demo_step = 0;
    game.phase = ARCADE_IDLE;
    brick_relay = (brick_relay_t){0};
    snake_reset(&model);
    show_page(CARD);
    if (enabled) ESP_LOGI("candypin", "Demo selected");
}

static void set_relay_demo(uint32_t now) {
    set_demo(true, now);
    relay_demo = true;
}

static void accept_power(const snake_sample_t *sample, uint32_t now) {
    bool adjacent = card_style() == STYLE_BRICKS && model.has_sample && !model.held &&
        !model.pellet_ports && snake_freshness(&model, now) == SNAKE_FRESH && model.sample.source == sample->source;
    if (!adjacent) brick_relay = (brick_relay_t){0};
    bool transfer = adjacent && brick_relay_observe(&brick_relay, &model.sample, sample, now);
    snake_accept(&model, sample, now);
    if (transfer && model.change_ms) model.change_ms = BRICK_RELAY_MORPH_MS;
}

static void demo_update(uint32_t now) {
    if (relay_demo) {
        if (demo_step < 2 && (!demo_step || now - demo_at >= BRICK_REPLAY_TRANSFER_MS)) {
            snake_sample_t sample = brick_replay_sample(demo_step++, now);
            accept_power(&sample, now);
        }
        return;
    }
    if (demo_step == sizeof scene / sizeof scene[0] || now - demo_at < scene[demo_step].at) return;
    unsigned i = demo_step++;
    snake_sample_t sample = {.source = SNAKE_DEMO, .at = now};
    sample.port[0] = (snake_port_t){scene[i].c1, scene[i].attached_c1 ? 1 : 0, scene[i].attached_c1};
    sample.port[3] = (snake_port_t){scene[i].c4, scene[i].c4 ? 1 : 0, scene[i].c4 != 0};
    accept_power(&sample, now);
}

static bool send(snake_net_cmd_t *cmd) {
    bool accepted = snake_net_send(cmd);
    explicit_bzero(cmd, sizeof *cmd);
    if (!accepted) error_text = "网络正忙，请重试";
    dirty_settings = true;
    return accepted;
}

static void apply_prefs(const snake_prefs_t *next) {
    bool changed_brightness = prefs.brightness != next->brightness;
    if (prefs.style != next->style) {
        game.phase = ARCADE_IDLE;
        brick_relay = (brick_relay_t){0};
    }
    prefs = *next;
    if (changed_brightness && !screen_dark) bsp_display_backlight(prefs.brightness);
    dirty_settings = true;
}

static bool store_prefs(const snake_prefs_t *next) {
    if (!memcmp(next, &prefs, sizeof prefs)) return true;
    snake_net_cmd_t cmd = {.action = NET_PREFS, .value.prefs = *next};
    if (!send(&cmd)) return false;
    prefs_pending = true;
    apply_prefs(next);
    return true;
}

static void change_style(unsigned style) {
    snake_prefs_t next = prefs;
    next.style = (uint8_t)style;
    if (store_prefs(&next)) show_page(CARD);
}

static int clamped(int value, int low, int high) {
    if (value < low) return low;
    return value > high ? high : value;
}

static void adjust_value(int sign, bool fast) {
    if (editing == 0) {
        int value = edit.brightness + sign * (fast ? 20 : 5);
        edit.brightness = (uint8_t)clamped(value, BRIGHT_MIN, BRIGHT_MAX);
        bsp_display_backlight(edit.brightness);
    } else if (editing == 1) {
        int i = 0;
        int count = sizeof sleep_choices / sizeof sleep_choices[0];
        while (i < count - 1 && sleep_choices[i] < edit.sleep_seconds) ++i;
        edit.sleep_seconds = sleep_choices[clamped(i + sign, 0, count - 1)];
    } else {
        uint8_t *seconds = editing == 2 ? &edit.lan_seconds : &edit.mcp_seconds;
        int minimum = editing == 2 ? LAN_MIN_SECONDS : MCP_MIN_SECONDS;
        *seconds = (uint8_t)clamped(*seconds + sign * (fast ? 5 : 1), minimum, SAMPLE_MAX_SECONDS);
    }
    dirty_settings = true;
}

static void display_sleep(bool dark, uint32_t now) {
    lv_timer_t *refresh = lv_display_get_refr_timer(lv_display_get_default());
    if (dark) {
        bsp_display_backlight(0);
        if (refresh) lv_timer_pause(refresh);
    }
    esp_err_t result = bsp_display_panel_sleep(dark);
    if (result != ESP_OK) {
        ESP_LOGW("candypin", "LCD power transition failed: %s", esp_err_to_name(result));
        if (!dark) return;
    }
    screen_dark = dark;
    __atomic_store_n(&shown_dark, dark, __ATOMIC_RELAXED);
    if (dark) return;
    if (refresh) lv_timer_resume(refresh);
    lv_obj_invalidate(lv_screen_active());
    last_input = now;
    idle_since = last_motion = now;
    art_phase = 0;
    bsp_display_backlight(page == ADJUST && editing == 0 ? edit.brightness : prefs.brightness);
    if (page == CARD) {
        snake_reset(&model);
        brick_relay = (brick_relay_t){0};
        if (demo) {
            demo_step = 0;
            demo_at = now;
        }
        lv_obj_invalidate(card);
    } else dirty_settings = true;
}

static void start_scan(void) {
    show_page(SCAN);
    snake_net_cmd_t cmd = {.action = NET_SCAN};
    if (send(&cmd)) {
        net.scanning = true;
        net.ap_count = 0;
    }
}

static void start_join(void) {
    if (protected_wifi && strlen(secret) < 8) {
        error_text = "密码至少 8 位";
        return;
    }
    snake_net_cmd_t cmd = {.action = NET_JOIN};
    memcpy(cmd.value.join.ssid, ssid, sizeof ssid);
    memcpy(cmd.value.join.password, secret, sizeof secret);
    if (send(&cmd)) {
        join_revision = net.revision;
        show_page(JOINING);
    }
}

static void erase_char(char *input) {
    size_t n = strlen(input);
    if (n) input[n - 1] = '\0';
}

static void open_hotspot(bool for_photo) {
    snake_net_cmd_t cmd = {.action = NET_START_SETUP};
    if (!net.setup && !send(&cmd)) return;
    hotspot_diy = for_photo;
    hotspot_return = for_photo ? CARD : CONNECTIONS;
    show_page(HOTSPOT);
}

static void leave_hotspot(void) {
    show_page(hotspot_return);
    if (page == CONNECTIONS) cursor = 2;
}

static void return_settings(unsigned item) {
    show_page(MENU);
    cursor = item;
}

static void return_connections(unsigned item) {
    show_page(CONNECTIONS);
    cursor = item;
}

static void refresh_card(uint32_t now) {
    if (card_style() == STYLE_DIY) {
        diy_slot = snake_pixels_next(diy_slot, snake_album_mask());
        diy_shown_slot = DIY_SLOTS;
    } else {
        art_phase = ((snake_idle_scene(art_phase) + 1u) % ARCADE_MAZES) * ARCADE_SCENE_MS;
        idle_since = now - art_phase;
        last_motion = now;
        game_dirty = true;
    }
}

static void input(input_t key, uint32_t now) {
    last_input = now;
    if (screen_dark) {
        display_sleep(false, now);
        if (key != KEY_DEMO && key != KEY_LIVE && key != KEY_RELAY) return;
    }
    if (key == KEY_DEMO || key == KEY_LIVE || key == KEY_RELAY) {
        explicit_bzero(secret, sizeof secret);
        if (key == KEY_RELAY) set_relay_demo(now);
        else set_demo(key == KEY_DEMO, now);
        return;
    }
    if (key == KEY_BACK) {
        if (page == CARD) {
            if (game.phase != ARCADE_IDLE) {
                game.phase = ARCADE_IDLE;
                return;
            }
            if (relay_demo) set_demo(false, now);
            show_page(MENU);
        }
        else if (page == MENU) show_page(CARD);
        else if (page == ADJUST) {
            bsp_display_backlight(prefs.brightness);
            show_page(adjust_return);
            cursor = editing % 2u;
        }
        else if (page == HOTSPOT) {
            snake_net_cmd_t cmd = {.action = NET_STOP_SETUP};
            if (send(&cmd)) leave_hotspot();
        }
        else if (page == CHARACTERS) show_page(PASSWORD);
        else if (page == PASSWORD) {
            explicit_bzero(secret, sizeof secret);
            show_page(SCAN);
        } else if (page == CONNECTIONS) return_settings(0);
        else if (page == PREFS) return_settings(1);
        else if (page == STYLES) return_settings(2);
        else if (page == DEMOS) return_settings(3);
        else {
            explicit_bzero(secret, sizeof secret);
            return_connections(page == MODES ? 0 : page == ADDRESS ? 3 : page == SAMPLING ? 4 : 1);
        }
        return;
    }
    if (page == CARD) {
        if (game.phase != ARCADE_IDLE) {
            static const arcade_key_t game_keys[] = {
                [KEY_UP] = ARCADE_LEFT, [KEY_DOWN] = ARCADE_RIGHT, [KEY_OK] = ARCADE_OK,
                [KEY_UP_FAST] = ARCADE_PAUSE, [KEY_DOWN_FAST] = ARCADE_DROP
            };
            game_dirty |= arcade_press(&game, game_keys[key], now);
        } else if (key == KEY_OK) {
            if (demo) set_demo(false, now);
            else if (card_style() == STYLE_DIY) open_hotspot(true);
            else {
                arcade_start(&game, card_style(), now);
                game_dirty = true;
            }
        } else if (key == KEY_UP_FAST && !demo) refresh_card(now);
        else if (!relay_demo && (key == KEY_UP || key == KEY_DOWN)) {
            change_style((prefs.style + (key == KEY_UP ? STYLE_COUNT - 1 : 1u)) % STYLE_COUNT);
        }
        return;
    }
    if (page == HOTSPOT) return;
    if (page == ADJUST) {
        bool fast = key == KEY_UP_FAST || key == KEY_DOWN_FAST;
        if (key == KEY_UP || key == KEY_UP_FAST) adjust_value(1, fast);
        else if (key == KEY_DOWN || key == KEY_DOWN_FAST) adjust_value(-1, fast);
        else if (key == KEY_OK && store_prefs(&edit)) {
            show_page(adjust_return);
            cursor = editing % 2u;
        }
        return;
    }
    unsigned count = options();
    if (!count) return;
    error_text = NULL;
    dirty_settings = true;
    if (key == KEY_UP) { cursor = (cursor + count - 1) % count; return; }
    if (key == KEY_DOWN) { cursor = (cursor + 1) % count; return; }
    if (key != KEY_OK) return;
    snake_net_cmd_t cmd = {0};
    switch (page) {
        case MENU:
            show_page(categories[cursor]);
            if (page == STYLES) cursor = prefs.style;
            break;
        case CONNECTIONS:
            switch (cursor) {
                case 0: show_page(MODES); cursor = (unsigned)net.mode; break;
                case 1: start_scan(); break;
                case 2: open_hotspot(false); break;
                case 3: memcpy(address, net.lan_ip, sizeof address); show_page(ADDRESS); break;
                case 4: show_page(SAMPLING); break;
                default:
                    if (net.wifi == WIFI_READY || net.saved_wifi) {
                        cmd.action = net.wifi == WIFI_READY ? NET_DISCONNECT : NET_RECONNECT;
                        (void)send(&cmd);
                    } else start_scan();
                    break;
            }
            break;
        case DEMOS:
            if (cursor == 0) set_demo(true, now);
            else set_relay_demo(now);
            break;
        case MODES:
            cmd.action = NET_MODE;
            cmd.value.mode = (snake_mode_t)cursor;
            if (send(&cmd)) return_connections(0);
            break;
        case STYLES:
            change_style(cursor);
            break;
        case PREFS: case SAMPLING:
            adjust_return = page;
            editing = cursor + (page == SAMPLING ? 2u : 0u);
            edit = prefs;
            show_page(ADJUST);
            break;
        case SCAN:
            if (cursor == net.ap_count) start_scan();
            else if (!net.ap[cursor].supported) error_text = "暂不支持此加密方式";
            else {
                memcpy(ssid, net.ap[cursor].ssid, sizeof ssid);
                explicit_bzero(secret, sizeof secret);
                protected_wifi = net.ap[cursor].secure;
                if (protected_wifi) show_page(PASSWORD);
                else start_join();
            }
            break;
        case PASSWORD:
            if (cursor < 4) { group = cursor; show_page(CHARACTERS); }
            else if (cursor == 4) erase_char(secret);
            else if (cursor == 5) start_join();
            else { explicit_bzero(secret, sizeof secret); show_page(SCAN); }
            break;
        case CHARACTERS: {
            size_t n = strlen(secret);
            if (n < sizeof secret - 1) {
                secret[n] = alphabets[group][cursor];
                secret[n + 1] = '\0';
            } else error_text = "密码长度已满";
            break;
        }
        case ADDRESS:
            if (cursor <= 10) {
                size_t n = strlen(address);
                if (n < sizeof address - 1) {
                    address[n] = cursor == 10 ? '.' : '0' + (int)cursor;
                    address[n + 1] = '\0';
                }
            } else if (cursor == 11) erase_char(address);
            else if (cursor == 12) address[0] = '\0';
            else if (cursor == 14) return_connections(3);
            else if (!snake_lan_ip_valid(address)) error_text = "请输入有效的局域网地址";
            else {
                cmd.action = NET_IP;
                memcpy(cmd.value.ip.address, address, sizeof address);
                cmd.value.ip.port = net.lan_port ? net.lan_port : 80;
                if (send(&cmd)) return_connections(3);
            }
            break;
        default: break;
    }
}

bool snake_app_press(input_t key) {
    return inputs && xQueueSend(inputs, &key, 0) == pdTRUE;
}

static void button(bsp_btn_t key, bsp_btn_ev_t event, void *unused) {
    (void)unused;
    static struct { bool fast, held; } gesture[BSP_BTN_OK + 1];
    static const input_t short_keys[] = {KEY_UP, KEY_DOWN, KEY_OK};
    static const input_t long_keys[] = {KEY_UP_FAST, KEY_DOWN_FAST, KEY_BACK};
    switch (event) {
        case BSP_BTN_PRESS:
            gesture[key].fast = __atomic_load_n(&fast_card_input, __ATOMIC_RELAXED);
            gesture[key].held = false;
            break;
        case BSP_BTN_RELEASE:
            if (gesture[key].fast && !gesture[key].held) snake_app_press(short_keys[key]);
            break;
        case BSP_BTN_LONG:
            gesture[key].held = true;
            snake_app_press(long_keys[key]);
            break;
        case BSP_BTN_CLICK:
            if (!gesture[key].fast) snake_app_press(short_keys[key]);
            break;
        default: break;
    }
}

static void tick(lv_timer_t *timer) {
    uint32_t now = now_ms();
    bool was_dark = screen_dark;
    snake_net_view_t latest;
    if (snake_net_view(&latest) && latest.revision != net.revision) {
        bool was_setup = net.setup;
        net = latest;
        dirty_settings = true;
        if (snake_prefs_valid(&net.prefs)) {
            bool different = memcmp(&prefs, &net.prefs, sizeof prefs) != 0;
            bool failed = prefs_pending && net.note == NET_NOTE_SAVE_FAILED;
            if (!different || failed) prefs_pending = false;
            if (!prefs_pending && different) apply_prefs(&net.prefs);
            if (failed && (page == PREFS || page == SAMPLING)) error_text = "保存失败";
        }
        if (!was_setup && net.setup && page == CARD && !demo && game.phase == ARCADE_IDLE) {
            hotspot_diy = false;
            hotspot_return = CONNECTIONS;
            show_page(HOTSPOT);
        }
        if (was_setup && !net.setup && page == HOTSPOT) {
            last_input = now;
            leave_hotspot();
        }
        if (page == JOINING && net.revision != join_revision && net.wifi != WIFI_CONNECTING) {
            if (net.note == NET_NOTE_JOIN_FAILED) {
                show_page(protected_wifi ? PASSWORD : SCAN);
                error_text = "连接失败，请检查密码";
            } else if (net.wifi == WIFI_READY) {
                explicit_bzero(secret, sizeof secret);
                return_connections(1);
            }
        }
    }
    arcade_phase_t previous = game.phase;
    if (!screen_dark && page == CARD) game_dirty |= arcade_update(&game, now);
    input_t key;
    while (xQueueReceive(inputs, &key, 0) == pdTRUE) input(key, now);
    if (previous == ARCADE_PLAYING && game.phase == ARCADE_RESULT) last_input = now;
    if (!screen_dark && page != HOTSPOT && game.phase != ARCADE_PLAYING && prefs.sleep_seconds &&
        now - last_input >= (uint32_t)prefs.sleep_seconds * 1000u) display_sleep(true, now);
    __atomic_store_n(&fast_card_input, !screen_dark && page == CARD, __ATOMIC_RELAXED);
    if (was_dark != screen_dark) lv_timer_set_period(timer, screen_dark ? 160 : 40);
    if (screen_dark) return;
    if (page == CARD) {
        if (relay_demo && now - demo_at >= BRICK_REPLAY_MS) set_demo(false, now);
        if (demo) demo_update(now);
        else {
            snake_sample_t sample;
            if (snake_net_latest(&sample)) accept_power(&sample, now);
        }
        snake_tick(&model, now);
        paint_card(now);
    } else if (dirty_settings) paint_settings();
}

void snake_app_status(char *message, size_t capacity) {
    unsigned style = __atomic_load_n(&shown_style, __ATOMIC_RELAXED);
    unsigned long score = __atomic_load_n(&shown_score, __ATOMIC_RELAXED);
    snprintf(message, capacity, "PS STATE page=%lu source=%u total_mw=%lu frames=%u heap=%u largest=%u "
        "style=%u dark=%u round=%u coins=%lu relay=%u age_ms=%lu score=%lu level=%u lives=%u\n",
        (unsigned long)__atomic_load_n(&settings_revision, __ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&shown_source, __ATOMIC_RELAXED),
        (unsigned long)__atomic_load_n(&shown_watts, __ATOMIC_RELAXED),
        __atomic_load_n(&drawn_frames, __ATOMIC_RELAXED),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        style,
        (unsigned)__atomic_load_n(&shown_dark, __ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&shown_round, __ATOMIC_RELAXED),
        style == STYLE_BRICKS ? score : 0ul,
        (unsigned)__atomic_load_n(&shown_relay, __ATOMIC_RELAXED),
        (unsigned long)__atomic_load_n(&shown_round_age, __ATOMIC_RELAXED), score,
        (unsigned)__atomic_load_n(&shown_level, __ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&shown_lives, __ATOMIC_RELAXED));
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    prefs = snake_prefs_default();
    last_input = now_ms();
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) {
        ESP_LOGE("candypin", "Display initialization failed");
        return;
    }
    inputs = xQueueCreate(12, sizeof(input_t));
    if (!inputs) return;
    snake_reset(&model);
    if (bsp_lvgl_lock(1000)) {
        create_ui();
        paint_card(now_ms());
        lv_timer_create(tick, 40, NULL);
        bsp_lvgl_unlock();
    } else return;
    bsp_display_backlight(prefs.brightness);
    if (bsp_button_init(button, NULL) != ESP_OK) ESP_LOGE("candypin", "Button initialization failed");
    if (nvs == ESP_OK) snake_album_init();
    if (nvs == ESP_OK && !snake_net_start()) ESP_LOGE("candypin", "Network task could not start");
    if (nvs != ESP_OK) ESP_LOGE("candypin", "Settings unavailable: %s", esp_err_to_name(nvs));
    snake_usb_start();
    ESP_LOGI("candypin", "240x320 card ready, heap=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    if (bsp_battery_init() != ESP_OK) return;
    for (;;) {
        __atomic_store_n(&battery_pct, bsp_battery_soc(), __ATOMIC_RELAXED);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
