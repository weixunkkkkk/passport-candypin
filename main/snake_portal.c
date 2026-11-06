#include "snake_portal.h"
#include "snake_net.h"
#include "snake_album.h"
#include "snake_setup_dns.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "lwip/udp.h"
#include <stdio.h>
#include <string.h>

static httpd_handle_t server;
static struct udp_pcb *setup_dns; // Owned by the lwIP thread.
static char token[17];
extern const char diy_html[] asm("_binary_snake_diy_portal_html_start");

static bool hotspot_request(httpd_req_t *req) {
    struct sockaddr_storage local = {0};
    socklen_t size = sizeof local;
    if (getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &size) != 0) return false;
    bool on_hotspot = false;
    if (local.ss_family == AF_INET) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)&local;
        on_hotspot = ntohl(ipv4->sin_addr.s_addr) == 0xc0a80401u;
    }
#if LWIP_IPV6
    else if (local.ss_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)&local;
        const unsigned char mapped[] = {0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
        on_hotspot = !memcmp(ipv6->sin6_addr.s6_addr, mapped, sizeof mapped);
    }
#endif
    return on_hotspot;
}

static bool portal_host(httpd_req_t *req) {
    char host[32];
    return httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) == ESP_OK &&
        (!strcmp(host, "192.168.4.1") || !strcmp(host, "192.168.4.1:80"));
}

static bool local_request(httpd_req_t *req) {
    if (!hotspot_request(req) || !portal_host(req)) return false;
    char origin[40];
    if (httpd_req_get_hdr_value_len(req, "Origin") &&
        (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof origin) != ESP_OK ||
         (strcmp(origin, "http://192.168.4.1") && strcmp(origin, "http://192.168.4.1:80")))) return false;
    return true;
}

static esp_err_t message(httpd_req_t *req, const char *status, const char *text) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t open_portal(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return message(req, "302 Found", "正在打开 CandyPin 配网");
}

static esp_err_t captive_probe(httpd_req_t *req, httpd_err_code_t code) {
    (void)code;
    if (req->method == HTTP_GET && hotspot_request(req)) return open_portal(req);
    return message(req, "404 Not Found", "未找到页面。");
}

static esp_err_t retry_page(httpd_req_t *req) {
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static void safe_html(char out[193], const char *text) {
    char *next = out;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *entity = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" :
            *p == '"' ? "&quot;" : *p == '\'' ? "&#39;" : NULL;
        if (entity) {
            size_t n = strlen(entity);
            memcpy(next, entity, n);
            next += n;
        } else *next++ = *p < 32 || *p == 127 ? '?' : (char)*p;
    }
    *next = '\0';
}

static void hex_name(char out[65], const char *text) {
    const char digits[] = "0123456789abcdef";
    size_t i = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        out[i++] = digits[*p >> 4];
        out[i++] = digits[*p & 15];
    }
    out[i] = '\0';
}

