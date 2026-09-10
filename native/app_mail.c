/* Target Lua mailbox 1026 (202/67, 66/808/831). Character receipt and inventory are serialized
 * together by CHAR_charSaveFromConnect; SQL is reconciled only after that
 * snapshot is acknowledged, or after a receipt is loaded from the character.
 * All hooks run on the native game/SAAC event loop, not concurrently. */
#include "version.h"
#include "char.h"
#include "char_base.h"
#include "net.h"
#include "item.h"
#include "battle.h"
#include "autil.h"
#include "lssproto_serv.h"
#include "longzoro/sasql.h"
#include "wire_charset.h"
#include "app_mail.h"
#include <mysql/mysql.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern MYSQL mysql;

typedef struct {
    int id, item, claimed, created, expires;
    char title[384], body[6144];
} MailRow;
typedef struct MailSession {
    struct MailSession *next;
    int fd, epoch, index, initialized, blocked, pending, persisted, confirmed;
    uint64_t sent, acknowledged, required;
    char account[64], name[192];
    int shown[100], shown_item[100], shown_count;
    int sequence, popup, popup_sequence, red_sent, red_state;
} MailSession;
static MailSession *mail_sessions;
static int mail_schema_ready;
static unsigned mail_window_serial;
extern int stone_storage_busy(int fd);
extern int stone_attributes_busy(int fd);
extern int stone_market_busy(int fd);
int stone_mail_busy(int fd)
{
    for (MailSession *s = mail_sessions; s; s = s->next)
        if (s->fd == fd && CONNECT_checkfd(fd) && CONNECT_getFdid(fd) == s->epoch)
            return s->blocked || s->pending || s->sent != s->acknowledged;
    return 0;
}
#define MAIL_WINDOW 1026
#define MAIL_SEQUENCE_BASE 0x4d000000
#define MAIL_SEQUENCE_MASK 0x00ffffff
#define MAIL_PAYLOAD_SIZE 12000

static int mail_character(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 ||
        !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd)) return -1;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
        CHAR_getWorkInt(index, CHAR_WORKFD) != fd) return -1;
    char account[64], name[192];
    CONNECT_getCdkey(fd, account, sizeof(account));
    CONNECT_getCharname(fd, name, sizeof(name));
    if (!*account || !*name || strcmp(account, CHAR_getChar(index, CHAR_CDKEY)) ||
        strcmp(name, CHAR_getChar(index, CHAR_NAME))) return -1;
    return index;
}

static MailSession *mail_session(int fd)
{
    MailSession **link = &mail_sessions;
    while (*link) {
        MailSession *s = *link;
        if (!CONNECT_checkfd(s->fd) || CONNECT_getFdid(s->fd) != s->epoch) {
            *link = s->next; free(s); continue;
        }
        if (s->fd == fd) return s;
        link = &s->next;
    }
    if (!CONNECT_checkfd(fd)) return NULL;
    MailSession *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->fd = fd; s->epoch = CONNECT_getFdid(fd); s->index = -1;
    s->next = mail_sessions; mail_sessions = s;
    return s;
}

static int mail_same_character(MailSession *s)
{
    int index = mail_character(s->fd);
    return index >= 0 && s->initialized && index == s->index &&
        !strcmp(s->account, CHAR_getChar(index, CHAR_CDKEY)) &&
        !strcmp(s->name, CHAR_getChar(index, CHAR_NAME));
}

static void mail_notice(MailSession *s, const char *message)
{
    if (mail_same_character(s))
        CHAR_talkToCli(s->index, -1, (char *)message, CHAR_COLORYELLOW);
}

static int mail_integer(const char *text, int positive, int *out)
{
    if (!text || !*text) return 0;
    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (INT_MAX - (*p - '0')) / 10) return 0;
        value = value * 10 + *p - '0';
    }
    if (positive && !value) return 0;
    *out = value; return 1;
}

