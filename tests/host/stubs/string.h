#pragma once
#include <stddef.h>
void *memcpy(void *dest, const void *source, size_t size);
void *memset(void *dest, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
