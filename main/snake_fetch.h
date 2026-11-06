#pragma once

#include "snake_model.h"

enum { SNAKE_MCP_URL_MAX = 256 };

bool snake_read_lan(const char *ipv4, unsigned port, snake_sample_t *sample);
bool snake_read_mcp(const char *url, snake_sample_t *sample);
void snake_close_mcp(void);
bool snake_decode_ports(const char *json, bool mcp, snake_sample_t *sample);