/* 0 is empty, -1 is reserved/non-mail data, >0 is a canonical mail receipt. */
static int mail_receipt(const char *text)
{
    int id; char canonical[32];
    if (!text || !*text) return 0;
    if (strncmp(text, "mail:", 5) || !mail_integer(text + 5, 1, &id)) return -1;
    snprintf(canonical, sizeof(canonical), "mail:%d", id);
    return strcmp(canonical, text) ? -1 : id;
}

static void mail_hex(const char *input, char *output)
{
    const char digits[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        *output++ = digits[*p >> 4]; *output++ = digits[*p & 15];
    }
    *output = 0;
}

static void mail_owner_sql(MailSession *s, char *output, size_t size)
{
    char account[sizeof(s->account) * 2], name[sizeof(s->name) * 2];
    mail_hex(s->account, account); mail_hex(s->name, name);
    snprintf(output, size, "cdkey=X'%s' AND charname=X'%s'", account, name);
}

static int mail_database(void)
{
    if (mail_schema_ready) return 1;
    /* Binary identities preserve exact native bytes, case and trailing spaces;
     * hex literals below do not depend on SQL quoting modes or connection keys.
     * Every query is independently idempotent, including sasql's reconnect retry.
     */
    char schema[] = "CREATE TABLE IF NOT EXISTS local_system_mail ("
        "id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "cdkey VARBINARY(64) NOT NULL,charname VARBINARY(192) NOT NULL,"
        "title VARCHAR(127) CHARACTER SET utf8mb4 NOT NULL,"
        "body TEXT CHARACTER SET utf8mb4 NOT NULL,item_template INT NULL,"
        "claimed TINYINT NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "expires_at BIGINT UNSIGNED NULL,"
        "KEY recipient(cdkey,charname,claimed,id)) ENGINE=InnoDB";
    if (sasql_mysql_query(schema)) return 0;
    /* Existing records receive their real import/migration timestamp; no
     * historical send date is fabricated. MariaDB's IF NOT EXISTS makes a
     * reconnect or interrupted migration safe to repeat. NULL means no expiry. */
    char migration[] = "ALTER TABLE local_system_mail "
        "ADD COLUMN IF NOT EXISTS created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "ADD COLUMN IF NOT EXISTS expires_at BIGINT UNSIGNED NULL";
    if (sasql_mysql_query(migration)) return 0;
    mail_schema_ready = 1; return 1;
}

static int mail_copy_column(char *out, size_t capacity, const char *column, unsigned long length)
{
    if (!column || length >= capacity || strlen(column) != length) return 0;
    memcpy(out, column, length); out[length] = 0; return 1;
}

static int mail_read_row(MYSQL_RES *result, MYSQL_ROW columns, MailRow *row)
{
    unsigned long *lengths = mysql_fetch_lengths(result);
    if (!lengths || !mail_integer(columns[0], 1, &row->id) ||
        !mail_integer(columns[4], 0, &row->claimed) || row->claimed > 1 ||
        !mail_copy_column(row->title, sizeof(row->title), columns[1], lengths[1]) ||
        !mail_copy_column(row->body, sizeof(row->body), columns[2], lengths[2]) ||
        !mail_integer(columns[5], 1, &row->created)) return 0;
    row->expires = INT_MAX; /* Display sentinel for existing non-expiring mail. */
    if (columns[6] && !mail_integer(columns[6], 1, &row->expires)) return 0;
    row->item = -1;
    return !columns[3] || mail_integer(columns[3], 0, &row->item);
}

/* Returns 1 for an owned record, 0 for absent/foreign id, -1 for an error. */
static int mail_find(MailSession *s, int id, MailRow *row)
{
    char owner[600], query[900];
    mail_owner_sql(s, owner, sizeof(owner));
    snprintf(query, sizeof(query), "SELECT id,title,body,item_template,claimed,UNIX_TIMESTAMP(created_at),expires_at FROM local_system_mail "
             "WHERE id=%d AND %s", id, owner);
    if (sasql_mysql_query(query)) return -1;
    MYSQL_RES *result = mysql_store_result(&mysql);
    if (!result) return -1;
    int status = -1;
    if (mysql_num_fields(result) == 7) {
        MYSQL_ROW columns = mysql_fetch_row(result);
        status = !columns ? 0 : mail_read_row(result, columns, row) ? 1 : -1;
    }
    mysql_free_result(result); return status;
}

