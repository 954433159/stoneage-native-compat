/* Native same-account companions. SAAC remains the only character loader,
 * account lock owner and file writer; CHAR_login/logout and BATTLE are reused.
 * eventfd reserves a real Connect index without a second network client. */
#include "version.h"
#include "char.h"
#include "char_base.h"
#include "battle.h"
#include "battle_command.h"
#include "net.h"
#include "autil.h"
#include "lssproto_serv.h"
#include "saacproto_cli.h"
#include "configfile.h"
#include "longzoro/sasql.h"
#include "app_role.h"
#include "app_mail.h"
#include "app_storage.h"
#include "app_attributes.h"
#include "app_market.h"
#include "app_features.h"
#include "trade.h"
#include "lua.h"
#include <mysql/mysql.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern MYSQL mysql;
extern lua_State *FindLua(char *filename);

enum { ROLE_IDLE, ROLE_LIST, ROLE_LOAD, ROLE_ONLINE, ROLE_SAVE };
typedef struct {
    int fd, epoch, phase, wanted, face, level, rebirth, saves, save_failed, last_state;
    long long account_id;
    char account[CDKEYLEN], password[PASSWDLEN], name[CHARNAMELEN];
    time_t since, saved;
} AppMember;
typedef struct AppGroup {
    struct AppGroup *next;
    int fd, epoch, index, closing, primary_slot, saves, switching;
    time_t handoff_until;
    long long master_id;
    AppMember member[5];
} AppGroup;
static AppGroup *groups;
static int session_busy(int fd, int index);

static int primary_valid(AppGroup *g)
{
    return !g->closing && CONNECT_checkfd(g->fd) &&
        CONNECT_getFdid(g->fd) == g->epoch && CONNECT_isLOGIN(g->fd) &&
        CONNECT_getCharaindex(g->fd) == g->index && CHAR_CHECKINDEX(g->index);
}

static AppGroup *group_for(int fd)
{
    for (AppGroup *g = groups; g; g = g->next)
        if (g->fd == fd && primary_valid(g)) return g;
    return NULL;
}

static AppGroup *member_for(int fd, int *slot)
{
    if (fd < 0) return NULL;
    for (AppGroup *g = groups; g; g = g->next)
        for (int s = 0; s < 5; ++s)
            if (s + 1 != g->primary_slot && g->member[s].fd == fd && CONNECT_checkfd(fd) &&
                g->member[s].epoch == CONNECT_getFdid(fd)) {
                if (slot) *slot = s;
                return g;
            }
    return NULL;
}

int stone_app_virtual(int fd) { return member_for(fd, NULL) != NULL; }

static void release_member(AppMember *m)
{
    int fd = m->fd;
    if (CONNECT_checkfd(fd) && CONNECT_getFdid(fd) == m->epoch) {
        int index = CONNECT_getCharaindex(fd);
        if (CHAR_CHECKINDEX(index) && CHAR_getWorkInt(index, CHAR_WORKFD) == fd) {
            fprintf(stderr, "[local] refusing to release a live companion\n");
            return;
        }
        /* A deleted native character index can already belong to a new
         * player. The closing context must never act on that reused index. */
        CONNECT_setCharaindex(fd, -1);
        /* Native close otherwise marks this account offline even when its
         * live character has just been rebound to a new authenticated fd. */
        CONNECT_setCdkey(fd, "");
    }
    /* Remove the association before native close to avoid recursive cleanup. */
    m->fd = -1;
    m->phase = ROLE_IDLE;
    m->wanted = 0;
    if (CONNECT_checkfd(fd) && CONNECT_getFdid(fd) == m->epoch)
        CONNECT_endOne_debug(fd);
}

static int member_index(AppMember *m)
{
    if (m->phase != ROLE_ONLINE || !CONNECT_checkfd(m->fd) ||
        CONNECT_getFdid(m->fd) != m->epoch || !CONNECT_isLOGIN(m->fd)) return -1;
    int index = CONNECT_getCharaindex(m->fd);
    return CHAR_CHECKINDEX(index) && CHAR_getWorkInt(index, CHAR_WORKFD) == m->fd ? index : -1;
}

static void request_list(AppMember *m)
{
    m->phase = ROLE_LIST;
    m->since = time(NULL);
    CONNECT_setState(m->fd, WHILEDOWNLOADCHARLIST);
    /* charlistflg=1 is the native read-only list path: flag 0 may kick an
     * existing account before returning a list. Never use it for discovery. */
    saacproto_ACCharList_send(acfd, m->account, m->password, "127.0.0.1",
        "", "", "", m->epoch, 1);
}

