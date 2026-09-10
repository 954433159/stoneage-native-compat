#include <assert.h>
#include "../native/overrides/gmsv/lssproto_serv.c"

char PersonalKey[4096];
static char types[32];
static int fields, sent;
int CONNECT_checkfd(int fd) { return fd == 5; }
void CONNECT_getCdkey(int fd, char *out, int capacity) { snprintf(out, capacity, "local-fixture"); }
char *getRandDefaultKey(int fd) { return "fixture-key"; }
int CONNECT_getCharaindex(int fd) { return 0; }
int CONNECT_getloginmark(int fd) { return 4; }
BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return TRUE; }
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) { return 0; }
int util_mkint(int fd, char *buffer, int value) { types[fields++]='I'; return value; }
int util_mkstring(int fd, char *buffer, char *value) { types[fields++]='S'; return strlen(value); }
void _util_SendMesg(char *file, int line, int fd, int function, char *buffer) {
    assert(function == LSSPROTO_TK_SEND);
    sent++;
}

int main(void) {
    lssproto_TK_send(5,0,"P|Shitou:你好",0,"","fixture-uid",0,0,1,0);
    assert(sent == 1);
    assert(fields == 7 && !memcmp(types,"ISISSII",7));
    puts("PASS: App chat reply has the six reference client fields followed by checksum");
    return 0;
}
