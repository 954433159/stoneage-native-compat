#ifndef STONE_APP_PET_SKILL_OPTIONS_H
#define STONE_APP_PET_SKILL_OPTIONS_H
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Optional ASCII fields preserve legacy numeric-only native skill records. */
static inline int stone_pet_option(const char *text, const char *key,
                                   int fallback, int minimum, int maximum)
{
    if (!text) return fallback;
    const char *at = strstr(text, key);
    if (!at) return fallback;
    at += strlen(key);
    char *end;
    long value = strtol(at, &end, 10);
    if (end == at || (*end && *end != ' ') || value < minimum || value > maximum)
        return fallback;
    return (int)value;
}

static inline int stone_pet_scale(int base, int percent)
{
    long long value = (long long)base + (long long)base * percent / 100;
    return value < 0 ? 0 : value > INT_MAX ? INT_MAX : (int)value;
}
#endif
