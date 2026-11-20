#pragma once
#include <stdint.h>
typedef struct { uint16_t code; uint32_t offset; uint8_t width; } home_font_glyph_t;
typedef struct { uint8_t height; uint16_t count; const home_font_glyph_t *glyphs; const uint8_t *bits; } home_font_t;
extern const home_font_t home_font_18,home_font_20,home_font_28;