static const char page_head[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8><meta name=viewport "
    "content=\"width=device-width, initial-scale=1\"><title>CandyPin 连接设置</title>"
    "<style>body{background:#191612;color:#fff0d7;font:16px system-ui,sans-serif;max-width:26rem;"
    "margin:2rem auto;padding:0 1.1rem}h1{font-size:1.55rem}p{color:#d9c4a8;line-height:1.5}"
    "label{display:block;margin:1rem 0 .3rem}input,select,button{box-sizing:border-box;width:100%;"
    "font:inherit;padding:.7rem;border:1px solid #675139;border-radius:.45rem;background:#2d251b;"
    "color:#fff0d7}button{margin-top:.9rem;background:#ffc444;color:#281b0d;border:0}"
    "form+form button{background:#3a2f21;color:#fff0d7}.muted{font-size:.88rem}</style>"
    "<h1>CandyPin 连接设置</h1><p><a style=\"color:#ffc444\" href=/diy>DIY 像素画上传</a></p>";

static esp_err_t show(httpd_req_t *req) {
    if (hotspot_request(req) && !portal_host(req)) return open_portal(req);
    if (!local_request(req)) return message(req, "403 Forbidden", "请连接 Passport 的配网热点。");
    snake_net_view_t view;
    if (!snake_net_view(&view) || !view.setup) return message(req, "503 Service Unavailable", "配网页正在启动。");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (view.wifi == WIFI_CONNECTING || view.scanning) httpd_resp_set_hdr(req, "Refresh", "2");
    if (httpd_resp_sendstr_chunk(req, page_head) != ESP_OK) return ESP_FAIL;
    const char *status = view.setup_joined ? "Wi-Fi 已连接。热点稍后自动关闭。" :
        view.wifi == WIFI_CONNECTING ? "正在连接 Wi-Fi…" :
        view.note == NET_NOTE_JOIN_FAILED ? "连接失败，请检查 Wi-Fi 密码后重试。" :
        view.note == NET_NOTE_SAVE_FAILED ? "保存失败，请重试。" :
        view.scanning ? "正在扫描 Wi-Fi…" :
        view.note == NET_NOTE_SCAN_FAILED ? "扫描失败，可以重新扫描。" :
        view.mode == MODE_MCP ? "选择 2.4 GHz Wi-Fi，并配置小电拼 MCP 地址。" :
        view.lan_ip[0] ? "选择与小电拼同网的 2.4 GHz Wi-Fi。" :
        "选择与小电拼同网的 2.4 GHz Wi-Fi，CandyPin 会自动查找小电拼。";
    char item[320];
    snprintf(item, sizeof item, "<p>%s</p>", status);
    if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
    if (!view.setup_joined && view.wifi != WIFI_CONNECTING && !view.scanning) {
        snprintf(item, sizeof item, "<form method=post action=/setup><input type=hidden name=token value=\"%s\">"
            "<label for=ssid>Wi-Fi</label><select id=ssid name=ssid>"
            "<option value=\"\">%s</option>", token, view.saved_wifi ? "保持当前 Wi-Fi" : "请选择 Wi-Fi");
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
        for (unsigned i = 0; i < view.ap_count; ++i) {
            const snake_ap_t *ap = &view.ap[i];
            char hex[65], html[193];
            hex_name(hex, ap->ssid);
            safe_html(html, ap->ssid);
            snprintf(item, sizeof item, "<option value=\"%s\"%s>%s%s</option>", hex,
                ap->supported ? "" : " disabled", html,
                ap->supported ? ap->secure ? " · 加密" : " · 开放" : " · 不支持");
            if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
        }
        if (httpd_resp_sendstr_chunk(req,
            "</select><label for=wifi>Wi-Fi 密码</label><input id=wifi name=wifi type=password maxlength=64 "
            "autocomplete=new-password placeholder=\"开放网络留空\"><label for=mcp>小电拼 MCP 地址（选填）</label>"
            "<input id=mcp name=mcp type=password maxlength=255 autocomplete=new-password "
            "placeholder=\"https://…\"><p class=muted>") != ESP_OK) return ESP_FAIL;
        if (httpd_resp_sendstr_chunk(req, view.mcp_set ? "MCP 已配置。留空保留现有地址。" :
                                    "从小电拼 App 的 AI 接入页复制。只用局域网时留空。") != ESP_OK) return ESP_FAIL;
        snprintf(item, sizeof item, "</p><button type=submit>保存并连接</button></form>"
            "<form method=post action=/scan><input type=hidden name=token value=\"%s\">"
            "<button type=submit>重新扫描 Wi-Fi</button></form>", token);
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static int digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool decode_value(const char *start, const char *end, char *out, size_t capacity) {
    size_t size = 0;
    while (start < end) {
        char next = *start++;
        if (next == '%') {
            if (end - start < 2) return false;
            int high = digit(*start++), low = digit(*start++);
            if (high < 0 || low < 0) return false;
            next = (char)(high * 16 + low);
        } else if (next == '+') next = ' ';
        if (!next || size + 1 >= capacity) return false;
        out[size++] = next;
    }
    out[size] = '\0';
    return true;
}

static bool form_value(const char *form, const char *key, char *out, size_t capacity) {
    size_t length = strlen(key);
    for (const char *p = form; p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > length && !memcmp(p, key, length) && p[length] == '=')
            return decode_value(p + length + 1, end, out, capacity);
        p = *end ? end + 1 : NULL;
    }
    out[0] = '\0';
    return true;
}

static bool decode_ssid(const char *hex, char ssid[33]) {
    size_t n = strlen(hex);
    if (!n || n % 2 || n > 64) return false;
    for (size_t i = 0; i < n / 2; ++i) {
        int high = digit(hex[i * 2]), low = digit(hex[i * 2 + 1]);
        if (high < 0 || low < 0 || !(high | low)) return false;
        ssid[i] = (char)(high * 16 + low);
    }
    ssid[n / 2] = '\0';
    return true;
}

static esp_err_t submit(httpd_req_t *req) {
    if (!local_request(req)) return message(req, "403 Forbidden", "请连接 Passport 的配网热点。");
    enum { MAX_FORM = 1100 };
    char kind[48];
    if (req->content_len < 1 || req->content_len > MAX_FORM ||
        httpd_req_get_hdr_value_str(req, "Content-Type", kind, sizeof kind) != ESP_OK ||
        strncmp(kind, "application/x-www-form-urlencoded", 33) != 0)
        return message(req, "400 Bad Request", "表单格式不正确。");
    char body[MAX_FORM + 1] = {0}, submitted_token[17];
    snake_net_cmd_t cmd = {0};
    bool valid = false, queued = false;
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) goto cleanup;
        received += (size_t)n;
    }
    if (!form_value(body, "token", submitted_token, sizeof submitted_token) || strcmp(submitted_token, token)) goto cleanup;
    if (!strcmp(req->uri, "/scan")) {
        cmd.action = NET_SCAN;
        valid = true;
    } else {
        char hex[65];
        if (!form_value(body, "ssid", hex, sizeof hex)) goto cleanup;
        if (hex[0]) {
            cmd.action = NET_JOIN;
            valid = decode_ssid(hex, cmd.value.join.ssid) &&
                form_value(body, "wifi", cmd.value.join.password, sizeof cmd.value.join.password) &&
                form_value(body, "mcp", cmd.value.join.mcp_url, sizeof cmd.value.join.mcp_url) &&
                (!cmd.value.join.mcp_url[0] || snake_mcp_url_valid(cmd.value.join.mcp_url));
        } else {
            cmd.action = NET_MCP;
            valid = form_value(body, "mcp", cmd.value.mcp_url, sizeof cmd.value.mcp_url) &&
                snake_mcp_url_valid(cmd.value.mcp_url);
        }
    }
    if (valid) queued = snake_net_send(&cmd);
cleanup:
    explicit_bzero(body, sizeof body);
    explicit_bzero(&cmd, sizeof cmd);
    if (!valid) return message(req, "400 Bad Request", "请选择 Wi-Fi 或填写有效的 HTTPS MCP 地址。");
    if (!queued) return message(req, "503 Service Unavailable", "设备正在处理上次设置，请稍后重试。");
    return retry_page(req);
}

