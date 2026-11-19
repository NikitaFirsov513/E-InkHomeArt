#include "art_frame.h"
#include <string.h>

static const uint8_t magic[8] = {'E', 'I', 'N', 'K', 'A', 'R', 'T', 0};
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8));
}

uint32_t art_crc32_update(uint32_t state, const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        state ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ (UINT32_C(0xedb88320) & (0U - (state & 1U)));
    }
    return state;
}

uint32_t art_crc32(const uint8_t *bytes, size_t length)
{
    return art_crc32_update(UINT32_MAX, bytes, length) ^ UINT32_MAX;
}

art_frame_result_t art_frame_check_header(const uint8_t *header, size_t length, uint32_t *pixel_crc)
{
    if (pixel_crc)
        *pixel_crc = 0;
    if (!header || !pixel_crc)
        return ART_FRAME_ARGUMENT;
    if (length != ART_FRAME_HEADER_BYTES)
        return ART_FRAME_SIZE;
    if (memcmp(header, magic, sizeof(magic)))
        return ART_FRAME_MAGIC;
    if (le16(header + 8) != 1 || le16(header + 10) != ART_FRAME_HEADER_BYTES)
        return ART_FRAME_VERSION;
    if (le16(header + 12) != ART_FRAME_WIDTH || le16(header + 14) != ART_FRAME_HEIGHT)
        return ART_FRAME_GEOMETRY;
    if (header[16] != ART_FRAME_FORMAT_GRAY4_MSB || header[17] || le16(header + 18) ||
        le32(header + 20) != ART_FRAME_PIXEL_BYTES)
        return ART_FRAME_FORMAT;
    for (unsigned i = 32; i < ART_FRAME_HEADER_BYTES; ++i)
        if (header[i])
            return ART_FRAME_FORMAT;
    uint8_t copy[ART_FRAME_HEADER_BYTES];
    memcpy(copy, header, sizeof(copy));
    memset(copy + 28, 0, 4);
    if (art_crc32(copy, sizeof(copy)) != le32(header + 28))
        return ART_FRAME_HEADER_CRC;
    *pixel_crc = le32(header + 24);
    return ART_FRAME_OK;
}

art_frame_result_t art_frame_parse(const uint8_t *packet, size_t length, art_frame_view_t *view)
{
    if (view)
        *view = (art_frame_view_t){0};
    if (!packet || !view)
        return ART_FRAME_ARGUMENT;
    if (length != ART_FRAME_PACKET_BYTES)
        return ART_FRAME_SIZE;
    uint32_t crc;
    art_frame_result_t result = art_frame_check_header(packet, ART_FRAME_HEADER_BYTES, &crc);
    if (result != ART_FRAME_OK)
        return result;
    if (art_crc32(packet + ART_FRAME_HEADER_BYTES, ART_FRAME_PIXEL_BYTES) != crc)
        return ART_FRAME_PIXEL_CRC;
    view->pixels = packet + ART_FRAME_HEADER_BYTES;
    view->pixel_crc = crc;
    return ART_FRAME_OK;
}

art_frame_result_t art_frame_encode(uint8_t *packet, size_t length, const uint8_t *pixels,
                                    size_t pixel_length)
{
    if (!packet || !pixels)
        return ART_FRAME_ARGUMENT;
    if (length != ART_FRAME_PACKET_BYTES || pixel_length != ART_FRAME_PIXEL_BYTES)
        return ART_FRAME_SIZE;
    /* Input may be exactly packet+64; other overlapping buffers are not supported. */
    if (pixels != packet + ART_FRAME_HEADER_BYTES)
        memcpy(packet + ART_FRAME_HEADER_BYTES, pixels, pixel_length);
    memset(packet, 0, ART_FRAME_HEADER_BYTES);
    memcpy(packet, magic, sizeof(magic));
    put16(packet + 8, 1);
    put16(packet + 10, ART_FRAME_HEADER_BYTES);
    put16(packet + 12, ART_FRAME_WIDTH);
    put16(packet + 14, ART_FRAME_HEIGHT);
    packet[16] = ART_FRAME_FORMAT_GRAY4_MSB;
    put32(packet + 20, ART_FRAME_PIXEL_BYTES);
    put32(packet + 24, art_crc32(packet + ART_FRAME_HEADER_BYTES, pixel_length));
    put32(packet + 28, art_crc32(packet, ART_FRAME_HEADER_BYTES));
    return ART_FRAME_OK;
}