static void copy_device(int to, int from)
{
    char mac[129];
    CONNECT_getMAC(from, mac, sizeof(mac)); CONNECT_setMAC(to, mac);
    CONNECT_getMAC2(from, mac, sizeof(mac)); CONNECT_setMAC2(to, mac);
    CONNECT_getMAC3(from, mac, sizeof(mac)); CONNECT_setMAC3(to, mac);
}

static int open_member(AppMember *m, int owner_fd)
{
    if (getfdFromCdkey(m->account) >= 0) return 0;
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) return 0;
    if (fd >= ConnectLen) { close(fd); return 0; }
    initConnectOne(fd, NULL, 0);
    CONNECT_setCtype(fd, CLI);
    CONNECT_setCdkey(fd, m->account);
    CONNECT_setPasswd(fd, m->password);
    CONNECT_setloginmark(fd, 4);
    copy_device(fd, owner_fd);
    m->fd = fd;
    m->epoch = CONNECT_getFdid(fd);
    request_list(m);
    return 1;
}

static AppMember *detached_offline(AppGroup *owner, AppMember *member)
{
    if (owner->master_id <= 0 || member->account_id <= 0) return NULL;
    for (AppGroup *g = groups; g; g = g->next) {
        if (g == owner || !g->closing || g->master_id != owner->master_id) continue;
        for (int s = 0; s < 5; ++s) {
            if (s + 1 == g->primary_slot) continue;
            AppMember *old = &g->member[s];
            if (old->account_id != member->account_id || strcmp(old->account, member->account)) continue;
            int index = member_index(old);
            if (index < 0 || (CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) != 1 &&
                !(g->handoff_until >= time(NULL) && CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 2))) continue;
            return old;
        }
    }
    return NULL;
}

static AppGroup *create_group(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 ||
        !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd) || stone_app_virtual(fd)) return NULL;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
        CHAR_getWorkInt(index, CHAR_WORKFD) != fd) return NULL;
    AppGroup *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->fd = fd; g->epoch = CONNECT_getFdid(fd); g->index = index; g->primary_slot = 1;
    for (int s = 0; s < 5; ++s) g->member[s].fd = -1;
    g->next = groups; groups = g;

    char account[CDKEYLEN], escaped[CDKEYLEN * 2 + 1], query[1024];
    CONNECT_getCdkey(fd, account, sizeof(account));
    mysql_real_escape_string(&mysql, escaped, account, strlen(account));
    snprintf(query, sizeof(query),
        "SELECT child.Name,child.PassWord,child.Id=current.Id,child.Id,child.MasterId FROM csalogin child "
        "JOIN csalogin current ON current.MasterId=child.MasterId "
        "WHERE current.Name='%s' AND current.MasterId>0 "
        "ORDER BY child.Id LIMIT 5", escaped);
    if (mysql_query(&mysql, query)) return g;
    MYSQL_RES *rows = mysql_store_result(&mysql);
    if (!rows) return g;
    MYSQL_ROW row;
    int s = 0, primary_slot = 0;
    while (s < 5 && (row = mysql_fetch_row(rows))) {
        AppMember *m = &g->member[s++];
        if (row[2] && !strcmp(row[2], "1")) {
            primary_slot = s;
            if (row[4]) g->master_id = strtoll(row[4], NULL, 10);
        }
        /* Invalid sibling records leave a hole; later account IDs must not
         * move to another client's fixed slot. Credentials stay in this group. */
        if (!row[0] || !row[1] || !row[0][0] || strlen(row[0]) >= CDKEYLEN ||
            strlen(row[1]) >= PASSWDLEN) continue;
        strcpy(m->account, row[0]);
        strcpy(m->password, row[1]);
        if (row[3]) m->account_id = strtoll(row[3], NULL, 10);
    }
    mysql_free_result(rows);
    if (!primary_slot) {
        /* A failed/inconsistent lookup only exposes the authenticated player. */
        for (s = 0; s < 5; ++s) {
            memset(&g->member[s], 0, sizeof(g->member[s]));
            g->member[s].fd = -1;
        }
        return g;
    }
    g->primary_slot = primary_slot;
    snprintf(g->member[primary_slot - 1].name, CHARNAMELEN, "%s", CHAR_getChar(index, CHAR_NAME));
    for (s = 0; s < 5; ++s) {
        if (s + 1 == primary_slot || !g->member[s].account[0]) continue;
        AppMember *old = detached_offline(g, &g->member[s]);
        if (old && CHAR_getWorkInt(member_index(old), CHAR_WORK_OFFLINE) == 2) {
            g->member[s] = *old;
            memset(old, 0, sizeof(*old)); old->fd = -1;
            /* Reconnect mode has a native timeout. Once owned again, these
             * companions resume normal native control, never paid offline AI. */
            CHAR_setWorkInt(member_index(&g->member[s]), CHAR_WORK_OFFLINE, 0);
        } else open_member(&g->member[s], fd);
    }
    return g;
}

