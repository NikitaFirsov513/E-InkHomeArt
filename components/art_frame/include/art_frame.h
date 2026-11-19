#pragma once
#include <stddef.h>
#include <stdint.h>

#define ART_FRAME_WIDTH 800U
#define ART_FRAME_HEIGHT 480U
#define ART_FRAME_HEADER_BYTES 64U
#define ART_FRAME_PIXEL_BYTES (ART_FRAME_WIDTH * ART_FRAME_HEIGHT / 4U)
#define ART_FRAME_PACKET_BYTES (ART_FRAME_HEADER_BYTES + ART_FRAME_PIXEL_BYTES)
#define ART_FRAME_FORMAT_GRAY4_MSB 1U

typedef enum {
    ART_FRAME_OK,
    ART_FRAME_ARGUMENT,
    ART_FRAME_SIZE,
    ART_FRAME_MAGIC,
    ART_FRAME_VERSION,
    ART_FRAME_GEOMETRY,
    ART_FRAME_FORMAT,
    ART_FRAME_HEADER_CRC,
    ART_FRAME_PIXEL_CRC,
} art_frame_result_t;

typedef struct {
    const uint8_t *pixels; /* Borrowed; valid only while the input packet lives. */
    uint32_t pixel_crc;
} art_frame_view_t;

/* IEEE CRC-32: start with UINT32_MAX, update chunks, then XOR UINT32_MAX. */
uint32_t art_crc32_update(uint32_t state, const uint8_t *bytes, size_t length);
uint32_t art_crc32(const uint8_t *bytes, size_t length);
/* Pure byte protocol. No packed/unaligned struct casts; all integers are LE. */
art_frame_result_t art_frame_check_header(const uint8_t *header, size_t length, uint32_t *pixel_crc);
art_frame_result_t art_frame_parse(const uint8_t *packet, size_t length, art_frame_view_t *view);
art_frame_result_t art_frame_encode(uint8_t *packet, size_t length, const uint8_t *pixels,
                                    size_t pixel_length);
