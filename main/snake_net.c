#include "snake_net.h"
#include "snake_portal.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NVS_SPACE "power_snake"
#define HAS_IP BIT0
#define PROBE_INTERVAL_MS 5500u
#define DISCOVERY_RETRY_MS 7000u
#define DISCOVERY_QUERY_MS 1200u
#define DISCOVERY_RESULTS 16u

typedef struct { char address[16]; uint16_t port; } target_t;

static const char *TAG = "snake-net";
static QueueHandle_t commands, view_queue, sample_queue;
static EventGroupHandle_t wifi_events;
static snake_net_view_t view;
static wifi_config_t saved_sta;
static char mcp_url[SNAKE_MCP_URL_MAX];
static bool paused;
static uint32_t poll_at, probe_at, reconnect_at;
static uint32_t cloud_at;
static uint32_t discovery_at;
static target_t discovered_lan;
static bool mdns_ready;
static bool screen_off;
static uint32_t setup_deadline;
static esp_netif_t *station_netif;
static esp_netif_t *setup_netif;
static bool sntp_started;
static uint16_t disconnect_reason;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

bool snake_lan_ip_valid(const char *ip) {
    struct in_addr address;
    if (strnlen(ip, 16) >= 16) return false;
    if (inet_pton(AF_INET, ip, &address) != 1) return false;
    uint32_t n = ntohl(address.s_addr);
    return (n >> 24) == 10 || (n >> 20) == 0xac1 || (n >> 16) == 0xc0a8;
}

bool snake_mcp_url_valid(const char *url) {
    if (strncmp(url, "https://", 8) || !url[8] || url[8] == '/') return false;
    if (strnlen(url, SNAKE_MCP_URL_MAX) == SNAKE_MCP_URL_MAX) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= ' ' || *p >= 0x7f || *p == '@' || *p == '#' || *p == '\\') return false;
    return true;
}

static void publish(void) {
    ++view.revision;
    xQueueOverwrite(view_queue, &view);
}

static bool save_blob(const char *key, const void *data, size_t size) {
    nvs_handle_t handle;
    if (nvs_open(NVS_SPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, key, data, size);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool read_blob(nvs_handle_t handle, const char *key, void *out, size_t size) {
    size_t actual = size;
    return nvs_get_blob(handle, key, out, &actual) == ESP_OK && actual == size;
}

static void read_settings(void) {
    view.lan_port = 80;
    view.prefs = snake_prefs_default();
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) != ESP_OK) return;
    uint8_t mode = 0;
    if (read_blob(nvs, "mode", &mode, sizeof mode) && mode <= MODE_MCP) view.mode = (snake_mode_t)mode;
    target_t target = {0};
    if (read_blob(nvs, "lan", &target, sizeof target) && target.address[15] == '\0' &&
        (!target.address[0] || snake_lan_ip_valid(target.address)) && target.port) {
        memcpy(view.lan_ip, target.address, sizeof view.lan_ip);
        view.lan_port = target.port;
    }
    if (!read_blob(nvs, "mcp", mcp_url, sizeof mcp_url) || !snake_mcp_url_valid(mcp_url))
        explicit_bzero(mcp_url, sizeof mcp_url);
    view.mcp_set = mcp_url[0] != '\0';
    uint8_t bytes[sizeof(snake_prefs_t)];
    size_t size = sizeof bytes;
    if (nvs_get_blob(nvs, "prefs", bytes, &size) == ESP_OK)
        (void)snake_prefs_decode(bytes, size, &view.prefs);
    nvs_close(nvs);
}

static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, HAS_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (data) __atomic_store_n(&disconnect_reason, ((wifi_event_sta_disconnected_t *)data)->reason, __ATOMIC_RELAXED);
        xEventGroupClearBits(wifi_events, HAS_IP);
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ESP_LOGI(TAG, "Setup hotspot assigned a client address");
    }
}

static bool radio_init(void) {
    wifi_init_config_t defaults = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
    station_netif = esp_netif_create_default_wifi_sta();
    if (!station_netif || esp_wifi_init(&defaults) != ESP_OK) return false;
    if (esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL) != ESP_OK) return false;
    setup_netif = esp_netif_create_default_wifi_ap();
    (void)esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, event, NULL);
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    // ESP-IDF loads its existing station config during init. Keep it as a fallback
    // and use our own NVS key only when a new network obtains an IP.
    (void)esp_wifi_get_config(WIFI_IF_STA, &saved_sta);
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) == ESP_OK) {
        wifi_config_t own = {0};
        if (read_blob(nvs, "wifi", &own, sizeof own) && own.sta.ssid[0]) saved_sta = own;
        explicit_bzero(&own, sizeof own);
        nvs_close(nvs);
    }
    view.saved_wifi = saved_sta.sta.ssid[0] != '\0';
    if (esp_wifi_start() != ESP_OK) return false;
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return true;
}