int stone_app_primary_slot(int fd)
{
    AppGroup *g = member_for(fd, NULL);
    if (!g) g = group_for(fd);
    /* CHAR_login binds the character and sets LOGIN before its first P.
     * Resolve the account order here so that P token 41 is correct immediately. */
    if (!g) g = create_group(fd);
    return g ? g->primary_slot : 1;
}

static void scoped(int fd, int protocol, int slot, const char *data)
{
    char buffer[65500] = "";
    CONNECT_getCdkey(fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(fd));
    int sum = util_mkint(fd, buffer, slot);
    sum += util_mkstring(fd, buffer, (char *)data);
    util_mkint(fd, buffer, sum);
    util_SendMesg(fd, protocol, buffer);
}

static void close_member(AppMember *m)
{
    m->wanted = 0;
    int index = member_index(m);
    if (index >= 0) {
        /* Keep this fdid alive until the SAAC save/unlock acknowledgment. */
        m->phase = ROLE_SAVE;
        m->since = time(NULL);
        CONNECT_setState(m->fd, WHILELOGOUTSAVE);
        if (!CHAR_logout(index, TRUE)) {
            m->phase = ROLE_ONLINE;
            CONNECT_setState(m->fd, LOGIN);
            return;
        }
        CONNECT_setCharaindex(m->fd, -1);
    } else if (m->fd >= 0 && m->phase != ROLE_LOAD && m->phase != ROLE_SAVE) {
        release_member(m);
    }
}

static void detach_group(AppGroup *g)
{
    g->closing = 1;
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        AppMember *m = &g->member[s];
        int index = member_index(m);
        /* Normal paid offline characters outlive their controller. Keep their
         * native loop, SAAC context and periodic saves until expiry or adoption. */
        if (index >= 0 && CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 1) continue;
        close_member(m);
    }
}

void stone_app_disconnect(int fd)
{
    int s;
    AppGroup *owner = member_for(fd, &s);
    if (owner) {
        AppMember *m = &owner->member[s];
        m->wanted = 0;
        m->phase = ROLE_SAVE;
        m->since = time(NULL);
        CONNECT_setState(fd, WHILELOGOUTSAVE);
        return;
    }
    /* CHAR_logout may already have changed LOGIN to WHILELOGOUTSAVE. The
     * unchanged fdid still identifies this owner's cleanup context. */
    for (AppGroup *g = groups; g; g = g->next)
        if (!g->closing && g->fd == fd && CONNECT_checkfd(fd) && CONNECT_getFdid(fd) == g->epoch) {
            detach_group(g);
            return;
        }
}

int stone_app_close(int fd)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) return 0;
    close_member(&g->member[s]);
    return 1;
}

void stone_app_logged_out(int fd)
{
    if (stone_app_virtual(fd)) CONNECT_setCharaindex(fd, -1);
}