static esp_err_t show_diy(httpd_req_t *req) {
    if (!local_request(req)) return message(req, "403 Forbidden", "请连接 Passport 的配网热点。");
    static const char marker[] = "PASSPORT_TOKEN";
    const char *at = strstr(diy_html, marker);
    if (!at) return message(req, "500 Internal Server Error", "无法打开像素画页面。");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (httpd_resp_send_chunk(req, diy_html, at - diy_html) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, token) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, at + sizeof marker - 1u) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t save_diy(httpd_req_t *req) {
    if (!local_request(req)) return message(req, "403 Forbidden", "请连接 Passport 的配网热点。");
    char header[40], query[20], slot[2];
    if (httpd_req_get_hdr_value_str(req, "X-Album-Token", header, sizeof header) != ESP_OK ||
        strcmp(header, token)) return message(req, "403 Forbidden", "请刷新像素画网页后重试。");
    if (req->content_len != DIY_FRAME_BYTES ||
        httpd_req_get_hdr_value_str(req, "Content-Type", header, sizeof header) != ESP_OK ||
        strcmp(header, "application/octet-stream") ||
        httpd_req_get_url_query_str(req, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "slot", slot, sizeof slot) != ESP_OK ||
        slot[0] < '1' || slot[0] >= '1' + DIY_SLOTS)
        return message(req, "400 Bad Request", "图片大小或画位有误，请重新生成。");

    uint8_t data[DIY_FRAME_BYTES];
    size_t received = 0;
    while (received < sizeof data) {
        int n = httpd_req_recv(req, (char *)data + received, sizeof data - received);
        if (n <= 0) return message(req, "400 Bad Request", "上传中断，请再试一次。");
        received += (size_t)n;
    }
    if (!snake_pixels_valid(data, sizeof data))
        return message(req, "400 Bad Request", "像素画格式有误，请重新生成。");
    if (!snake_album_save((unsigned)(slot[0] - '1'), data, sizeof data))
        return message(req, "500 Internal Server Error", "设备保存失败，请稍后重试。");
    return message(req, "200 OK", "已保存。在 Passport 上长按 OK 关闭热点，就能看像素画。");
}

