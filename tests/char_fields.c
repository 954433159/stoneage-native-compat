#include <assert.h>
#ifdef ORIGINAL_FILE
#include "../vendor/gmsv/mylua/charbase.c"
#else
#include "../native/overrides/gmsv/mylua/charbase.c"
#endif

static int find_field(CharBase *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(table[i].field, name)) return table[i].element;
    return -1;
}

int main(void) {
    assert(find_field(CharBaseChar, arraysizeof(CharBaseChar), "账号") == CHAR_CDKEY);
    assert(find_field(CharBaseChar, arraysizeof(CharBaseChar), "名字") == CHAR_NAME);
    assert(find_field(CharBaseInt, arraysizeof(CharBaseInt), "等级") == CHAR_LV);
    assert(find_field(CharBaseInt, arraysizeof(CharBaseInt), "地图号") == CHAR_FLOOR);
    assert(find_field(CharBaseInt, arraysizeof(CharBaseInt), "新玩家旗标") == CHAR_NEWPLAYERFLG);
    puts("PASS: original Lua character fields match UTF-8 scripts");
    return 0;
}
