#include <assert.h>
#ifdef ORIGINAL_FILE
#include "../vendor/gmsv/autil.c"
#else
#include "../native/overrides/gmsv/autil.c"
#endif

char *getRandDefaultKey(int fd) { (void)fd; return "local-test-key"; }

int main(void) {
    char packed[256] = "", decoded[256] = "", incoming[256] = "", output[256] = "";
    const char *text = "石器|你好";
    const char *wire = "\xCA\xAF\xC6\xF7|\xC4\xE3\xBA\xC3";
    strcpy(PersonalKey, "local-test-key");
    assert(util_mkstring(0, packed, (char *)text) == 9);
    util_shr_64to256(decoded, packed + 1, DEFAULTTABLE, PersonalKey);
    assert(!strcmp(decoded, wire));
    util_256to64_shl(incoming, (char *)wire, 9, DEFAULTTABLE, PersonalKey);
    MesgSlice[2] = incoming;
    assert(util_destring(0, 2, output) == 9);
    assert(!strcmp(output, text));
    packed[0] = 0;
    assert(util_mkstring(0, packed, "ascii") == 5);
    incoming[0] = 0;
    assert(util_destring(0, 2, output) == 0 && output[0] == 0);
#ifndef ORIGINAL_FILE
    struct { char text[5]; unsigned char guard[4]; } small = {{0}, {1,2,3,4}};
    util_256to64_shl(incoming, (char *)wire, 9, DEFAULTTABLE, PersonalKey);
    assert(util_destring(0, 2, small.text) == -1);
    assert(small.text[0] == 0 && !memcmp(small.guard, "\1\2\3\4", 4));
    util_256to64_shl(incoming, "\x81", 1, DEFAULTTABLE, PersonalKey);
    assert(util_destring(0, 2, output) == -1);
#endif
    puts("PASS: native wire strings use GBK byte checksums, UTF-8 application text, bounded decoding");
    return 0;
}