static void online_status(bool online) {
    snake_wifi_t state = online ? WIFI_READY : WIFI_IDLE;
    if (state == view.wifi) return;
    view.wifi = state;
    if (online) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) memcpy(view.ssid, ap.ssid, sizeof view.ssid);
        view.ssid[32] = '\0';
        poll_at = probe_at = cloud_at = now_ms();
        discovery_at = poll_at;
        ESP_LOGI(TAG, "Wi-Fi has an IP");
        if (!sntp_started) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
        }
    } else {
        view.ssid[0] = '\0';
        view.source = SNAKE_NONE;
        discovered_lan = (target_t){0};
        snake_close_mcp();
    }
    publish();
}

static esp_err_t connect_station(wifi_config_t *config) {
    (void)esp_wifi_disconnect();
    xEventGroupClearBits(wifi_events, HAS_IP);
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, config);
    return result == ESP_OK ? esp_wifi_connect() : result;
}

static bool save_mcp(const char *url) {
    if (url[0] && !snake_mcp_url_valid(url)) return false;
    if (!save_blob("mcp", url, sizeof mcp_url)) return false;
    snake_close_mcp();
    memcpy(mcp_url, url, sizeof mcp_url);
    view.mcp_set = mcp_url[0] != '\0';
    return true;
}

static void join(const snake_net_cmd_t *cmd) {
    wifi_config_t next = {0};
    size_t name_len = strnlen(cmd->value.join.ssid, sizeof cmd->value.join.ssid);
    size_t pass_len = strnlen(cmd->value.join.password, sizeof cmd->value.join.password);
    if (!name_len || name_len > 32 || pass_len > 64 || (pass_len && pass_len < 8)) {
        view.note = NET_NOTE_JOIN_FAILED;
        publish();
        return;
    }
    if (cmd->value.join.mcp_url[0] && !save_mcp(cmd->value.join.mcp_url)) {
        view.note = NET_NOTE_SAVE_FAILED;
        publish();
        return;
    }
    memcpy(next.sta.ssid, cmd->value.join.ssid, name_len);
    memcpy(next.sta.password, cmd->value.join.password, pass_len);
    next.sta.threshold.authmode = pass_len ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    next.sta.pmf_cfg.capable = true;
    paused = false;
    snake_close_mcp();
    view.wifi = WIFI_CONNECTING;
    view.note = NET_NOTE_NONE;
    view.source = SNAKE_NONE;
    view.ssid[0] = '\0';
    publish();
    __atomic_store_n(&disconnect_reason, 0, __ATOMIC_RELAXED);
    esp_err_t start = connect_station(&next);
    bool got_ip = start == ESP_OK &&
        (xEventGroupWaitBits(wifi_events, HAS_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(11000)) & HAS_IP);
    if (got_ip) {
        saved_sta = next;
        view.saved_wifi = true;
        if (!save_blob("wifi", &saved_sta, sizeof saved_sta)) view.note = NET_NOTE_SAVE_FAILED;
        else if (view.setup) {
            view.setup_joined = true;
            setup_deadline = now_ms() + 15000;
        }
    } else {
        view.note = NET_NOTE_JOIN_FAILED;
        ESP_LOGW(TAG, "Wi-Fi join failed (reason %u)", (unsigned)__atomic_load_n(&disconnect_reason, __ATOMIC_RELAXED));
        (void)esp_wifi_disconnect();
        if (view.saved_wifi) (void)connect_station(&saved_sta);
    }
    explicit_bzero(&next, sizeof next);
    online_status(got_ip);
    reconnect_at = now_ms() + 4000;
}

static bool auth_supported(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return true;
        default:
            return false;
    }
}

