#include "wire_charset.h"
#include <iconv.h>
#include <string.h>

int stone_convert_text(const char *source, char *destination, size_t capacity,
                       const char *from, const char *to) {
    if (!source || !destination || !capacity || capacity == (size_t)-1) return -1;
    destination[0] = '\0';
    iconv_t converter = iconv_open(to, from);
    if (converter == (iconv_t)-1) return -1;
    char *input = (char *)source, *output = destination;
    size_t input_left = strlen(source), output_left = capacity - 1;
    size_t result = iconv(converter, &input, &input_left, &output, &output_left);
    iconv_close(converter);
    if (result == (size_t)-1 || input_left) {
        destination[0] = '\0';
        return -1;
    }
    *output = '\0';
    return (int)(output - destination);
}
