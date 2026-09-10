#include <assert.h>
#include "../native/app_role.c"

char PersonalKey[4096];
static int profile = 4, connected = 1, emitted, packet_type;
static int logged_in = 1, client_connection = 1, character_fd = 7, primary_slot = 2, roster_available = 1;
static int integers[3], integer_count;
static char payload[2048];
int stone_app_virtual(int fd) { return 0; }
int stone_app_primary_slot(int fd) { return primary_slot; }
int stone_app_control_roster(int fd, char *out, size_t size) {
    if (!roster_available) return 0;
    size_t used = snprintf(out, size, "L|1|");
    for (int slot = 1; slot <= 5; ++slot) {
        if (slot == primary_slot)
            used += snprintf(out + used, size - used, "%d|NativeRole|23|2|3|1|0||1|1|native-role-uid|%d|0|",
                slot, primary_slot);
        else used += snprintf(out + used, size - used, "%d|-1|-1|-1|-1|-1|-1|-1|-1|0|-1|-1|0|", slot);
    }
    return used < size;
}
int stone_app_control_owned(int fd, int slot) { return slot == primary_slot || slot == 1; }
int stone_app_control_request(int fd, const char *request) { return 0; }
int CONNECT_checkfd(int fd) { return fd == 7 && connected; }
int CONNECT_getloginmark(int fd) { return profile; }
int CONNECT_getCharaindex(int fd) { return connected ? 2 : -1; }
BOOL CONNECT_isCLI(int fd) { return client_connection; }
BOOL CONNECT_isLOGIN(int fd) { return logged_in; }
BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return index == 2; }
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    assert(index == 2 && field == CHAR_WORKFD); return character_fd;
}
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    assert(index == 2);
    if (field == CHAR_WHICHTYPE) return CHAR_TYPEPLAYER;
    if (field == CHAR_BASEIMAGENUMBER) return 100010;
    if (field == CHAR_FACEIMAGENUMBER) return 23;
    if (field == CHAR_LV) return 3;
    if (field == CHAR_TRANSMIGRATION) return 2;
    return 0;
}
char *CHAR_getUseName(int index) { assert(index == 2); return "NativeRole"; }
char *_CHAR_getChar(char *file, int line, int index, CHAR_DATACHAR field) { return "native-role-uid"; }
char *makeEscapeString(char *source, char *destination, int length) {
    snprintf(destination, length, "%s", source); return destination;
}
void CONNECT_getCdkey(int fd, char *out, int capacity) { snprintf(out, capacity, "fixture"); }
char *getRandDefaultKey(int fd) { return "key"; }
int util_mkstring(int fd, char *buffer, char *value) {
    snprintf(payload, sizeof(payload), "%s", value); return strlen(value);
}
int util_mkint(int fd, char *buffer, int value) {
    assert(fd == 7 && integer_count < 3);
    integers[integer_count++] = value; return value;
}
void _util_SendMesg(char *file, int line, int fd, int function, char *buffer) {
    assert(fd == 7); ++emitted; packet_type = function;
    if (function == 2024) assert(integer_count == 1 && integers[0] == strlen(payload));
    else if (function == 2028) assert(integer_count == 2 && integers[0] == integers[1]);
    else assert(function == 2029 && integer_count == 3 && integers[2] == integers[0] + integers[1]);
    integer_count = 0;
}

int main(void) {
    assert(stone_app_role_roster(7));
    assert(emitted == 1 && packet_type == 2024);
    char *fields[80], *p = payload, *bar;
    int count = 0;
    while ((bar = strchr(p, '|'))) { *bar = 0; fields[count++] = p; p = bar + 1; }
    assert(count == 2 + 5 * 13 && !*p);
    assert(!strcmp(fields[0], "L") && atoi(fields[1]) == 1);
    assert(atoi(fields[15]) == 2 && !strcmp(fields[16], "NativeRole"));
    assert(atoi(fields[17]) == 23 && atoi(fields[18]) == 2 && atoi(fields[19]) == 3);
    assert(atoi(fields[26]) == 2); /* Target 0x531647 compares this owner to P token 41. */
    for (int slot = 0; slot < 5; ++slot) {
        if (slot == 1) continue;
        assert(atoi(fields[2 + slot * 13]) == slot + 1);
        assert(!strcmp(fields[3 + slot * 13], "-1"));
        assert(atoi(fields[4 + slot * 13]) == -1);
        assert(atoi(fields[11 + slot * 13]) == 0);
        assert(atoi(fields[13 + slot * 13]) == -1);
    }
    profile = 2; assert(!stone_app_role_roster(7));
    profile = 4; connected = 0; assert(!stone_app_role_roster(7));
    connected = 1; assert(!stone_app_role_roster(8));
    assert(emitted == 1);
    roster_available = 0; assert(!stone_app_role_roster(7) && emitted == 1); roster_available = 1;
    stone_app_role_request(7, "L|");
    assert(emitted == 2);
    stone_app_role_request(7, "I|5|"); /* No access to an unowned role. */
    assert(emitted == 2);
    /* The request's socket is the Windows client's handle, not a server fd or
     * character selector. Enable all OWNED roles through the target's AI reply. */
    stone_app_role_request(7, "AB|2948|6|");
    assert(emitted == 3 && packet_type == 2029 && integers[0] == 2 && integers[1] == 2);
    stone_app_role_request(7, "AB|123456|1|");
    assert(emitted == 4 && integers[0] == 1 && integers[1] == 1);
    const char *invalid[] = {"AB|2948|3|", "AB|2948|5|", "AB|2948|0|", "AB|2948|7|",
        "AB||6|", "AB|x|6|", "AB|-1|6|", "AB|2147483648|6|", "AB|2948|6|extra",
        "AB|2948|6", "AB|2948|66|", "AB|2948|1|6|", "AB|2948|", "AB|", "AB"};
    for (size_t n = 0; n < sizeof(invalid)/sizeof(invalid[0]); ++n)
        stone_app_role_request(7, invalid[n]);
    assert(emitted == 4);
    profile = 2; stone_app_role_request(7, "AB|2948|6|"); profile = 4;
    logged_in = 0; stone_app_role_request(7, "AB|2948|6|"); logged_in = 1;
    client_connection = 0; stone_app_role_request(7, "AB|2948|6|"); client_connection = 1;
    character_fd = 9; stone_app_role_request(7, "AB|2948|6|"); character_fd = 7;
    connected = 0; stone_app_role_request(7, "AB|2948|6|"); connected = 1;
    stone_app_role_request(8, "AB|2948|6|");
    assert(emitted == 4);
    /* No server automation state survives reconnect or can issue commands. */
    stone_app_role_request(7, "L|");
    assert(emitted == 5 && packet_type == 2024);
    stone_app_role_request(7, "KS|2948|1|");
    assert(emitted == 6 && packet_type == 2028 && integers[0] == 1);
    stone_app_role_request(7, "KS|2948|0|");
    assert(emitted == 7 && packet_type == 2028 && integers[0] == 0);
    stone_app_role_request(7, "KS|2948|2|");
    stone_app_role_request(7, "KS|-1|1|");
    stone_app_role_request(7, "KS|2147483648|1|");
    assert(emitted == 7);
    stone_app_role_request(7, "AB|2948|2|");
    assert(emitted == 8 && integers[0] == 2 && integers[1] == 1);
    primary_slot = 5; stone_app_role_request(7, "AB|2948|6|");
    assert(emitted == 9 && integers[0] == 5 && integers[1] == 2);
    puts("PASS: native role roster, client AI replies, checksum, malformed requests and authenticated owner isolation");
    return 0;
}