static void start_load(AppMember *m)
{
    if (!m->wanted || !m->name[0] || m->phase != ROLE_IDLE || m->fd < 0) return;
    /* Native window-close keeps the character and SAAC lock in reconnect
     * mode. Reuse CharLogin for that exact disconnected sibling; never load
     * a second object, clear its lock, or steal a live/paid-offline session. */
    if (sasql_queryonline(m->account) != 0) {
        int slot, index = -1;
        AppGroup *g = member_for(m->fd, &slot);
        if (!g || &g->member[slot] != m || !primary_valid(g) ||
            m->account_id <= 0 || m->saves || m->save_failed) return;
        for (int i = 0; i < CHAR_getPlayerMaxNum(); ++i) {
            if (!CHAR_CHECKINDEX(i) || CHAR_getInt(i, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
                strcmp(CHAR_getChar(i, CHAR_CDKEY), m->account) ||
                strcmp(CHAR_getChar(i, CHAR_NAME), m->name)) continue;
            if (index >= 0 || CHAR_getWorkInt(i, CHAR_WORKFD) != -1 ||
                CHAR_getWorkInt(i, CHAR_WORK_OFFLINE) != 2 || session_busy(m->fd, i)) return;
            index = i;
        }
        if (index < 0) return;
        m->phase = ROLE_LOAD;
        m->since = time(NULL);
        CONNECT_setState(m->fd, NOTLOGIN);
        lssproto_CharLogin_recv(m->fd, m->name);
        int rebound = CONNECT_checkfd(m->fd) && CONNECT_isLOGIN(m->fd) &&
            CONNECT_getCharaindex(m->fd) == index && CHAR_getWorkInt(index, CHAR_WORKFD) == m->fd &&
            CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 0;
        stone_app_load_reply(m->fd, rebound ? "successful" : "failed");
        return;
    }
    m->phase = ROLE_LOAD;
    m->since = time(NULL);
    CONNECT_setCharname(m->fd, m->name);
    CONNECT_setState(m->fd, WHILELOGIN);
    saacproto_ACCharLoad_send(acfd, m->account, m->password, m->name, 1, "stone-control", m->epoch);
}

int stone_app_list_reply(int fd, const char *result, const char *data)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) return 0;
    AppMember *m = &g->member[s];
    if (m->phase != ROLE_LIST) return 1;
    m->phase = ROLE_IDLE;
    CONNECT_setState(fd, NOTLOGIN);
    if (!g->closing && !strcmp(result, "successful") && data && data[0]) {
        char name[CHARNAMELEN * 2], option[2048], field[64];
        if (getStringFromIndexWithDelim((char *)data, "|", 1, name, sizeof(name))) {
            makeStringFromEscaped(name);
            if (strlen(name) < sizeof(m->name)) strcpy(m->name, name);
        }
        /* SAAC loadCharNameAndOption emits name|escaped-option. Decode that
         * inner dataplace|face|level|hp record before taking its fields. */
        if (getStringFromIndexWithDelim((char *)data, "|", 2, option, sizeof(option))) {
            makeStringFromEscaped(option);
            if (getStringFromIndexWithDelim(option, "|", 2, field, sizeof(field))) m->face = atoi(field);
            if (getStringFromIndexWithDelim(option, "|", 3, field, sizeof(field))) m->level = atoi(field);
        }
        start_load(m);
    }
    if (g->closing) release_member(m);
    else stone_app_role_roster(g->fd);
    return 1;
}

static void send_member_snapshot(int index)
{
    CHAR_sendStatusString(index, "P");
    CHAR_sendStatusString(index, "I");
    for (int p = 0; p < 5; ++p) {
        char status[8];
        snprintf(status, sizeof(status), "K%d", p); CHAR_sendStatusString(index, status);
        snprintf(status, sizeof(status), "W%d", p); CHAR_sendStatusString(index, status);
    }
}

void stone_app_refresh_companions(int fd)
{
    AppGroup *g = group_for(fd);
    if (!g) return;
    stone_app_role_roster(fd);
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        int index = member_index(&g->member[s]);
        if (index >= 0) send_member_snapshot(index);
    }
}

void stone_app_load_reply(int fd, const char *result)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) return;
    AppMember *m = &g->member[s];
    if (m->phase != ROLE_LOAD) return;
    int index = CONNECT_getCharaindex(fd);
    if (!strcmp(result, "successful") && CONNECT_isLOGIN(fd) && CHAR_CHECKINDEX(index)) {
        m->phase = ROLE_ONLINE;
        m->saved = time(NULL);
        fprintf(stderr, "[local] companion login slot=%d index=%d\n", s + 1, index);
        if (!primary_valid(g) || !m->wanted) close_member(m);
        else {
            stone_app_role_roster(g->fd);
            send_member_snapshot(index);
        }
    } else {
        m->phase = ROLE_IDLE;
        m->wanted = 0;
        if (g->closing) release_member(m);
        else stone_app_role_roster(g->fd);
    }
}

int stone_app_save_reply(int fd, const char *result)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) {
        g = group_for(fd);
        if (g && g->saves > 0) --g->saves;
        return 0;
    }
    AppMember *m = &g->member[s];
    if (m->saves > 0) --m->saves;
    /* SAAC processes this connection's save requests in order. A later
     * successful complete snapshot supersedes an earlier failed autosave. */
    m->save_failed = strcmp(result, "successful") != 0;
    if (m->phase == ROLE_SAVE) {
        if (m->saves) return 1;
        if (m->save_failed) {
            fprintf(stderr, "[local] companion save failed slot=%d; keeping session\n", s + 1);
            return 1;
        }
        release_member(m);
        if (primary_valid(g)) stone_app_role_roster(g->fd);
        fprintf(stderr, "[local] companion saved slot=%d\n", s + 1);
    }
    return 1;
}

void stone_app_saving(int fd)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (g) ++g->member[s].saves;
    else if ((g = group_for(fd))) ++g->saves;
}

int stone_app_control_owned(int fd, int slot)
{
    AppGroup *g = group_for(fd);
    if (!g) g = create_group(fd);
    return g && slot >= 1 && slot <= 5 &&
        (slot == g->primary_slot || member_index(&g->member[slot - 1]) >= 0);
}

