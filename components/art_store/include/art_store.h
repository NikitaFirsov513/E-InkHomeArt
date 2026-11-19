#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "art_frame.h"

#define ART_STORE_SECTOR_BYTES 4096U
#define ART_STORE_SLOT_BYTES 131072U
#define ART_STORE_MIN_BYTES (2U * ART_STORE_SLOT_BYTES)

typedef struct {
    void *context;
    uint32_t size;
    bool (*read)(void *, uint32_t offset, void *bytes, size_t length);
    bool (*write)(void *, uint32_t offset, const void *bytes, size_t length);
    bool (*erase)(void *, uint32_t offset, size_t length);
} art_store_io_t;

typedef enum {
    ART_STORE_OK,
    ART_STORE_EMPTY,
    ART_STORE_ARGUMENT,
    ART_STORE_IO,
    ART_STORE_INVALID_FRAME
} art_store_result_t;
typedef struct {
    art_store_io_t io;
    int active_slot;
    uint32_t generation, pixel_crc;
    bool ready; /* False after a failed open: writes must remain disabled. */
} art_store_t;

/* One active picture plus a recovery slot; not a gallery. Serialize all calls.
 * No automatic formatting on errors. An interrupted save leaves a valid old or
 * new record; CRC-invalid records are never selected. Uses <= 512-byte scratch. */
art_store_result_t art_store_open(art_store_t *store, const art_store_io_t *io);
art_store_result_t art_store_save(art_store_t *store, const uint8_t *packet, size_t length);
art_store_result_t art_store_load(art_store_t *store, uint8_t *packet, size_t capacity);