static void scan(void) {
    static wifi_ap_record_t records[SNAKE_APS];
    view.scanning = true;
    view.ap_count = 0;
    view.note = NET_NOTE_NONE;
    publish();
    wifi_scan_config_t config = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 60, .max = 120}};
    uint16_t n = SNAKE_APS;
    if (esp_wifi_scan_start(&config, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
        (void)esp_wifi_clear_ap_list();
        view.note = NET_NOTE_SCAN_FAILED;
    } else {
        for (unsigned i = 0; i < n && view.ap_count < SNAKE_APS; ++i) {
            if (!records[i].ssid[0]) continue;
            bool duplicate = false;
            for (unsigned j = 0; j < view.ap_count; ++j) {
                if (strncmp(view.ap[j].ssid, (const char *)records[i].ssid, 32) == 0) duplicate = true;
            }
            if (duplicate) continue;
            snake_ap_t *ap = &view.ap[view.ap_count++];
            memcpy(ap->ssid, records[i].ssid, sizeof ap->ssid);
            ap->ssid[32] = '\0';
            ap->rssi = records[i].rssi;
            ap->secure = records[i].authmode != WIFI_AUTH_OPEN;
            ap->supported = auth_supported(records[i].authmode);
        }
    }
    view.scanning = false;
    publish();
}

static void stop_setup(void) {
    snake_portal_stop();
    if (view.setup) (void)esp_wifi_set_mode(WIFI_MODE_STA);
    view.setup = false;
    view.setup_joined = false;
    view.setup_ssid[0] = '\0';
    reconnect_at = now_ms();
    publish();
}

static void start_setup(void) {
    if (view.setup) return;
    if (!setup_netif) {
        view.note = NET_NOTE_SETUP_FAILED;
        publish();
        return;
    }
    wifi_config_t ap = {0};
    unsigned suffix = (unsigned)(esp_random() & 0xffffu);
    snprintf(view.setup_ssid, sizeof view.setup_ssid, "CandyPin-配网-%04X", suffix);
    ap.ap.ssid_len = strlen(view.setup_ssid);
    memcpy(ap.ap.ssid, view.setup_ssid, ap.ap.ssid_len);
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 1;
    (void)esp_wifi_stop();
    xEventGroupClearBits(wifi_events, HAS_IP);
    online_status(false);
    esp_netif_ip_info_t ip;
    bool ready = esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK &&
        esp_wifi_set_config(WIFI_IF_AP, &ap) == ESP_OK && esp_wifi_start() == ESP_OK &&
        esp_netif_get_ip_info(setup_netif, &ip) == ESP_OK && ntohl(ip.ip.addr) == 0xc0a80401u &&
        snake_portal_start(setup_netif);
    explicit_bzero(&ap, sizeof ap);
    if (!ready) {
        snake_portal_stop();
        (void)esp_wifi_stop();
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        (void)esp_wifi_start();
        view.note = NET_NOTE_SETUP_FAILED;
        view.setup_ssid[0] = '\0';
        publish();
        return;
    }
    paused = false;
    view.setup = true;
    view.setup_joined = false;
    setup_deadline = now_ms() + 300000;
    ESP_LOGI(TAG, "Setup hotspot and HTTP server ready at 192.168.4.1");
    publish();
    scan();
}

static void command(snake_net_cmd_t *cmd, bool radio) {
    view.note = NET_NOTE_NONE;
    switch (cmd->action) {
        case NET_SCAN:
            if (radio) scan();
            return;
        case NET_JOIN:
            if (radio) join(cmd);
            return;
        case NET_MODE: {
            uint8_t mode = cmd->value.mode;
            if (mode <= MODE_MCP) {
                if (save_blob("mode", &mode, sizeof mode)) view.mode = (snake_mode_t)mode;
                else view.note = NET_NOTE_SAVE_FAILED;
            }
            break;
        }
        case NET_IP: {
            target_t target = {.port = cmd->value.ip.port};
            memcpy(target.address, cmd->value.ip.address, sizeof target.address);
            if (target.port && (!target.address[0] || snake_lan_ip_valid(target.address))) {
                if (save_blob("lan", &target, sizeof target)) {
                    memcpy(view.lan_ip, target.address, sizeof view.lan_ip);
                    view.lan_port = target.port;
                    discovered_lan = (target_t){0};
                    discovery_at = now_ms();
                } else view.note = NET_NOTE_SAVE_FAILED;
            }
            break;
        }
        case NET_MCP:
            if (!save_mcp(cmd->value.mcp_url)) view.note = NET_NOTE_SAVE_FAILED;
            break;
        case NET_DISCONNECT:
            paused = true;
            if (radio) (void)esp_wifi_disconnect();
            xEventGroupClearBits(wifi_events, HAS_IP);
            online_status(false);
            break;
        case NET_RECONNECT:
            paused = false;
            reconnect_at = now_ms();
            break;
        case NET_START_SETUP:
            if (radio) start_setup();
            else { view.note = NET_NOTE_SETUP_FAILED; publish(); }
            return;
        case NET_STOP_SETUP:
            stop_setup();
            return;
        case NET_PREFS:
            if (snake_prefs_valid(&cmd->value.prefs) && save_blob("prefs", &cmd->value.prefs, sizeof view.prefs)) {
                bool cadence_changed = view.prefs.lan_seconds != cmd->value.prefs.lan_seconds ||
                    view.prefs.mcp_seconds != cmd->value.prefs.mcp_seconds;
                view.prefs = cmd->value.prefs;
                if (cadence_changed) poll_at = probe_at = cloud_at = now_ms();
            } else view.note = NET_NOTE_SAVE_FAILED;
            break;
    }
    if (cmd->action == NET_MODE || cmd->action == NET_IP || cmd->action == NET_MCP) {
        snake_close_mcp();
        view.source = SNAKE_NONE;
        poll_at = probe_at = cloud_at = now_ms();
    }
    publish();
}

