#include <assert.h>
#include "../native/overrides/gmsv/item/item.c"

int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) { return 0; }
int CONNECT_getloginmark(int fd) { return 4; }
int FreeJudgePlayer(int charaindex) { return 1; }
void strcpysafe(char *destination, size_t length, const char *source) {
    snprintf(destination, length, "%s", source);
}
char *makeEscapeString(char *source, char *destination, int length) {
    snprintf(destination, length, "%s", source);
    return destination;
}
int FreeGetItemStar(int itemindex) { return 0; }
int MAGIC_getMagicArray(int id) { return -1; }
char *MAGIC_getChar(int index, MAGIC_DATACHAR field) { return ""; }

static size_t separators(const char *text) {
    size_t count = 0;
    for (; *text; ++text) if (*text == '|') ++count;
    return count;
}

int main(void) {
    const char *empty = ITEM_makeItemFalseString(0);
    size_t bars = separators(empty);
    printf("App empty item has %zu separators (reference S/I item stride is 21)\n", bars);
    fflush(stdout);
    assert(bars == 20);
    assert(separators(ITEM_makeItemFalseStringWithNum(0, 9)) == 21);
    static ITEM_exists fixture;
    ITEM_item = &fixture;
    ITEM_itemnum = 1;
    fixture.use = TRUE;
    strcpy(fixture.itm.string[ITEM_NAME].string, "fixture club");
    strcpy(fixture.itm.string[ITEM_SECRETNAME].string, "fixture club");
    fixture.itm.data[ITEM_MODIFYATTACK] = 30;
    fixture.itm.data[ITEM_BASEIMAGENUMBER] = 24000;
    strcpy(fixture.itm.string[ITEM_TYPECODE].string, "2,4");
    strcpy(fixture.itm.string[ITEM_INLAYCODE].string, "9,11");
    const char *occupied = ITEM_makeItemStatusString(0, -1, 0);
    assert(separators(occupied) == 20);
    assert(strncmp(occupied, "fixture club|", 13) == 0);
    assert(strstr(occupied, "|24000|"));
    assert(strstr(occupied, "|2,4|9,11|"));
    const char *numbered_empty = ITEM_makeItemFalseStringWithNum(0, 9);
    assert(strncmp(numbered_empty, "9|", 2) == 0);
    assert(separators(numbered_empty) == 21);
    puts("PASS: occupied, empty and numbered App items retain the reference packet stride");
    return 0;
}
