#include "version.h"
#include "char.h"
#include "char_base.h"
#include "net.h"
#include "autil.h"
#include "app_role.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

int stone_app_role_roster(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4) return 0;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER) return 0;

    char roster[2048], buffer[8192] = "";
    /* Target 0x86320a: five slots, 13 fields each. The active native player
     * occupies its account's fixed slot; the owner matches P token 41.
     * Reference-server comparison: face (not world sprite), rebirth then level;
     * empty slots retain their slot number and use -1 for absent identities.
     * Fields: slot, name, face, rebirth, level, online, primary controller,
     *         companion label, server line, present, UID, owner, reserved.
     */
    if (!stone_app_control_roster(fd, roster, sizeof(roster))) return 0;
    CONNECT_getCdkey(fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(fd));
    int checksum = util_mkstring(fd, buffer, roster);
    util_mkint(fd, buffer, checksum);
    util_SendMesg(fd, 2024, buffer);
    return 1;
}

static int socket_argument(const char *arguments, int max_value)
{
    const char *p = arguments;
    if (*p < '0' || *p > '9') return -1;
    int hint = 0;
    while (*p >= '0' && *p <= '9') {
        int digit = *p++ - '0';
        if (hint > (INT_MAX - digit) / 10) return -1;
        hint = hint * 10 + digit;
    }
    if (*p++ != '|') return -1;
    if (*p < '0' || *p > '0' + max_value || p[1] != '|' || p[2] != '\0') return -1;
    return *p - '0';
}

void stone_app_auto_send(int fd, int slot, int mode)
{
    char buffer[256] = "";
    CONNECT_getCdkey(fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(fd));
    int checksum = util_mkint(fd, buffer, slot);
    checksum += util_mkint(fd, buffer, mode);
    util_mkint(fd, buffer, checksum);
    util_SendMesg(fd, 2029, buffer);
}

static void stone_app_auto_request(int fd, const char *arguments)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 ||
        !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd)) return;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
        CHAR_getWorkInt(index, CHAR_WORKFD) != fd) return;

    /* Target 0x7c5c03 sends AB|<Windows socket>|6| for one-key enable;
     * 0x7c5354 sends AB|<Windows socket>|<role slot>| for individual enable.
     * The socket is only a client-side hint. Never resolve a server connection
     * or player from it. Value 6 means all slots owned by this native player.
     */
    int slot = socket_argument(arguments, 6);
    int all_owned = slot == 6;
    if (slot < 1 || (!all_owned && !stone_app_control_owned(fd, slot))) return;

    /* Target 2029 -> 0x876f40: mode=1 enables one client AI;
     * mode=2 enables its owned slots and the one-key indicator. The original
     * client chooses and sends normal battle commands. Cancellation clears
     * its AI locally (0x7c5c3f); no server/offline automation state is created.
     */
    stone_app_auto_send(fd, all_owned ? stone_app_primary_slot(fd) : slot, all_owned ? 2 : 1);
}

void stone_app_role_request(int fd, const char *request)
{
    if (!request) return;
    if (!CONNECT_checkfd(fd) || !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd) ||
        CONNECT_getloginmark(fd) != 4 || stone_app_virtual(fd)) return;
    if (!strncmp(request, "AB|", 3)) {
        stone_app_auto_request(fd, request + 3);
    } else if (!strncmp(request, "KS|", 3)) {
        int enabled = socket_argument(request + 3, 1);
        if (enabled < 0) return;
        /* The original client drives all owned characters in its fast state
         * machine; server rounds, awards and save paths remain native. */
        char buffer[128] = "";
        CONNECT_getCdkey(fd, PersonalKey, 4096);
        strcat(PersonalKey, getRandDefaultKey(fd));
        util_mkint(fd, buffer, enabled);
        util_mkint(fd, buffer, enabled);
        util_SendMesg(fd, 2028, buffer);
    } else if (!strcmp(request, "L") || !strncmp(request, "L|", 2)) {
        stone_app_role_roster(fd);
    } else if (!stone_app_control_request(fd, request)) {
        /* Record only the operation tag; arguments may contain private data. */
        fprintf(stderr, "[local] unsupported role operation=%.2s\n", request);
    }
}