static void mail_list(MailSession *s, int mode);

static void mail_red(MailSession *s, int flag)
{
    if (!mail_same_character(s) || (s->red_sent && s->red_state == flag)) return;
    char buffer[128] = "";
    CONNECT_getCdkey(s->fd, PersonalKey, 4096);
    strcat(PersonalKey, getRandDefaultKey(s->fd));
    int checksum = util_mkint(s->fd, buffer, flag);
    util_mkint(s->fd, buffer, checksum);
    util_SendMesg(s->fd, 831, buffer);
    s->red_sent = 1; s->red_state = flag;
}

static void mail_completed_display(MailSession *s)
{
    if (!mail_same_character(s)) { s->popup = 0; return; }
    if (s->sequence && s->popup && s->popup_sequence == s->sequence) {
        /* Updating retains Lua's recieveIndex and emailInfo. A new 66/L here
         * would erase them before the attachment confirmation can be drawn. */
        lssproto_WNUpdate_send(s->fd, MAIL_WINDOW, 0, s->sequence, -1, "S");
        mail_list(s, 0);
    } else {
        mail_list(s, s->sequence ? 2 : 0);
    }
    s->popup = 0;
}

static int mail_finish(MailSession *s)
{
    if (!s->pending) return 1;
    if (!s->persisted || s->blocked) return 0;
    MailRow row;
    if (mail_find(s, s->pending, &row) != 1) {
        mail_notice(s, "邮件领取状态暂时无法确认，请稍后重试。"); return 0;
    }
    if (!row.claimed) {
        char owner[600], query[800];
        mail_owner_sql(s, owner, sizeof(owner));
        snprintf(query, sizeof(query), "UPDATE local_system_mail SET claimed=1 WHERE id=%d AND %s", s->pending, owner);
        if (sasql_mysql_query(query) || mail_find(s, s->pending, &row) != 1 || !row.claimed) {
            mail_notice(s, "邮件领取记录保存失败，请稍后重试，暂时不能领取下一封。"); return 0;
        }
    }
    s->confirmed = s->pending; s->pending = 0; s->persisted = 0; s->required = 0;
    /* Keep the character receipt. It is overwritten only by the next claim,
     * after this SQL acknowledgement, and never cleared ahead of persistence. */
    mail_completed_display(s); return 1;
}

static int mail_wire_fits(const char *text, size_t maximum)
{
    char converted[8192];
    int size = stone_convert_text(text, converted, sizeof(converted), "UTF-8", "GBK");
    return size >= 0 && (size_t)size <= maximum;
}

static int mail_attachment(int template, char *out, size_t capacity)
{
    snprintf(out, capacity, "|0|");
    if (template < 0) return 1;
    if (!ITEM_CHECKITEMTABLE(template)) return 0;
    const char *name = ITEMTBL_getChar(template, ITEM_SECRETNAME);
    if (!name || !*name) name = ITEMTBL_getChar(template, ITEM_NAME);
    const char *memo = ITEMTBL_getChar(template, ITEM_EFFECTSTRING);
    int picture = ITEMTBL_getInt(template, ITEM_BASEIMAGENUMBER);
    if (!name || !memo || picture < 0 || strchr(name, '|') || strchr(memo, '|') ||
        !mail_wire_fits(name, 127) || !mail_wire_fits(memo, 2047)) return 0;
    int count = snprintf(out, capacity, "%s|%d|%s", name, picture, memo);
    return count >= 0 && (size_t)count < capacity;
}

