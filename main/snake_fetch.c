#include "snake_fetch.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { MAX_BODY = 8192 };
static struct {
    char text[MAX_BODY + 1];
    size_t len;
    bool oversized;
    bool event_stream;
} response;

static struct {
    esp_http_client_handle_t client;
    char session[129];
    char version[11];
    unsigned sequence;
} mcp;

static esp_err_t receive_http(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (!strcasecmp(event->header_key, "Content-Type"))
            response.event_stream = !strncasecmp(event->header_value, "text/event-stream", 17);
        if (event->client == mcp.client && !strcasecmp(event->header_key, "Mcp-Session-Id")) {
            size_t size = strnlen(event->header_value, sizeof mcp.session);
            if (size && size < sizeof mcp.session) {
                memcpy(mcp.session, event->header_value, size);
                mcp.session[size] = '\0';
            }
        }
    } else if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (event->data_len < 0 || (size_t)event->data_len > MAX_BODY - response.len) {
            response.oversized = true;
            return ESP_FAIL;
        }
        memcpy(response.text + response.len, event->data, (size_t)event->data_len);
        response.len += (size_t)event->data_len;
    }
    return ESP_OK;
}

static int request(esp_http_client_handle_t client) {
    response.len = 0;
    response.oversized = false;
    response.event_stream = false;
    if (esp_http_client_perform(client) != ESP_OK || response.oversized) return 0;
    response.text[response.len] = '\0';
    return esp_http_client_get_status_code(client);
}

static bool integer(const cJSON *obj, const char *name, uint32_t limit, uint32_t *out) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsNumber(value)) return false;
    double n = value->valuedouble;
    if (!(n >= 0 && n <= limit)) return false;
    uint32_t parsed = (uint32_t)n;
    if ((double)parsed != n) return false;
    *out = parsed;
    return true;
}

static bool read_ports(const cJSON *root, bool cloud, snake_sample_t *sample) {
    const cJSON *ports = cJSON_GetObjectItemCaseSensitive(root, "ports");
    if (!cJSON_IsArray(ports) || cJSON_GetArraySize(ports) != SNAKE_PORTS) return false;
    snake_sample_t next = {.source = cloud ? SNAKE_MCP : SNAKE_LAN};
    unsigned seen = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, ports) {
        uint32_t wire_id, mv, ma, session;
        const cJSON *attached = cJSON_GetObjectItemCaseSensitive(item, cloud ? "connected" : "attached");
        if (!integer(item, cloud ? "port" : "id", cloud ? 5 : 4, &wire_id) ||
            (cloud && wire_id == 0) || !cJSON_IsBool(attached) ||
            !integer(item, cloud ? "vout_mv" : "voltage", 60000, &mv) ||
            !integer(item, cloud ? "iout_ma" : "current", 15000, &ma) ||
            !integer(item, "session_id", UINT32_MAX, &session)) return false;
        unsigned index = (wire_id + (cloud ? 3u : 4u)) % SNAKE_PORTS;
        if (seen & (1u << index)) return false;
        seen |= 1u << index;
        uint32_t mw = (uint32_t)(((uint64_t)mv * ma + 500u) / 1000u);
        if (mw > 200000) return false;
        next.port[index] = (snake_port_t){mw, session, cJSON_IsTrue(attached)};
    }
    *sample = next;
    return true;
}

bool snake_decode_ports(const char *json, bool cloud, snake_sample_t *sample) {
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    bool valid = root && read_ports(root, cloud, sample);
    cJSON_Delete(root);
    return valid;
}

bool snake_read_lan(const char *ipv4, unsigned port, snake_sample_t *sample) {
    if (!ipv4[0]) return false;
    char url[64];
    snprintf(url, sizeof url, "http://%s:%u/metrics.json", ipv4, port);
    esp_http_client_config_t config = {.url = url, .timeout_ms = 1400, .disable_auto_redirect = true,
                                       .event_handler = receive_http, .buffer_size = 1024};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    bool ok = request(client) == 200 && snake_decode_ports(response.text, false, sample);
    esp_http_client_cleanup(client);
    return ok;
}