// CP-02S advertises a cp02s-* HTTP service with its port and IPv4 address.
// A manually saved target takes precedence. Never choose between two chargers.
static target_t discover_lan(void) {
    if (!mdns_ready) {
        mdns_ready = mdns_init() == ESP_OK;
        if (!mdns_ready) return (target_t){0};
    }
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_http", "_tcp", DISCOVERY_QUERY_MS, DISCOVERY_RESULTS, &results) != ESP_OK)
        return (target_t){0};

    target_t target = {0};
    const char *instance = NULL;
    unsigned count = 0;
    for (const mdns_result_t *r = results; r; r = r->next) {
        if (++count >= DISCOVERY_RESULTS) {
            target = (target_t){0};
            break;
        }
        if (r->esp_netif != station_netif || !r->instance_name ||
            strncasecmp(r->instance_name, "cp02s-", 6) || !r->instance_name[6]) continue;
        if (instance && strcasecmp(instance, r->instance_name)) {
            target = (target_t){0};
            break;
        }
        instance = r->instance_name;
        if (target.port || !r->port) continue;
        for (const mdns_ip_addr_t *a = r->addr; a; a = a->next) {
            char ipv4[sizeof target.address];
            if (a->addr.type == ESP_IPADDR_TYPE_V4 &&
                inet_ntop(AF_INET, &a->addr.u_addr.ip4.addr, ipv4, sizeof ipv4) &&
                snake_lan_ip_valid(ipv4)) {
                strcpy(target.address, ipv4);
                target.port = r->port;
                break;
            }
        }
    }
    mdns_query_results_free(results);
    return target;
}

static bool read_lan_sample(snake_sample_t *sample) {
    if (view.lan_ip[0]) return snake_read_lan(view.lan_ip, view.lan_port, sample);
    bool first_read = !discovered_lan.port;
    if (first_read) {
        discovered_lan = discover_lan();
        discovery_at = now_ms() + DISCOVERY_RETRY_MS;
        if (!discovered_lan.port) return false;
    }
    if (!snake_read_lan(discovered_lan.address, discovered_lan.port, sample)) {
        discovered_lan = (target_t){0};
        if (!first_read) discovery_at = now_ms();
        return false;
    }
    if (first_read) ESP_LOGI(TAG, "XDP auto-discovered and telemetry verified");
    return true;
}

