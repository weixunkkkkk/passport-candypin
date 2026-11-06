#pragma once

#include <stddef.h>
#include <stdint.h>

enum { SNAKE_SETUP_DNS_CAPACITY = 288 };

// Replace a standard one-question DNS request with a reply for the setup AP.
// The buffer must hold SNAKE_SETUP_DNS_CAPACITY bytes. Zero means drop the packet.
size_t snake_setup_dns_reply(uint8_t packet[SNAKE_SETUP_DNS_CAPACITY], size_t request_size);