/* mode: 0 updates only the red dot; 1 opens; 2 updates an existing Lua window. */
static void mail_list(MailSession *s, int mode)
{
    if (!mail_same_character(s)) return;
    char owner[600], query[900];
    mail_owner_sql(s, owner, sizeof(owner));
    snprintf(query, sizeof(query), "SELECT id,title,body,item_template,claimed,UNIX_TIMESTAMP(created_at),expires_at FROM local_system_mail "
             "WHERE %s AND claimed=0 ORDER BY id LIMIT 100", owner);
    if (sasql_mysql_query(query)) { mail_notice(s, "邮件列表读取失败，请稍后重试。"); return; }
    MYSQL_RES *result = mysql_store_result(&mysql);
    if (!result) { mail_notice(s, "邮件列表读取失败，请稍后重试。"); return; }
    if (mysql_num_fields(result) != 7) { mysql_free_result(result); return; }
    char entries[MAIL_PAYLOAD_SIZE] = "", payload[MAIL_PAYLOAD_SIZE];
    int shown[100], shown_item[100], count = 0, invalid = 0, has_mail = 0, limited = 0;
    size_t used = 0;
    MYSQL_ROW columns;
    while ((columns = mysql_fetch_row(result))) {
        has_mail = 1;
        if (!mode) continue;
        MailRow row; char attachment[4096], entry[MAIL_PAYLOAD_SIZE];
        if (!mail_read_row(result, columns, &row) || row.claimed ||
            strchr(row.title, '|') || strchr(row.body, '|') ||
            !mail_wire_fits(row.title, 127) || !mail_wire_fits(row.body, 2047) ||
            !mail_attachment(row.item, attachment, sizeof(attachment))) { invalid = 1; continue; }
        int size = snprintf(entry, sizeof(entry), "|1|%d|%s|%s|%s|%d|%d", row.id,
                            attachment, row.title, row.body, row.created, row.expires);
        if (size < 0 || (size_t)size >= sizeof(entry) || used + size + 16 >= sizeof(entries) || count == 100) {
            limited = 1; continue;
        }
        memcpy(entries + used, entry, size + 1); used += size;
        shown_item[count] = row.item; shown[count++] = row.id;
    }
    mysql_free_result(result);
    mail_red(s, has_mail);
    if (!mode) return;
    int prefix = snprintf(payload, sizeof(payload), "L|%d", count);
    if (prefix < 0 || (size_t)prefix + used >= sizeof(payload)) return;
    memcpy(payload + prefix, entries, used + 1);
    memcpy(s->shown, shown, count * sizeof(shown[0])); s->shown_count = count;
    memcpy(s->shown_item, shown_item, count * sizeof(shown_item[0]));
    s->popup = 0;
    if (mode == 1) lssproto_WN_send(s->fd, MAIL_WINDOW, 0, s->sequence, -1, payload);
    else lssproto_WNUpdate_send(s->fd, MAIL_WINDOW, 0, s->sequence, -1, payload);
    if (invalid) mail_notice(s, "部分邮件内容或附件数据无效，请联系管理员。");
    if (limited) mail_notice(s, "本页邮件内容较多，处理后刷新可继续查看其余邮件。");
}

void stone_mail_login(int fd)
{
    int index = mail_character(fd);
    if (index < 0) return;
    MailSession *s = mail_session(fd);
    if (!s) return;
    if (!mail_same_character(s)) {
        s->index = index; s->initialized = 1; s->blocked = 0;
        s->pending = s->persisted = s->confirmed = 0; s->required = 0; s->shown_count = 0;
        s->sequence = s->popup = s->popup_sequence = s->red_sent = s->red_state = 0;
        snprintf(s->account, sizeof(s->account), "%s", CHAR_getChar(index, CHAR_CDKEY));
        snprintf(s->name, sizeof(s->name), "%s", CHAR_getChar(index, CHAR_NAME));
        int id = mail_receipt(CHAR_getChar(index, CHAR_ACTIVE_ACT16));
        if (id < 0) s->blocked = 1;
        else if (id) { s->pending = id; s->persisted = 1; }
    }
    /* A repeated login hook on a live character must not reinterpret an
     * unacknowledged in-memory receipt as a receipt loaded from disk. */
    if (s->blocked) { mail_notice(s, "邮件领取状态异常，请联系管理员。"); return; }
    if (!mail_database()) { mail_notice(s, "邮件数据库暂时不可用，请稍后重试。"); return; }
    if (mail_finish(s)) mail_list(s, 0);
}