static void answer_dns(void *context, struct udp_pcb *pcb, struct pbuf *query, const ip_addr_t *from, u16_t port) {
    (void)context;
    static uint8_t packet[SNAKE_SETUP_DNS_CAPACITY];
    u16_t length = pbuf_copy_partial(query, packet, sizeof packet, 0);
    size_t response = snake_setup_dns_reply(packet, length);
    if (response) {
        struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, (u16_t)response, PBUF_RAM);
        if (reply) {
            if (pbuf_take(reply, packet, (u16_t)response) == ERR_OK) (void)udp_sendto(pcb, reply, from, port);
            pbuf_free(reply);
        }
    }
    pbuf_free(query);
}

static esp_err_t start_dns(void *context) {
    struct netif *ap = esp_netif_get_netif_impl(context);
    if (!ap) return ESP_FAIL;
    ip_addr_t address;
    IP_ADDR4(&address, 192, 168, 4, 1);
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return ESP_ERR_NO_MEM;
    if (udp_bind(pcb, &address, 53) != ERR_OK) {
        udp_remove(pcb);
        return ESP_FAIL;
    }
    udp_bind_netif(pcb, ap);
    udp_recv(pcb, answer_dns, NULL);
    setup_dns = pcb;
    return ESP_OK;
}

static esp_err_t stop_dns(void *context) {
    (void)context;
    if (setup_dns) {
        udp_remove(setup_dns);
        setup_dns = NULL;
    }
    return ESP_OK;
}

bool snake_portal_start(esp_netif_t *ap) {
    if (server) return true;
    snprintf(token, sizeof token, "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 5800;
    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    if (httpd_start(&server, &config) != ESP_OK) return false;
    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = show},
        {.uri = "/scan", .method = HTTP_POST, .handler = submit},
        {.uri = "/setup", .method = HTTP_POST, .handler = submit},
        {.uri = "/diy", .method = HTTP_GET, .handler = show_diy},
        {.uri = "/diy/image", .method = HTTP_POST, .handler = save_diy},
    };
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; ++i) {
        if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) {
            snake_portal_stop();
            return false;
        }
    }
    if (httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_probe) != ESP_OK ||
        esp_netif_tcpip_exec(start_dns, ap) != ESP_OK)
        ESP_LOGW("snake-portal", "Automatic setup page unavailable; use 192.168.4.1");
    return true;
}

void snake_portal_stop(void) {
    if (server) {
        (void)esp_netif_tcpip_exec(stop_dns, NULL);
        if (httpd_stop(server) == ESP_OK) server = NULL;
    }
    if (!server) explicit_bzero(token, sizeof token);
}
