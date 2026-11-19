#include "art_store.h"
#include <string.h>

#define RECORD_BYTES 64U
static const uint8_t magic[8] = {'E', 'A', 'S', 'T', 'O', 'R', 'E', '1'};
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t frame_offset(unsigned slot)
{
    return slot * ART_STORE_SLOT_BYTES + ART_STORE_SECTOR_BYTES;
}

/* 0=invalid, 1=valid, -1=I/O failure. Validation streams flash into scratch. */
static int check_frame(const art_store_io_t *io, unsigned slot, uint32_t expected_crc)
{
    uint8_t bytes[512];
    uint32_t crc;
    if (!io->read(io->context, frame_offset(slot), bytes, ART_FRAME_HEADER_BYTES))
        return -1;
    if (art_frame_check_header(bytes, ART_FRAME_HEADER_BYTES, &crc) != ART_FRAME_OK || crc != expected_crc)
        return 0;
    uint32_t state = UINT32_MAX;
    for (uint32_t at = 0; at < ART_FRAME_PIXEL_BYTES;) {
        size_t n = ART_FRAME_PIXEL_BYTES - at;
        if (n > sizeof(bytes))
            n = sizeof(bytes);
        if (!io->read(io->context, frame_offset(slot) + ART_FRAME_HEADER_BYTES + at, bytes, n))
            return -1;
        state = art_crc32_update(state, bytes, n);
        at += (uint32_t)n;
    }
    return (state ^ UINT32_MAX) == crc;
}

static int check_record(const art_store_io_t *io, unsigned slot, uint32_t *generation, uint32_t *crc)
{
    uint8_t record[RECORD_BYTES];
    if (!io->read(io->context, slot * ART_STORE_SLOT_BYTES, record, sizeof(record)))
        return -1;
    if (memcmp(record, magic, sizeof(magic)) || le32(record + 12) != ART_FRAME_PACKET_BYTES)
        return 0;
    uint32_t wanted = le32(record + 20);
    memset(record + 20, 0, 4);
    if (art_crc32(record, sizeof(record)) != wanted)
        return 0;
    for (unsigned i = 24; i < sizeof(record); ++i)
        if (record[i])
            return 0;
    *generation = le32(record + 8);
    *crc = le32(record + 16);
    return check_frame(io, slot, *crc);
}

art_store_result_t art_store_open(art_store_t *store, const art_store_io_t *io)
{
    if (store)
        store->ready = false;
    if (!store || !io || !io->read || !io->write || !io->erase || io->size < ART_STORE_MIN_BYTES)
        return ART_STORE_ARGUMENT;
    *store = (art_store_t){.io = *io, .active_slot = -1};
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t gen, crc;
        int valid = check_record(io, i, &gen, &crc);
        if (valid < 0)
            return ART_STORE_IO;
        if (valid && (store->active_slot < 0 ||
                      (gen != store->generation && gen - store->generation < UINT32_C(0x80000000)))) {
            store->active_slot = (int)i;
            store->generation = gen;
            store->pixel_crc = crc;
        }
    }
    store->ready = true;
    return store->active_slot < 0 ? ART_STORE_EMPTY : ART_STORE_OK;
}

art_store_result_t art_store_save(art_store_t *store, const uint8_t *packet, size_t length)
{
    if (!store || !store->ready || !store->io.read || !store->io.write || !store->io.erase ||
        store->io.size < ART_STORE_MIN_BYTES || store->active_slot < -1 || store->active_slot > 1)
        return ART_STORE_ARGUMENT;
    art_frame_view_t frame;
    if (art_frame_parse(packet, length, &frame) != ART_FRAME_OK)
        return ART_STORE_INVALID_FRAME;
    unsigned next = store->active_slot == 0 ? 1U : 0U;
    if (!store->io.erase(store->io.context, next * ART_STORE_SLOT_BYTES, ART_STORE_SLOT_BYTES) ||
        !store->io.write(store->io.context, frame_offset(next), packet, length))
        return ART_STORE_IO;
    if (check_frame(&store->io, next, frame.pixel_crc) != 1)
        return ART_STORE_IO;
    uint8_t record[RECORD_BYTES] = {0};
    memcpy(record, magic, sizeof(magic));
    uint32_t generation = store->generation + 1U;
    put32(record + 8, generation);
    put32(record + 12, ART_FRAME_PACKET_BYTES);
    put32(record + 16, frame.pixel_crc);
    put32(record + 20, art_crc32(record, sizeof(record)));
    /* Commit last, after the complete packet has been read back successfully. */
    if (!store->io.write(store->io.context, next * ART_STORE_SLOT_BYTES, record, sizeof(record)))
        return ART_STORE_IO;
    uint32_t gen, crc;
    if (check_record(&store->io, next, &gen, &crc) != 1)
        return ART_STORE_IO;
    store->active_slot = (int)next;
    store->generation = gen;
    store->pixel_crc = crc;
    return ART_STORE_OK;
}

art_store_result_t art_store_load(art_store_t *store, uint8_t *packet, size_t capacity)
{
    if (!store || !store->ready || !packet || capacity < ART_FRAME_PACKET_BYTES || !store->io.read ||
        store->active_slot < -1 || store->active_slot > 1)
        return ART_STORE_ARGUMENT;
    if (store->active_slot < 0)
        return ART_STORE_EMPTY;
    if (!store->io.read(store->io.context, frame_offset((unsigned)store->active_slot), packet,
                        ART_FRAME_PACKET_BYTES))
        return ART_STORE_IO;
    art_frame_view_t frame;
    if (art_frame_parse(packet, ART_FRAME_PACKET_BYTES, &frame) != ART_FRAME_OK ||
        frame.pixel_crc != store->pixel_crc) {
        memset(packet, 0, ART_FRAME_HEADER_BYTES);
        return ART_STORE_INVALID_FRAME;
    }
    return ART_STORE_OK;
}