void snake_close_mcp(void) {
    if (mcp.client) esp_http_client_cleanup(mcp.client);
    memset(&mcp, 0, sizeof mcp);
}

static bool rpc_has_id(const cJSON *json, unsigned id) {
    uint32_t received;
    return integer(json, "id", UINT32_MAX, &received) && received == id;
}

static cJSON *rpc_result(unsigned id) {
    if (!response.event_stream) {
        cJSON *json = cJSON_ParseWithOpts(response.text, NULL, true);
        if (json && rpc_has_id(json, id)) return json;
        cJSON_Delete(json);
        return NULL;
    }
    // The server can return one JSON reply or send JSON messages in SSE data lines.
    char *line = response.text;
    while (line && *line) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        cJSON *json = !strncmp(line, "data:", 5) ? cJSON_ParseWithOpts(line + 5, NULL, true) : NULL;
        if (json && rpc_has_id(json, id)) return json;
        cJSON_Delete(json);
        line = end ? end + 1 : NULL;
    }
    return NULL;
}

static int post(const char *json) {
    if (mcp.session[0]) esp_http_client_set_header(mcp.client, "Mcp-Session-Id", mcp.session);
    if (mcp.version[0]) esp_http_client_set_header(mcp.client, "MCP-Protocol-Version", mcp.version);
    esp_http_client_set_post_field(mcp.client, json, (int)strlen(json));
    int status = request(mcp.client);
    esp_http_client_set_post_field(mcp.client, NULL, 0);
    return status;
}

static bool initialize_mcp(const char *url) {
    const esp_http_client_config_t config = {.url = url, .timeout_ms = 2800, .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = receive_http,
        .buffer_size = 1536, .buffer_size_tx = 1024, .keep_alive_enable = true};
    mcp.client = esp_http_client_init(&config);
    if (!mcp.client) return false;
    esp_http_client_set_method(mcp.client, HTTP_METHOD_POST);
    esp_http_client_set_header(mcp.client, "Accept", "application/json, text/event-stream");
    esp_http_client_set_header(mcp.client, "Content-Type", "application/json");
    static const char init[] =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":"
        "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"candypin\",\"version\":\"0.5.1-CandyPin\"}}}";
    if (post(init) != 200) return false;
    cJSON *json = rpc_result(1);
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(result, "protocolVersion");
    bool ok = cJSON_IsString(version) && (!strcmp(version->valuestring, "2025-06-18") ||
        !strcmp(version->valuestring, "2025-03-26") || !strcmp(version->valuestring, "2024-11-05"));
    if (ok) memcpy(mcp.version, version->valuestring, sizeof mcp.version);
    cJSON_Delete(json);
    if (!ok || post("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}") != 202) return false;
    mcp.sequence = 1;
    return true;
}

bool snake_read_mcp(const char *url, snake_sample_t *sample) {
    if (!url[0]) return false;
    if (!mcp.client && !initialize_mcp(url)) {
        snake_close_mcp();
        return false;
    }
    char payload[160];
    unsigned id = ++mcp.sequence;
    snprintf(payload, sizeof payload, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"get_port_details\",\"arguments\":{}}}", id);
    int status = post(payload);
    if (status != 200) {
        snake_close_mcp();
        return false;
    }
    cJSON *json = rpc_result(id);
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
    bool ok = false;
    if (result && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "isError"))) {
        const cJSON *structured = cJSON_GetObjectItemCaseSensitive(result, "structuredContent");
        if (cJSON_IsObject(structured)) ok = read_ports(structured, true, sample);
        else {
            const cJSON *content = cJSON_GetObjectItemCaseSensitive(result, "content");
            const cJSON *item;
            cJSON_ArrayForEach(item, content) {
                const cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
                if (cJSON_IsString(text) && snake_decode_ports(text->valuestring, true, sample)) {
                    ok = true;
                    break;
                }
            }
        }
    }
    cJSON_Delete(json);
    return ok;
}