static int role_state(AppGroup *g, int index)
{
    if (CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) != 0) return 3;
    if (index != g->index && CHAR_getWorkInt(index, CHAR_WORKPARTYMODE) == CHAR_PARTY_CLIENT &&
        CHAR_getWorkInt(index, CHAR_WORKPARTYINDEX1) == g->index) return 2;
    return 1;
}

int stone_app_control_roster(int fd, char *out, size_t size)
{
    AppGroup *g = group_for(fd);
    if (!g) g = create_group(fd);
    if (!g) return 0;
    if (!out || !size) return 0;
    /* The L header and each record's line field identify the server line.
     * Reference line 6 stays 6 when the primary changes; this local line is 1. */
    size_t used = snprintf(out, size, "L|1|");
    if (used >= size) { out[0] = 0; return 0; }
    for (int s = 0; s < 5; ++s) {
        AppMember *m = &g->member[s];
        int index = s + 1 == g->primary_slot ? g->index : member_index(m);
        int owner = g->primary_slot;
        if (index < 0) {
            AppMember *detached = detached_offline(g, m);
            if (detached) {
                index = member_index(detached); owner = -1;
                snprintf(m->name, sizeof(m->name), "%s", CHAR_getUseName(index));
                m->face = CHAR_getInt(index, CHAR_FACEIMAGENUMBER);
                m->rebirth = CHAR_getInt(index, CHAR_TRANSMIGRATION);
                m->level = CHAR_getInt(index, CHAR_LV);
            }
        }
        m->last_state = index < 0 ? 0 : role_state(g, index);
        char name[160], uid[160] = "";
        if (index >= 0) {
            makeEscapeString(CHAR_getUseName(index), name, sizeof(name));
            makeEscapeString(CHAR_getChar(index, CHAR_PLAYERUID), uid, sizeof(uid));
            used += snprintf(out + used, size - used,
                "%d|%s|%d|%d|%d|%d|%d||1|1|%s|%d|0|", s + 1, name,
                CHAR_getInt(index, CHAR_FACEIMAGENUMBER), CHAR_getInt(index, CHAR_TRANSMIGRATION),
                CHAR_getInt(index, CHAR_LV), m->last_state, index == g->index, uid, owner);
        } else if (m->name[0]) {
            makeEscapeString(m->name, name, sizeof(name));
            used += snprintf(out + used, size - used,
                "%d|%s|%d|%d|%d|0|0||1|1||-1|0|", s + 1, name, m->face, m->rebirth, m->level);
        } else used += snprintf(out + used, size - used,
            "%d|-1|-1|-1|-1|-1|-1|-1|-1|0|-1|-1|0|", s + 1);
        if (used >= size) { out[0] = 0; return 0; }
    }
    return 1;
}

int stone_app_status(int fd, int protocol, const char *data)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) return 0;
    if (!primary_valid(g) || !data) return 1;
    if (protocol == 2027) scoped(g->fd, 2027, s + 1, data);
    else if (protocol == 2026 || !strncmp(data, "BP|", 3)) scoped(g->fd, 2026, s + 1, data);
    return 1;
}

int stone_app_pet_state(int fd, int protocol, int first, int result)
{
    int s;
    AppGroup *g = member_for(fd, &s);
    if (!g) return 0;
    if (!primary_valid(g)) return 1;
    if (protocol == 0) {
        int index = CONNECT_getCharaindex(fd);
        if (CHAR_CHECKINDEX(index))
            scoped(g->fd, 2026, s + 1, CHAR_makeStatusString(index, "P"));
        return 1;
    }
    char buffer[256] = "";
    fd = g->fd;
    CONNECT_getCdkey(fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(fd));
    int sum = util_mkint(fd, buffer, s + 1);
    sum += util_mkint(fd, buffer, first);
    sum += util_mkint(fd, buffer, result);
    util_mkint(fd, buffer, sum);
    util_SendMesg(fd, protocol, buffer);
    return 1;
}

int stone_app_battle_command(int fd, int position, char *command)
{
    if (CONNECT_getloginmark(fd) != 4) return 0;
    AppGroup *g = group_for(fd);
    if (!g || position < 0 || position >= 20 || !command) return 1;
    int battle = CHAR_getWorkInt(g->index, CHAR_WORKBATTLEINDEX);
    if (!BATTLE_CHECKINDEX(battle)) return 1;
    int actor = BATTLE_No2Index(battle, position);
    for (int s = 0; s < 5; ++s) {
        int index = s + 1 == g->primary_slot ? g->index : member_index(&g->member[s]);
        if (index < 0 || CHAR_getWorkInt(index, CHAR_WORKBATTLEINDEX) != battle) continue;
        /* Client 2010 uses the player's battle position even for W pet orders.
         * Feed its ordinary dispatcher so abnormal-status fallbacks and pet
         * selection operate on the correct real player. */
        if (actor == index) {
            BattleCommandDispach(s + 1 == g->primary_slot ? fd : g->member[s].fd, command);
            return 1;
        }
    }
    fprintf(stderr, "[local] rejected unowned battle position=%d\n", position);
    return 1;
}

