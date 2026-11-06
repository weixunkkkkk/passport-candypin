#include "snake_setup_dns.h"

#include <stdbool.h>
#include <string.h>

size_t snake_setup_dns_reply(uint8_t packet[SNAKE_SETUP_DNS_CAPACITY], size_t request_size) {
    enum { HEADER = 12, MAX_NAME = 255, QUESTION_TAIL = 4 };
    if (request_size < HEADER + 1 + QUESTION_TAIL || request_size > SNAKE_SETUP_DNS_CAPACITY ||
        (packet[2] & 0xfeu) || packet[4] != 0 || packet[5] != 1) return 0;

    size_t end = HEADER;
    for (;;) {
        if (end >= request_size || end - HEADER >= MAX_NAME) return 0;
        unsigned label = packet[end++];
        if (!label) break;
        if (label > 63 || label > request_size - end) return 0;
        end += label;
    }
    if (request_size - end < QUESTION_TAIL || packet[end + 2] != 0 || packet[end + 3] != 1) return 0;

    bool wants_ipv4 = packet[end] == 0 && packet[end + 1] == 1;
    end += QUESTION_TAIL;
    packet[2] = 0x84u | (packet[2] & 1u); // Reply from the setup network, preserve RD.
    packet[3] = 0;
    memset(packet + 6, 0, HEADER - 6); // Discard additional records, including EDNS.
    if (!wants_ipv4) return end;

    // Question-name pointer, A, IN, TTL 0, 4 address bytes. A zero TTL avoids
    // carrying the portal's DNS answer over to the phone's usual network.
    static const uint8_t answer[] = {0xc0, HEADER, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1};
    packet[7] = 1;
    memcpy(packet + end, answer, sizeof answer);
    return end + sizeof answer;
}
