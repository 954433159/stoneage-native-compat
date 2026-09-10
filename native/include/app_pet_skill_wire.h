#ifndef STONE_APP_PET_SKILL_WIRE_H
#define STONE_APP_PET_SKILL_WIRE_H
#include <stddef.h>
#include <string.h>

/* The 20260805 receiver uses six fields per skill (0x874c7e), with seven
 * active and four passive slots. Native W/Y/O producers retain five fields
 * and seven active skills. Adapt once, before physical/controlled routing.
 * The extra numeric client field stays zero; no passive skills are invented. */
static inline int stone_app_pet_skill_wire(const char *source, char *output, size_t capacity)
{
    if (!source || source[0] != 'W') return 0;
    if (source[1] < '0' || source[1] > '4' || source[2] != '|') return -1;
    size_t length = 0, bars = 0;
    while (length < capacity && source[length]) {
        if (source[length] == '|') ++bars;
        ++length;
    }
    if (length == capacity || !length || source[length - 1] != '|') return -1;
    /* Already adapted script frames must not receive a second extra field. */
    if (bars == 43 || bars == 67) return 0;
    if (bars != 36 || capacity < 39 || length > capacity - 39) return -1;
    memcpy(output, source, 3);
    size_t used = 3;
    const char *read = source + 3;
    for (int skill = 0; skill < 7; ++skill) {
        for (int field = 0; field < 5; ++field) {
            const char *end = strchr(read, '|');
            if (!end) return -1;
            size_t amount = (size_t)(end + 1 - read);
            memcpy(output + used, read, amount);
            used += amount;
            read = end + 1;
        }
        memcpy(output + used, "0|", 2);
        used += 2;
    }
    memset(output + used, '|', 24);
    output[used + 24] = 0;
    return 1;
}
#endif