static void mail_claim(MailSession *s, int id, int action)
{
    if (id <= 0 || !mail_same_character(s)) return;
    if (CHAR_getWorkInt(s->index, CHAR_WORKTRADEMODE) != CHAR_TRADE_FREE ||
        CHAR_getWorkInt(s->index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE) {
        mail_notice(s, "请在交易和战斗结束后处理邮件。"); return;
    }
    if (s->blocked) { mail_notice(s, "邮件领取状态异常，请联系管理员。"); return; }
    if (!mail_database()) { mail_notice(s, "邮件数据库暂时不可用，请稍后重试。"); return; }
    if (s->pending) {
        if (s->pending != id || action != 'R') s->popup = 0;
        if (!mail_finish(s) && !s->persisted)
            mail_notice(s, "上一封邮件正在等待角色保存完成，请稍候。");
        return;
    }
    if (stone_storage_busy(s->fd) || stone_attributes_busy(s->fd) || stone_market_busy(s->fd)) {
        mail_notice(s, "仓库、配点或市场操作正在保存，请稍后领取邮件。"); return;
    }
    MailRow row; int found = mail_find(s, id, &row);
    if (found != 1) { mail_notice(s, found < 0 ? "邮件读取失败，请稍后重试。" : "没有可领取的这封邮件。"); return; }
    if (row.claimed) { mail_list(s, s->sequence ? 2 : 0); return; }
    for (int n = 0; n < s->shown_count; ++n) {
        if (s->shown[n] == id && s->shown_item[n] != row.item) {
            mail_notice(s, "邮件附件已更新，请确认后重新领取。");
            mail_list(s, 2); return;
        }
    }
    if (action == 'D' && row.item >= 0) {
        mail_notice(s, "这封邮件还有未领取的附件，请先领取再删除。");
        mail_list(s, 2); return;
    }
    int prior = mail_receipt(CHAR_getChar(s->index, CHAR_ACTIVE_ACT16));
    if (prior < 0 || (prior && prior != s->confirmed) || s->sent == UINT64_MAX) {
        s->blocked = 1; mail_notice(s, "邮件领取状态异常，请联系管理员。"); return;
    }
    int slot = -1, item = -1;
    if (row.item >= 0) {
        if (!ITEM_CHECKITEMTABLE(row.item)) { mail_notice(s, "邮件附件物品不存在，请联系管理员。"); return; }
        slot = CHAR_findEmptyItemBox(s->index);
        if (slot < 0) { mail_notice(s, "背包已满，请留出一个空位后再领取邮件。"); return; }
        item = ITEM_makeItemAndRegist(row.item);
        if (item < 0) { mail_notice(s, "邮件附件创建失败，请稍后重试。"); return; }
        CHAR_setItemIndex(s->index, slot, item);
        if (CHAR_getItemIndex(s->index, slot) != item) {
            ITEM_endExistItemsOne(item); mail_notice(s, "邮件附件放入背包失败，请稍后重试。"); return;
        }
        ITEM_setWorkInt(item, ITEM_WORKOBJINDEX, -1);
        ITEM_setWorkInt(item, ITEM_WORKCHARAINDEX, s->index);
    }
    char token[32]; snprintf(token, sizeof(token), "mail:%d", id);
    CHAR_setChar(s->index, CHAR_ACTIVE_ACT16, token);
    if (strcmp(CHAR_getChar(s->index, CHAR_ACTIVE_ACT16), token)) {
        if (item >= 0) {
            CHAR_setItemIndex(s->index, slot, -1);
            if (CHAR_getItemIndex(s->index, slot) == -1) ITEM_endExistItemsOne(item);
            else s->blocked = 1;
        }
        mail_notice(s, "邮件领取未完成，请联系管理员。"); return;
    }
    s->pending = id; s->persisted = 0; s->required = s->sent + 1;
    s->popup = action == 'R' && row.item >= 0;
    s->popup_sequence = s->sequence;
    if (item >= 0) CHAR_sendItemDataOne(s->index, slot);
    if (!CHAR_charSaveFromConnect(s->index, FALSE))
        mail_notice(s, "邮件领取尚未保存，请稍候，暂时不能领取下一封。");
}

int stone_mail_menu(int fd, int menu)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 || menu != 118) return 0;
    if (mail_character(fd) < 0) return 1;
    stone_mail_login(fd);
    MailSession *s = mail_session(fd);
    if (!s || !mail_same_character(s) || s->blocked) return 1;
    if (CHAR_getWorkInt(s->index, CHAR_WORKTRADEMODE) != CHAR_TRADE_FREE ||
        CHAR_getWorkInt(s->index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE) return 1;
    if (!mail_database()) return 1;
    /* Bounded positive seqno is unique for this process; no client socket or
     * recipient string is trusted to select a server character. */
    if (mail_window_serial == MAIL_SEQUENCE_MASK) {
        mail_notice(s, "邮件窗口暂时无法打开，请稍后重试。"); return 1;
    }
    s->sequence = MAIL_SEQUENCE_BASE | ++mail_window_serial;
    s->popup = 0;
    mail_list(s, 1);
    return 1;
}

