#pragma once
#include <stddef.h>
void *memcpy(void *dest, const void *source, size_t size);
void *memset(void *dest, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
int strcmp(const char *a, const char *b);
size_t strlen(const char *s);
char *strstr(const char *s, const char *part);

void *memmove(void *dest, const void *source, size_t size);
char *strchr(const char *s, int c);