static void poll_telemetry(void) {
    uint32_t now = now_ms();
    uint32_t lan_ms = (uint32_t)view.prefs.lan_seconds * 1000u;
    uint32_t mcp_ms = (uint32_t)view.prefs.mcp_seconds * 1000u;
    bool scheduled = due(now, poll_at);
    bool lan_due = view.lan_ip[0] || discovered_lan.port || due(now, discovery_at);
    bool probing = view.mode == MODE_AUTO && view.source == SNAKE_MCP &&
        lan_due && due(now, probe_at);
    if (!scheduled && !probing) return;
    snake_sample_t sample;
    bool prefer_lan = view.mode == MODE_LAN || (view.mode == MODE_AUTO &&
        (view.source != SNAKE_MCP || due(now, probe_at)));
    bool ok = false;
    if (prefer_lan && lan_due) {
        ok = read_lan_sample(&sample);
        probe_at = now_ms() + (lan_ms > PROBE_INTERVAL_MS ? lan_ms : PROBE_INTERVAL_MS);
    }
    if (!ok && scheduled && view.mode != MODE_LAN && view.mcp_set &&
        time(NULL) >= 1735689600 && due(now_ms(), cloud_at)) {
        cloud_at = now_ms() + mcp_ms;
        ok = snake_read_mcp(mcp_url, &sample);
    }
    if (!ok) {
        if (scheduled)
            poll_at = now_ms() + ((view.source == SNAKE_MCP || view.mode == MODE_MCP) ? mcp_ms : lan_ms);
        return;
    }
    sample.at = now_ms();
    sample.interval_ms = sample.source == SNAKE_LAN ? lan_ms : mcp_ms;
    poll_at = now + sample.interval_ms;
    xQueueOverwrite(sample_queue, &sample);
    if (view.source != sample.source) {
        view.source = sample.source;
        if (view.source == SNAKE_LAN) snake_close_mcp();
        ESP_LOGI(TAG, "Telemetry ready: %s, %u ports, %lu mW",
            view.source == SNAKE_LAN ? "LAN" : "MCP", SNAKE_PORTS, (unsigned long)snake_total(&sample));
        publish();
    }
}

static void worker(void *unused) {
    (void)unused;
    read_settings();
    // The configured MCP URL can carry an access secret.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    bool radio = radio_init();
    if (!radio) {
        view.wifi = WIFI_DISABLED;
        ESP_LOGE(TAG, "Wi-Fi initialization failed");
    }
    publish();
    if (radio && !view.saved_wifi) start_setup();
    for (;;) {
        snake_net_cmd_t cmd;
        bool has_command = xQueueReceive(commands, &cmd, pdMS_TO_TICKS(view.suspended ? 500 : 100)) == pdTRUE;
        bool suspend = __atomic_load_n(&screen_off, __ATOMIC_RELAXED) &&
            view.prefs.standby == STANDBY_SAVE && !view.setup;
        if (suspend != view.suspended) {
            esp_err_t changed = ESP_OK;
            if (suspend) {
                snake_close_mcp();
                if (mdns_ready) { mdns_free(); mdns_ready = false; }
                if (radio) changed = esp_wifi_stop();
                if (changed == ESP_ERR_WIFI_NOT_STARTED) changed = ESP_OK;
                xEventGroupClearBits(wifi_events, HAS_IP);
                online_status(false);
            } else {
                if (radio) changed = esp_wifi_start();
                reconnect_at = now_ms();
            }
            if (changed == ESP_OK) {
                view.suspended = suspend;
                ESP_LOGI(TAG, "Screen standby network %s", suspend ? "stopped" : "resumed");
                publish();
            }
        }
        if (has_command) {
            command(&cmd, radio);
            explicit_bzero(&cmd, sizeof cmd);
        }
        if (!radio || view.suspended) continue;
        bool ip = !paused && (xEventGroupGetBits(wifi_events) & HAS_IP);
        online_status(ip);
        uint32_t now = now_ms();
        if (view.setup && due(now, setup_deadline)) stop_setup();
        if (ip && !view.setup) poll_telemetry();
        else if (!paused && !view.setup && view.saved_wifi && due(now, reconnect_at)) {
            (void)esp_wifi_set_config(WIFI_IF_STA, &saved_sta);
            (void)esp_wifi_connect();
            reconnect_at = now + 5000;
        }
    }
}

bool snake_net_start(void) {
    commands = xQueueCreate(4, sizeof(snake_net_cmd_t));
    view_queue = xQueueCreate(1, sizeof(snake_net_view_t));
    sample_queue = xQueueCreate(1, sizeof(snake_sample_t));
    wifi_events = xEventGroupCreate();
    if (!commands || !view_queue || !sample_queue || !wifi_events) return false;
    return xTaskCreate(worker, "snake-network", 6500, NULL, 4, NULL) == pdPASS;
}

bool snake_net_send(const snake_net_cmd_t *command) {
    return commands && xQueueSend(commands, command, 0) == pdTRUE;
}
bool snake_net_view(snake_net_view_t *out) {
    return view_queue && xQueuePeek(view_queue, out, 0) == pdTRUE;
}
bool snake_net_latest(snake_sample_t *sample) {
    return sample_queue && xQueuePeek(sample_queue, sample, 0) == pdTRUE;
}

void snake_net_screen_off(bool off) {
    __atomic_store_n(&screen_off, off, __ATOMIC_RELAXED);
}
