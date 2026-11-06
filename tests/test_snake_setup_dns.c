#include "snake_setup_dns.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t probe[] = {
    0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    7, 'c', 'a', 'p', 't', 'i', 'v', 'e', 5, 'a', 'p', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0,
    0, 1, 0, 1,
};

int main(void) {
    uint8_t packet[SNAKE_SETUP_DNS_CAPACITY] = {0};
    memcpy(packet, probe, sizeof probe);
    packet[11] = 1; // A probe can include an EDNS OPT record.
    packet[sizeof probe + 2] = 41;
    size_t size = snake_setup_dns_reply(packet, sizeof probe + 11);
    assert(size == sizeof probe + 16);
    assert(packet[0] == 0x12 && packet[1] == 0x34 && packet[2] == 0x85);
    assert(packet[7] == 1 && packet[11] == 0);
    assert(!memcmp(packet + 12, probe + 12, sizeof probe - 12));
    const uint8_t a_record[] = {0xc0, 12, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1};
    assert(!memcmp(packet + sizeof probe, a_record, sizeof a_record));

    memcpy(packet, probe, sizeof probe);
    packet[sizeof probe - 3] = 28; // IPv6 probe gets no address.
    assert(snake_setup_dns_reply(packet, sizeof probe) == sizeof probe);
    assert(packet[7] == 0);

    memcpy(packet, probe, sizeof probe);
    for (size_t i = 0; i < sizeof probe; ++i) assert(snake_setup_dns_reply(packet, i) == 0);
    packet[12] = 0xc0; // The only question must contain the complete name.
    assert(snake_setup_dns_reply(packet, sizeof probe) == 0);
    memcpy(packet, probe, sizeof probe);
    packet[5] = 2;
    assert(snake_setup_dns_reply(packet, sizeof probe) == 0);
    memcpy(packet, probe, sizeof probe);
    packet[2] = 0x81;
    assert(snake_setup_dns_reply(packet, sizeof probe) == 0);

    memset(packet, 'a', sizeof packet);
    memcpy(packet, probe, 12);
    packet[12] = packet[76] = packet[140] = 63;
    packet[204] = 61;
    packet[266] = 0;
    memcpy(packet + 267, probe + sizeof probe - 4, 4);
    assert(snake_setup_dns_reply(packet, 271) == 287);
    memcpy(packet, probe, 12);
    packet[204] = 62;
    packet[267] = 0;
    memcpy(packet + 268, probe + sizeof probe - 4, 4);
    assert(snake_setup_dns_reply(packet, 272) == 0);
    puts("Setup DNS tests: PASS");
    return 0;
}
