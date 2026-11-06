#include "snake_album.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <string.h>

static SemaphoreHandle_t lock;
static uint8_t album[DIY_SLOTS][DIY_FRAME_BYTES];
static unsigned mask, latest;
static uint32_t revision;
static const char *const keys[DIY_SLOTS] = {"photo0", "photo1", "photo2"};

void snake_album_init(void) {
    lock = xSemaphoreCreateMutex();
    if (!lock) return;
    nvs_handle_t nvs;
    if (nvs_open("snake_pixel", NVS_READONLY, &nvs) != ESP_OK) return;
    for (unsigned i = 0; i < DIY_SLOTS; ++i) {
        size_t size = sizeof album[i];
        if (nvs_get_blob(nvs, keys[i], album[i], &size) == ESP_OK && snake_pixels_valid(album[i], size))
            mask |= 1u << i;
    }
    nvs_close(nvs);
}

bool snake_album_save(unsigned slot, const uint8_t *data, size_t size) {
    if (!lock || slot >= DIY_SLOTS || !snake_pixels_valid(data, size)) return false;
    nvs_handle_t nvs;
    if (nvs_open("snake_pixel", NVS_READWRITE, &nvs) != ESP_OK) return false;
    bool saved = nvs_set_blob(nvs, keys[slot], data, size) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    if (!saved) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(album[slot], data, size);
    __atomic_store_n(&latest, slot, __ATOMIC_RELAXED);
    __atomic_or_fetch(&mask, 1u << slot, __ATOMIC_RELEASE);
    __atomic_add_fetch(&revision, 1, __ATOMIC_RELEASE);
    xSemaphoreGive(lock);
    return true;
}

unsigned snake_album_mask(void) {
    return __atomic_load_n(&mask, __ATOMIC_ACQUIRE);
}

uint32_t snake_album_revision(void) {
    return __atomic_load_n(&revision, __ATOMIC_ACQUIRE);
}

unsigned snake_album_latest(void) {
    return __atomic_load_n(&latest, __ATOMIC_RELAXED);
}

snake_album_view_t snake_album_decode(unsigned slot, unsigned reveal, uint16_t *pixels, unsigned stride) {
    if (!lock || slot >= DIY_SLOTS) return DIY_EMPTY;
    if (xSemaphoreTake(lock, 0) != pdTRUE) return DIY_BUSY;
    bool ready = snake_album_mask() & (1u << slot);
    if (ready) snake_pixels_decode(album[slot], slot, reveal, pixels, stride);
    xSemaphoreGive(lock);
    return ready ? DIY_READY : DIY_EMPTY;
}
