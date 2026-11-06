#pragma once

#include "snake_fetch.h"
#include "snake_prefs.h"

enum { SNAKE_APS = 16 };
typedef enum { MODE_AUTO, MODE_LAN, MODE_MCP } snake_mode_t;
typedef enum { WIFI_IDLE, WIFI_CONNECTING, WIFI_READY, WIFI_DISABLED } snake_wifi_t;
typedef enum { NET_NOTE_NONE, NET_NOTE_SCAN_FAILED, NET_NOTE_JOIN_FAILED, NET_NOTE_SAVE_FAILED, NET_NOTE_SETUP_FAILED } snake_net_note_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
    bool supported;
} snake_ap_t;

typedef struct {
    uint32_t revision;
    snake_wifi_t wifi;
    snake_mode_t mode;
    snake_net_note_t note;
    snake_source_t source;
    snake_prefs_t prefs;
    bool scanning;
    bool mcp_set;
    bool saved_wifi;
    bool setup;
    bool setup_joined;
    char setup_ssid[33];
    char setup_key[9];
    char ssid[33];
    char lan_ip[16];
    uint16_t lan_port;
    uint8_t ap_count;
    snake_ap_t ap[SNAKE_APS];
} snake_net_view_t;

typedef enum { NET_SCAN, NET_JOIN, NET_MODE, NET_IP, NET_MCP, NET_DISCONNECT, NET_RECONNECT,
               NET_START_SETUP, NET_STOP_SETUP, NET_PREFS } snake_net_action_t;

typedef struct {
    snake_net_action_t action;
    union {
        struct { char ssid[33]; char password[65]; char mcp_url[SNAKE_MCP_URL_MAX]; } join;
        struct { char address[16]; uint16_t port; } ip;
        char mcp_url[SNAKE_MCP_URL_MAX];
        snake_mode_t mode;
        snake_prefs_t prefs;
    } value;
} snake_net_cmd_t;

bool snake_net_start(void);
bool snake_net_send(const snake_net_cmd_t *command);
bool snake_net_view(snake_net_view_t *view);
bool snake_net_latest(snake_sample_t *sample);
bool snake_lan_ip_valid(const char *ip);
bool snake_mcp_url_valid(const char *url);
