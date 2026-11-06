#pragma once

#include "esp_netif.h"
#include <stdbool.h>

bool snake_portal_start(esp_netif_t *ap);
void snake_portal_stop(void);
