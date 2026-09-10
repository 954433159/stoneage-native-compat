#ifndef STONE_WIRE_CHARSET_H
#define STONE_WIRE_CHARSET_H
#include <stddef.h>

int stone_convert_text(const char *source, char *destination, size_t capacity,
                       const char *from, const char *to);
#endif