static int mail_shown(MailSession *s, int id)
{
    for (int n = 0; n < s->shown_count; ++n) if (s->shown[n] == id) return 1;
    return 0;
}

int stone_mail_window(int fd, int sequence, int object, int select, const char *data)
{
    if (((unsigned)sequence & ~MAIL_SEQUENCE_MASK) != MAIL_SEQUENCE_BASE) return 0;
    if (mail_character(fd) < 0) return 1;
    MailSession *s = mail_session(fd);
    if (!s || !mail_same_character(s) || !s->sequence || sequence != s->sequence ||
        object != -1 || select != 0 || !data || s->blocked) return 1;
    if (!strcmp(data, "T|1")) {
        s->popup = 0;
        if (mail_database()) mail_list(s, 2);
        return 1;
    }
    if ((data[0] != 'R' && data[0] != 'D') || data[1] != '|') return 1;
    const char *end = strchr(data + 2, '|');
    if (!end || strcmp(end, "|1") || end - (data + 2) < 1 || end - (data + 2) > 10) return 1;
    char text[12], canonical[12]; int id;
    memcpy(text, data + 2, end - (data + 2)); text[end - (data + 2)] = 0;
    if (!mail_integer(text, 1, &id)) return 1;
    snprintf(canonical, sizeof(canonical), "%d", id);
    if (strcmp(text, canonical) || !mail_shown(s, id)) return 1;
    mail_claim(s, id, data[0]);
    return 1;
}

/* The legacy callback may still reach this compatibility entrypoint, but it
 * cannot claim anything that was not listed in the authenticated 1026 view. */
void stone_mail_request(int fd, int id)
{
    if (id <= 0 || mail_character(fd) < 0) return;
    MailSession *s = mail_session(fd);
    if (s && mail_same_character(s) && s->sequence && mail_shown(s, id)) mail_claim(s, id, 0);
}

void stone_mail_saving(int fd)
{
    MailSession *s = mail_session(fd);
    if (!s) return;
    if (s->sent == UINT64_MAX) { s->blocked = 1; return; }
    ++s->sent;
}

void stone_mail_saved(int fd, const char *result)
{
    MailSession *s = mail_session(fd);
    if (!s || s->acknowledged == s->sent) return;
    uint64_t sequence = ++s->acknowledged;
    if (!s->pending || s->blocked || sequence < s->required) return;
    /* The native SAAC handler writes each snapshot and replies synchronously
     * on its ordered connection. Counting ALL sends/replies excludes an older
     * autosave ACK. A missing reply holds the queue conservatively; no timeout
     * is treated as success. A later full snapshot may recover a failed save.
     */
    if (result && !strcmp(result, SUCCESSFUL)) {
        s->persisted = 1; mail_finish(s);
    } else if (!s->persisted) {
        mail_notice(s, "邮件领取保存失败，请稍候，暂时不能领取下一封。");
    }
}