void stone_app_battle_out(int fd)
{
    AppGroup *g = group_for(fd);
    if (!g) return;
    int battle = CHAR_getWorkInt(g->index, CHAR_WORKBATTLEINDEX);
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        int index = member_index(&g->member[s]);
        if (index >= 0 && CHAR_getWorkInt(index, CHAR_WORKBATTLEMODE) == BATTLE_CHARMODE_FINAL &&
            CHAR_getWorkInt(index, CHAR_WORKBATTLEINDEX) == battle)
            BattleEncountOut(index);
    }
}

int stone_app_control_offline(int fd, int slot)
{
    AppGroup *g = group_for(fd);
    if (!g || slot < 1 || slot > 5 || slot == g->primary_slot) return 0;
    AppMember *m = &g->member[slot - 1];
    int index = member_index(m);
    if (index < 0) return 0;
    int party = CHAR_getWorkInt(index, CHAR_WORKPARTYMODE);
    if (party != CHAR_PARTY_NONE && CHAR_getWorkInt(index, CHAR_WORKPARTYINDEX1) != g->index) return 0;
    int offline = CHAR_getWorkInt(index, CHAR_WORK_OFFLINE);
    if (offline) return offline == 1;
    if (CHAR_getWorkInt(index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE) return 0;
    lua_State *lua = FindLua("data/ablua/item/offline.lua");
    if (!lua) return 0;
    int top = lua_gettop(lua);
    lua_getglobal(lua, "OffLine");
    if (!lua_isfunction(lua, -1)) { lua_settop(lua, top); return 0; }
    lua_pushinteger(lua, index);
    int status = lua_pcall(lua, 1, 1, 0);
    int accepted = status == LUA_OK && lua_tointeger(lua, -1) == 1;
    lua_settop(lua, top);
    if (member_index(m) != index) return 0;
    /* OffLine owns eligibility, vitality, SQL and native Loop/pLoop. Its
     * ordinary socket-close request is consumed here: the virtual fd must
     * retain the live character and SAAC save context while offline AI runs. */
    if (CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 1) CONNECT_setCloseRequest(m->fd, 0);
    if (status != LUA_OK) fprintf(stderr, "[local] companion offline callback failed slot=%d\n", slot);
    return accepted && CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 1;
}

static int session_busy(int fd, int index)
{
    return !CHAR_CHECKINDEX(index) ||
        CHAR_getWorkInt(index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE ||
        CHAR_getWorkInt(index, CHAR_WORKTRADEMODE) != CHAR_TRADE_FREE ||
        CHAR_getWorkInt(index, CHAR_WORKSTREETVENDOR) > -1 ||
        stone_features_busy(fd);
}

static int switch_busy(AppGroup *g)
{
    if (g->saves || session_busy(g->fd, g->index)) return 1;
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        AppMember *m = &g->member[s];
        if (m->phase == ROLE_LIST || m->phase == ROLE_LOAD || m->phase == ROLE_SAVE || m->saves) return 1;
        int index = member_index(m);
        if (index >= 0 && session_busy(m->fd, index)) return 1;
    }
    return 0;
}

static int request_switch(AppGroup *g, int slot)
{
    if (slot < 1 || slot > 5 || slot == g->primary_slot || g->switching) return 1;
    if (member_index(&g->member[slot - 1]) < 0) {
        CHAR_talkToCli(g->index, -1, "请先登录要切换的角色。", CHAR_COLORYELLOW);
        return 1;
    }
    if (switch_busy(g)) {
        CHAR_talkToCli(g->index, -1, "角色正在战斗、交易或保存，请稍后切换。", CHAR_COLORYELLOW);
        return 1;
    }
    /* The unmodified client chooses its authenticated child from Name's slot
     * suffix, logs out, and signs in again. No gameplay opcode is redirected
     * to a different identity behind the client's connection. */
    char response[16], buffer[128] = "";
    snprintf(response, sizeof(response), "Q|%d", slot);
    CONNECT_getCdkey(g->fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(g->fd));
    int checksum = util_mkstring(g->fd, buffer, response);
    util_mkint(g->fd, buffer, checksum);
    g->switching = slot;
    g->handoff_until = time(NULL) + 60;
    util_SendMesg(g->fd, 2024, buffer);
    return 1;
}

int stone_app_switch_logout(int fd)
{
    AppGroup *g = group_for(fd);
    if (!g || !g->switching) return 0;
    if (switch_busy(g)) {
        g->switching = 0; g->handoff_until = 0;
        lssproto_CharLogout_send(fd, FAILED, "角色正在保存，请稍后切换。");
        return 1;
    }
    int index = g->index, slot = g->primary_slot;
    AppMember *m = &g->member[slot - 1];
    int virtual_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (virtual_fd < 0 || virtual_fd >= ConnectLen) {
        if (virtual_fd >= 0) close(virtual_fd);
        g->switching = 0; g->handoff_until = 0;
        lssproto_CharLogout_send(fd, FAILED, "角色连接资源不足，请稍后重试。");
        return 1;
    }
    initConnectOne(virtual_fd, NULL, 0);
    CONNECT_setCtype(virtual_fd, CLI);
    CONNECT_setCdkey(virtual_fd, m->account);
    CONNECT_setPasswd(virtual_fd, m->password);
    CONNECT_setCharname(virtual_fd, CHAR_getChar(index, CHAR_NAME));
    CONNECT_setloginmark(virtual_fd, 4);
    copy_device(virtual_fd, fd);
    CONNECT_setCharaindex(virtual_fd, index);
    CONNECT_setState(virtual_fd, LOGIN);
    CHAR_setWorkInt(index, CHAR_WORKFD, virtual_fd);
    m->fd = virtual_fd; m->epoch = CONNECT_getFdid(virtual_fd);
    m->phase = ROLE_ONLINE; m->wanted = 1; m->saved = time(NULL);
    /* Preserve native characters and SAAC locks, including party membership.
     * Mode 2 is the original reconnect state; it neither buys nor runs paid
     * offline AI. A failed handoff has a bounded native-save cleanup below. */
    g->closing = 1; g->primary_slot = 0; g->index = -1; g->fd = -1;
    for (int s = 0; s < 5; ++s) {
        int member = member_index(&g->member[s]);
        if (member >= 0 && !CHAR_getWorkInt(member, CHAR_WORK_OFFLINE)) {
            CHAR_setWorkInt(member, CHAR_WORK_OFFLINE, 2);
            CHAR_setWorkInt(member, CHAR_WORK_LOGIN_TIME, time(NULL));
        }
    }
    stone_storage_logout(fd); stone_attributes_logout(fd); stone_market_logout(fd);
    stone_mail_login(virtual_fd); stone_storage_login(virtual_fd);
    /* Acknowledgment still uses the old account's transport key. Only the
     * client's subsequent authenticated login may set its new account. */
    lssproto_CharLogout_send(fd, SUCCESSFUL, "success");
    CONNECT_setCharaindex(fd, -1);
    CONNECT_setCharname(fd, "");
    CONNECT_setCdkey(fd, "");
    CONNECT_setPasswd(fd, "");
    CONNECT_setState(fd, NOTLOGIN);
    fprintf(stderr, "[local] controller handoff from=%d to=%d\n", slot, g->switching);
    return 1;
}

int stone_app_reconnect_ready(int fd, int index)
{
    int oldfd = CHAR_getWorkInt(index, CHAR_WORKFD), slot;
    AppGroup *g = member_for(oldfd, &slot);
    if (!g) return 1;
    AppMember *m = &g->member[slot];
    char account[CDKEYLEN]; CONNECT_getCdkey(fd, account, sizeof(account));
    /* Called after ordinary password/signature authentication. An active
     * controller's member or a pending save must never be taken over. */
    int handoff = g->handoff_until >= time(NULL) && g->switching == slot + 1;
    int paid_offline = CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 1;
    return g->closing && (handoff || paid_offline) && !strcmp(account, m->account) &&
        !m->saves && !session_busy(oldfd, index);
}

int stone_app_control_request(int fd, const char *request)
{
    if (!request) return 0;
    size_t length = strlen(request);
    int all = length == 2 && request[0] == 'A' && strchr("LZTG", request[1]);
    char operation = all ? request[1] : request[0];
    int only = 0;
    if (!all) {
        if ((length != 3 && length != 4) || !strchr("BIZTOGQ", operation) || request[1] != '|' ||
            request[2] < '1' || request[2] > '5' ||
            (request[3] != 0 && !(request[3] == '|' && request[4] == 0))) return 0;
        only = request[2] - '0';
        /* Target 0x7c9b32/0x863c00: the offline row's login button sends
         * I|<fixed slot>. Reuse the same authenticated native load as B/AL;
         * I is not a controller switch and never supplies a foreign account. */
        if (operation == 'B' || operation == 'I') operation = 'L';
    }
    AppGroup *g = group_for(fd);
    if (!g) g = create_group(fd);
    if (!g) return 1;
    if (operation == 'Q') return request_switch(g, only);
    if (g->switching) return 1;
    if (CHAR_getWorkInt(g->index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE && operation != 'G') return 1;
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        if (only && only != s + 1) continue;
        AppMember *m = &g->member[s];
        if (operation == 'L') {
            if (!m->account[0]) continue;
            m->wanted = 1;
            int index = member_index(m);
            if (index >= 0 && CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 2)
                CHAR_setWorkInt(index, CHAR_WORK_OFFLINE, 0);
            if (m->fd < 0) open_member(m, fd);
            else start_load(m);
            continue;
        }
        if (operation == 'O') {
            /* Reference O explicitly logs out a detached sibling. B can then
             * reload it normally after SAAC saves/unlocks the old context. */
            AppMember *target = m->fd < 0 ? detached_offline(g, m) : NULL;
            close_member(target ? target : m);
            continue;
        }
        int index = member_index(m);
        if (index < 0 || CHAR_getWorkInt(index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE) continue;
        int party = CHAR_getWorkInt(index, CHAR_WORKPARTYMODE);
        if (party != CHAR_PARTY_NONE && CHAR_getWorkInt(index, CHAR_WORKPARTYINDEX1) != g->index) continue;
        if (operation == 'G') {
            stone_app_control_offline(fd, s + 1);
        } else if (operation == 'Z') {
            CHAR_warpToSpecificPoint(index, CHAR_getInt(g->index, CHAR_FLOOR),
                CHAR_getInt(g->index, CHAR_X), CHAR_getInt(g->index, CHAR_Y));
        } else if (operation == 'T' && party == CHAR_PARTY_NONE &&
            CHAR_getWorkInt(g->index, CHAR_WORKPARTYMODE) != CHAR_PARTY_CLIENT &&
            CHAR_getInt(index, CHAR_FLOOR) == CHAR_getInt(g->index, CHAR_FLOOR) &&
            abs(CHAR_getInt(index, CHAR_X) - CHAR_getInt(g->index, CHAR_X)) <= 2 &&
            abs(CHAR_getInt(index, CHAR_Y) - CHAR_getInt(g->index, CHAR_Y)) <= 2) {
            CHAR_JoinParty_Main(index, g->index);
        }
    }
    stone_app_role_roster(fd);
    fprintf(stderr, "[local] companion operation=%c slot=%d\n", operation, only);
    return 1;
}

void stone_app_tick(void)
{
    static time_t last;
    time_t now = time(NULL);
    if (now == last) return;
    last = now;
    AppGroup **link = &groups;
    while (*link) {
        AppGroup *g = *link;
        if (!g->closing && g->switching && now > g->handoff_until) {
            g->switching = 0; g->handoff_until = 0;
            if (primary_valid(g)) stone_app_role_roster(g->fd);
        }
        if (g->closing && g->handoff_until && now > g->handoff_until) {
            g->handoff_until = 0;
            for (int s = 0; s < 5; ++s) {
                int index = member_index(&g->member[s]);
                if (index >= 0 && CHAR_getWorkInt(index, CHAR_WORK_OFFLINE) == 2)
                    close_member(&g->member[s]);
            }
        }
        if (!g->closing && !primary_valid(g)) {
            detach_group(g);
        }
        int active = 0, changed = 0;
        for (int s = 0; s < 5; ++s) {
            if (s + 1 == g->primary_slot) continue;
            AppMember *m = &g->member[s];
            int index = member_index(m), shown = index;
            if (shown < 0) {
                AppMember *detached = detached_offline(g, m);
                if (detached) shown = member_index(detached);
            }
            int state = shown < 0 ? 0 : role_state(g, shown);
            if (state != m->last_state) { m->last_state = state; changed = 1; }
            if (m->fd < 0) continue;
            ++active;
            if (index >= 0 && now - m->saved >= 60) {
                CHAR_charSaveFromConnect(index, FALSE);
                m->saved = now;
            }
            if (m->phase == ROLE_LIST && now - m->since >= 30) release_member(m);
            /* Pending load/save contexts are intentionally retained: their
             * fdid must remain resolvable for late SAAC replies and unlocks. */
        }
        if (changed && primary_valid(g)) stone_app_role_roster(g->fd);
        if (g->closing && !active) { *link = g->next; memset(g, 0, sizeof(*g)); free(g); }
        else link = &g->next;
    }
}
