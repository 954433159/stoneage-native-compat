/* Target 20260805: SaMenu 122 -> window 1119, B/S/C|1..5|.
 * CAA14 contains only this character's snapshots and durable random identity.
 * VipPoint and a purchase entitlement advance in ONE InnoDB multi-table UPDATE.
 * The entitlement is also the recovery journal when a character save is lost.
 * All entrypoints run on the native GMSV/SAAC event loop. */
#include "version.h"
#include "char.h"
#include "char_base.h"
#include "net.h"
#include "battle.h"
#include "profession_skill.h"
#include "lssproto_serv.h"
#include "longzoro/sasql.h"
#include "mylua/function.h"
#include "app_attributes.h"
#include <mysql/mysql.h>
#include <sys/random.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern MYSQL mysql;
extern int stone_mail_busy(int fd);
extern int stone_storage_busy(int fd);
extern int stone_market_busy(int fd);
#define ATTR_SEQUENCE_BASE 0x41000000
#define ATTR_SEQUENCE_MASK 0x00ffffff
#define ATTR_STATE_SIZE 128
#define ATTR_PAYLOAD_SIZE 1024
#define ATTR_TABLE "local_point_plan_ownership"
static const int attr_prices[5] = {4000, 4000, 5000, 8000, 10000};
static const CHAR_DATAINT attr_fields[8] = {CHAR_VITAL, CHAR_STR, CHAR_TOUGH, CHAR_DEX,
    CHAR_EARTHAT, CHAR_WATERAT, CHAR_FIREAT, CHAR_WINDAT};
static const char attr_digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
typedef struct { int value[9]; } AttrPlan;
typedef struct { char nonce[17]; int count; AttrPlan plan[5]; } AttrState;
typedef struct AttrSession {
    struct AttrSession *next;
    int fd, epoch, index, initialized, blocked, pending, uncertain, sequence;
    int created, slot;
    uint64_t sent, acknowledged, required;
    time_t opened;
    char account[64], name[192];
    AttrState state;
} AttrSession;
static AttrSession *attr_sessions;
static unsigned attr_serial;
static int attr_other_busy(int fd)
{
    return stone_mail_busy(fd) || stone_storage_busy(fd) || stone_market_busy(fd);
}

/* Exactly 124 data bits per snapshot: 4*18 attributes, 4*7 elements, 24 budget.
 * 21 URL-safe base64 chars (last two bits zero). 20+5*21=125 < STRING128.
 * The prefix includes a 96-bit random identity, durably saved BEFORE charging. */
static int attr_digit(char c)
{
    const char *p = c ? strchr(attr_digits, c) : NULL;
    return p ? (int)(p - attr_digits) : -1;
}
static void attr_pack(const int *values, const int *widths, int count, char *out, int chars)
{
    memset(out, 0, chars);
    int pos = 0;
    for (int n = 0; n < count; ++n) for (int b = widths[n] - 1; b >= 0; --b, ++pos)
        out[pos / 6] |= ((values[n] >> b) & 1) << (5 - pos % 6);
    for (int n = 0; n < chars; ++n) out[n] = attr_digits[(unsigned char)out[n]];
    out[chars] = 0;
}
static int attr_unpack(const char *in, int *values, const int *widths, int count, int chars)
{
    for (int n = 0; n < chars; ++n) if (attr_digit(in[n]) < 0) return 0;
    int pos = 0;
    for (int n = 0; n < count; ++n) {
        values[n] = 0;
        for (int b = 0; b < widths[n]; ++b, ++pos)
            values[n] = (values[n] << 1) | ((attr_digit(in[pos / 6]) >> (5 - pos % 6)) & 1);
    }
    while (pos < chars * 6) {
        if ((attr_digit(in[pos / 6]) >> (5 - pos % 6)) & 1) return 0;
        ++pos;
    }
    return 1;
}
static int attr_plan_valid(const AttrPlan *p, int empty)
{
    int64_t allocated = 0; int elements = 0;
    for (int n = 0; n < 4; ++n) {
        if (p->value[n] < 0 || p->value[n] > 262143) return 0;
        allocated += p->value[n];
    }
    for (int n = 4; n < 8; ++n) {
        if (p->value[n] < 0 || p->value[n] > 100) return 0;
        elements += p->value[n];
    }
    if (!p->value[8]) return empty && !allocated && !elements;
    return p->value[8] > 0 && p->value[8] <= 16777215 && elements == 100 &&
        allocated <= p->value[8] && (p->value[8] - allocated) % 100 == 0;
}
static const int attr_widths[9] = {18,18,18,18,7,7,7,7,24};
static int attr_encode(const AttrState *state, char out[ATTR_STATE_SIZE])
{
    if (state->count < 0 || state->count > 5 || strlen(state->nonce) != 16) return 0;
    for (int n = 0; n < 16; ++n) if (attr_digit(state->nonce[n]) < 0) return 0;
    snprintf(out, ATTR_STATE_SIZE, "pp1%d%s", state->count, state->nonce);
    for (int n = 0; n < state->count; ++n) {
        if (!attr_plan_valid(&state->plan[n], 1)) return 0;
        attr_pack(state->plan[n].value, attr_widths, 9, out + 20 + n * 21, 21);
    }
    return strlen(out) == (size_t)(20 + state->count * 21);
}
static int attr_decode(const char *text, AttrState *state)
{
    memset(state, 0, sizeof(*state));
    if (!text || strlen(text) < 20 || strncmp(text, "pp1", 3) || text[3] < '0' || text[3] > '5') return 0;
    state->count = text[3] - '0';
    if (strlen(text) != (size_t)(20 + state->count * 21)) return 0;
    memcpy(state->nonce, text + 4, 16);
    for (int n = 0; n < 16; ++n) if (attr_digit(state->nonce[n]) < 0) return 0;
    for (int n = 0; n < state->count; ++n)
        if (!attr_unpack(text + 20 + n * 21, state->plan[n].value, attr_widths, 9, 21) ||
            !attr_plan_valid(&state->plan[n], 1)) return 0;
    return 1;
}
static int attr_random(char nonce[17])
{
    unsigned char raw[12]; int values[12], widths[12];
    if (getrandom(raw, sizeof(raw), 0) != sizeof(raw)) return 0;
    for (int n = 0; n < 12; ++n) { values[n] = raw[n]; widths[n] = 8; }
    attr_pack(values, widths, 12, nonce, 16); return 1;
}

static int attr_character(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 || !CONNECT_isCLI(fd) ||
        !CONNECT_isLOGIN(fd)) return -1;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index, CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
        CHAR_getWorkInt(index, CHAR_WORKFD) != fd) return -1;
    char account[64], name[192];
    CONNECT_getCdkey(fd, account, sizeof(account)); CONNECT_getCharname(fd, name, sizeof(name));
    if (!*account || !*name || strcmp(account, CHAR_getChar(index, CHAR_CDKEY)) ||
        strcmp(name, CHAR_getChar(index, CHAR_NAME))) return -1;
    return index;
}
static AttrSession *attr_session(int fd)
{
    AttrSession **link = &attr_sessions;
    while (*link) {
        AttrSession *s = *link;
        if (!CONNECT_checkfd(s->fd) || CONNECT_getFdid(s->fd) != s->epoch) {
            *link = s->next; free(s); continue;
        }
        if (s->fd == fd) return s;
        link = &s->next;
    }
    if (!CONNECT_checkfd(fd)) return NULL;
    AttrSession *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    s->fd = fd; s->epoch = CONNECT_getFdid(fd); s->index = -1;
    s->next = attr_sessions; attr_sessions = s; return s;
}
static int attr_same(AttrSession *s)
{
    int index = attr_character(s->fd);
    return index >= 0 && s->initialized && index == s->index &&
        s->created == CHAR_getInt(index, CHAR_CREATETIME) && s->slot == CHAR_getInt(index, CHAR_SAVEINDEXNUMBER) &&
        !strcmp(s->account, CHAR_getChar(index, CHAR_CDKEY)) && !strcmp(s->name, CHAR_getChar(index, CHAR_NAME));
}
static void attr_notice(AttrSession *s, const char *text)
{
    if (attr_same(s)) CHAR_talkToCli(s->index, -1, (char *)text, CHAR_COLORYELLOW);
}
static int attr_ready(AttrSession *s)
{
    if (!attr_same(s) || s->blocked) return 0;
    if (CHAR_getWorkInt(s->index, CHAR_WORKTRADEMODE) != CHAR_TRADE_FREE ||
        CHAR_getWorkInt(s->index, CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE ||
        CHAR_getFlg(s->index, CHAR_ISDIE) || CHAR_getInt(s->index, CHAR_HP) <= 0) {
        attr_notice(s, "请在存活、非战斗、非交易状态操作配点方案。"); return 0;
    }
    if (s->pending) { attr_notice(s, "配点方案正在等待角色保存，请稍后再操作。"); return 0; }
    if (attr_other_busy(s->fd)) { attr_notice(s, "邮件、仓库或市场操作正在保存，请稍后再操作配点方案。"); return 0; }
    return 1;
}
static int attr_store(AttrSession *s)
{
    char text[ATTR_STATE_SIZE];
    if (!attr_encode(&s->state, text) || !CHAR_setChar(s->index, CHAR_ACTIVE_ACT14, text) ||
        strcmp(CHAR_getChar(s->index, CHAR_ACTIVE_ACT14), text)) {
        s->blocked = 1; attr_notice(s, "配点方案字段无法完整保存，请联系管理员。"); return 0;
    }
    return 1;
}
static void attr_save(AttrSession *s)
{
    if (attr_other_busy(s->fd)) {
        attr_notice(s, "其他资产操作正在保存，配点方案将在其完成后重试。"); return;
    }
    if (s->sent == UINT64_MAX) { s->blocked = 1; return; }
    s->pending = 1; s->required = s->sent + 1;
    if (!CHAR_charSaveFromConnect(s->index, FALSE))
        attr_notice(s, "配点方案尚未保存，请稍后重试。");
}

static void attr_hex(const char *in, char *out)
{
    static const char digits[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        *out++ = digits[*p >> 4]; *out++ = digits[*p & 15];
    }
    *out = 0;
}
static int attr_identifier(const char *value)
{
    if (!value || !*value || strlen(value) > 63) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    return 1;
}
static int attr_scalar(char *query, char *out, size_t capacity)
{
    if (sasql_mysql_query(query)) return 0;
    MYSQL_RES *result = mysql_store_result(&mysql); if (!result) return 0;
    int okay = 0;
    if (mysql_num_rows(result) == 1 && mysql_num_fields(result) == 1) {
        MYSQL_ROW row = mysql_fetch_row(result); unsigned long *length = mysql_fetch_lengths(result);
        if (row && row[0] && length && length[0] < capacity && strlen(row[0]) == length[0]) {
            memcpy(out, row[0], length[0] + 1); okay = 1;
        }
    }
    mysql_free_result(result); return okay;
}
static int attr_database(void)
{
    const char *table = stone_attributes_account_table(), *key = stone_attributes_account_key();
    if (!attr_identifier(table) || !attr_identifier(key)) return 0;
    char create[] = "CREATE TABLE IF NOT EXISTS " ATTR_TABLE " ("
        "cdkey VARBINARY(64) NOT NULL,nonce VARBINARY(16) NOT NULL,"
        "role_name VARBINARY(192) NOT NULL,created INT NOT NULL,slot INT NOT NULL,"
        "owned INT NOT NULL DEFAULT 0,PRIMARY KEY(cdkey,nonce)) ENGINE=InnoDB";
    if (sasql_mysql_query(create)) return 0;
    const char *names[2] = {table, ATTR_TABLE};
    for (int n = 0; n < 2; ++n) {
        char hex[129], query[512], engine[32]; attr_hex(names[n], hex);
        snprintf(query, sizeof(query), "SELECT ENGINE FROM information_schema.TABLES WHERE "
            "TABLE_SCHEMA=DATABASE() AND TABLE_NAME=X'%s'", hex);
        if (!attr_scalar(query, engine, sizeof(engine)) || strcmp(engine, "InnoDB")) return 0;
    }
    return 1;
}
static void attr_owner(AttrSession *s, char *out, size_t size)
{
    char account[129], nonce[33], name[385];
    attr_hex(s->account, account); attr_hex(s->state.nonce, nonce); attr_hex(s->name, name);
    snprintf(out, size, "p.cdkey=X'%s' AND p.nonce=X'%s' AND p.role_name=X'%s' AND p.created=%d AND p.slot=%d",
        account, nonce, name, s->created, s->slot);
}
static int attr_owned(AttrSession *s)
{
    char owner[768], query[1024], owned[8]; attr_owner(s, owner, sizeof(owner));
    snprintf(query, sizeof(query), "SELECT p.owned FROM " ATTR_TABLE " p WHERE %s", owner);
    if (!attr_scalar(query, owned, sizeof(owned)) || strlen(owned) != 1 || owned[0] < '0' || owned[0] > '5') return -1;
    return owned[0] - '0';
}
static int attr_reconcile(AttrSession *s)
{
    if (!attr_database()) { attr_notice(s, "配点方案数据库暂时不可用，请稍后重试。"); return 0; }
    /* Only an empty state can create an entitlement row. A saved nonempty state
     * without its paid ledger is not evidence of payment and cannot recreate it. */
    if (!s->state.count) {
        char account[129], nonce[33], name[385], query[1024];
        attr_hex(s->account, account); attr_hex(s->state.nonce, nonce); attr_hex(s->name, name);
        snprintf(query, sizeof(query), "INSERT IGNORE INTO " ATTR_TABLE
            " (cdkey,nonce,role_name,created,slot,owned) VALUES (X'%s',X'%s',X'%s',%d,%d,0)",
            account, nonce, name, s->created, s->slot);
        if (sasql_mysql_query(query)) return 0;
    }
    int owned = attr_owned(s);
    if (owned < s->state.count) {
        attr_notice(s, "配点方案购买凭证不一致，请联系管理员。"); return 0;
    }
    if (owned > s->state.count) {
        if (attr_other_busy(s->fd)) {
            s->uncertain = 1;
            attr_notice(s, "已确认购买凭证，其他资产操作保存后可恢复方案。"); return 0;
        }
        for (int n = s->state.count; n < owned; ++n) memset(&s->state.plan[n], 0, sizeof(AttrPlan));
        s->state.count = owned;
        if (!attr_store(s)) return 0;
        attr_save(s);
        FreeDispatchMessage(s->fd, 261); /* existing GetGoldSend: seven target fields */
    }
    s->uncertain = 0;
    return 1;
}
static void attr_view(AttrSession *s, int open)
{
    if (!attr_same(s) || !s->sequence || s->blocked) return;
    char data[ATTR_PAYLOAD_SIZE];
    int length = snprintf(data, sizeof(data), "L|4000|4000|5000|8000|10000|%d", s->state.count);
    for (int n = 0; n < s->state.count; ++n) {
        AttrPlan *p = &s->state.plan[n];
        length += snprintf(data + length, sizeof(data) - length, "|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
            n + 1, p->value[0], p->value[1], p->value[2], p->value[3], p->value[4],
            p->value[5], p->value[6], p->value[7], p->value[8]);
    }
    if (open) lssproto_WN_send(s->fd, 1119, 0, s->sequence, -1, data);
    else lssproto_WNUpdate_send(s->fd, 1119, 0, s->sequence, -1, data);
}
static int attr_current(AttrSession *s, AttrPlan *p)
{
    int64_t total = (int64_t)CHAR_getInt(s->index, CHAR_SKILLUPPOINT) * 100;
    if (total < 0) return 0;
    for (int n = 0; n < 8; ++n) {
        p->value[n] = CHAR_getInt(s->index, attr_fields[n]);
        if (n < 4) total += p->value[n];
    }
    if (total <= 0 || total > 16777215) return 0;
    p->value[8] = (int)total; return attr_plan_valid(p, 0);
}
static int attr_profession(AttrSession *s, AttrPlan *p)
{
#ifdef _CHAR_PROFESSION
    int job = CHAR_getInt(s->index, PROFESSION_CLASS);
    if (job == PROFESSION_CLASS_FIGHTER && p->value[3] > 20000) return 0;
    if ((job == PROFESSION_CLASS_WIZARD || job == PROFESSION_CLASS_HUNTER) &&
        (p->value[1] > 20000 || p->value[2] > 20000)) return 0;
    if (job == PROFESSION_CLASS_HUNTER && p->value[3] > 40000) return 0;
#endif
    return 1;
}
static void attr_buy(AttrSession *s, int id)
{
    if (id != s->state.count + 1) {
        attr_notice(s, id <= s->state.count ? "该方案已经购买。" : "请按顺序购买配点方案。"); return;
    }
    char account[129], owner[768], query[2048], count[16]; attr_hex(s->account, account);
    snprintf(query, sizeof(query), "SELECT COUNT(*) FROM `%s` WHERE BINARY `%s`=X'%s'",
        stone_attributes_account_table(), stone_attributes_account_key(), account);
    if (!attr_scalar(query, count, sizeof(count)) || strcmp(count, "1")) {
        attr_notice(s, "金币账号记录异常，未购买方案。"); return;
    }
    attr_owner(s, owner, sizeof(owner));
    snprintf(query, sizeof(query), "UPDATE `%s` a JOIN " ATTR_TABLE " p ON BINARY a.`%s`=p.cdkey "
        "SET a.VipPoint=a.VipPoint-%d,p.owned=%d WHERE %s AND p.owned=%d AND a.VipPoint>=%d",
        stone_attributes_account_table(), stone_attributes_account_key(), attr_prices[id-1], id,
        owner, id-1, attr_prices[id-1]);
    /* Do not infer failure from affected_rows: sasql may retry after a lost
     * successful response. The WHERE guard makes retries harmless; read the
     * durable ledger even when the transport returned an error. */
    sasql_mysql_query(query);
    int owned = attr_owned(s);
    if (owned < 0) { s->uncertain = 1; attr_notice(s, "购买结果暂时无法确认，请稍后重新打开方案。"); return; }
    if (owned != id) { attr_notice(s, "金币不足或购买未完成，请稍后重试。"); return; }
    s->state.count = id; memset(&s->state.plan[id-1], 0, sizeof(AttrPlan));
    if (attr_store(s)) { attr_save(s); attr_view(s, 0); }
    FreeDispatchMessage(s->fd, 261); /* P.GOLD is stone money, not SQL VipPoint */
}

int stone_attributes_menu(int fd, int menu)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 || menu != 122) return 0;
    int index = attr_character(fd); if (index < 0) return 1;
    AttrSession *s = attr_session(fd); if (!s) return 1;
    int initializing = !attr_same(s);
    if (initializing) {
        if (attr_other_busy(fd)) {
            CHAR_talkToCli(index, -1, "邮件、仓库或市场操作正在保存，请稍后打开配点方案。", CHAR_COLORYELLOW);
            return 1;
        }
        s->index = index; s->initialized = 1; s->blocked = s->pending = s->sequence = 0;
        s->uncertain = 0;
        s->opened = 0; s->created = CHAR_getInt(index, CHAR_CREATETIME);
        s->slot = CHAR_getInt(index, CHAR_SAVEINDEXNUMBER);
        snprintf(s->account, sizeof(s->account), "%s", CHAR_getChar(index, CHAR_CDKEY));
        snprintf(s->name, sizeof(s->name), "%s", CHAR_getChar(index, CHAR_NAME));
        const char *saved = CHAR_getChar(index, CHAR_ACTIVE_ACT14);
        if (!*saved) {
            memset(&s->state, 0, sizeof(s->state));
            if (!attr_random(s->state.nonce) || !attr_store(s)) s->blocked = 1;
        } else if (!attr_decode(saved, &s->state)) s->blocked = 1;
        if (s->created <= 0 || s->slot < 0) s->blocked = 1;
        if (!s->blocked) attr_save(s); /* durable nonce, including after controller switching */
    }
    if (s->blocked) { attr_notice(s, "配点方案存档格式异常，请联系管理员。"); return 1; }
    /* The target tab sends 202 repeatedly until its native window opens. */
    time_t now = time(NULL);
    if (s->sequence && now == s->opened) return 1;
    if (attr_serial == ATTR_SEQUENCE_MASK) return 1;
    s->opened = now; s->sequence = ATTR_SEQUENCE_BASE | ++attr_serial;
    if (s->pending && !initializing) attr_save(s);
    if (!s->pending) attr_reconcile(s);
    attr_view(s, 1); return 1;
}
int stone_attributes_window(int fd, int sequence, int object, int select, const char *data)
{
    if (((unsigned)sequence & ~ATTR_SEQUENCE_MASK) != ATTR_SEQUENCE_BASE) return 0;
    if (attr_character(fd) < 0) return 1;
    AttrSession *s = attr_session(fd);
    if (!s || !attr_same(s) || !s->sequence || sequence != s->sequence || object != -1 || select != 0 ||
        !data || strlen(data) != 4 || data[1] != '|' || data[3] != '|' || data[2] < '1' || data[2] > '5' ||
        (data[0] != 'B' && data[0] != 'S' && data[0] != 'C')) return 1;
    if (!attr_ready(s) || !attr_reconcile(s) || s->pending) return 1;
    int id = data[2] - '0';
    if (data[0] == 'B') { attr_buy(s, id); return 1; }
    if (id > s->state.count) { attr_notice(s, "请先购买该配点方案。"); return 1; }
    AttrPlan current;
    if (!attr_current(s, &current)) { attr_notice(s, "当前属性超出方案保存范围，未作修改。"); return 1; }
    if (data[0] == 'S') {
        s->state.plan[id-1] = current;
        if (attr_store(s)) { attr_save(s); attr_view(s, 0); }
        return 1;
    }
    AttrPlan *target = &s->state.plan[id-1];
    if (s->state.count < 2) { attr_notice(s, "需同时购买两套方案才可相互切换。"); return 1; }
    if (!target->value[8]) { attr_notice(s, "请先保存该配点方案。"); return 1; }
    int64_t allocated = 0;
    for (int n = 0; n < 4; ++n) allocated += target->value[n];
    int64_t remaining = current.value[8] - allocated;
    if (remaining < 0 || remaining % 100 || !attr_profession(s, target)) {
        attr_notice(s, "该方案与当前可用属性点或职业不兼容，请重新保存。"); return 1;
    }
    int hp = CHAR_getInt(s->index, CHAR_HP);
    for (int n = 0; n < 8; ++n) CHAR_setInt(s->index, attr_fields[n], target->value[n]);
    CHAR_setInt(s->index, CHAR_SKILLUPPOINT, (int)(remaining / 100));
    CHAR_complianceParameter(s->index);
    int maxhp = CHAR_getWorkInt(s->index, CHAR_WORKMAXHP);
    if (hp > maxhp) CHAR_setInt(s->index, CHAR_HP, maxhp); /* no healing by toggling */
    CHAR_sendStatusString(s->index, "P"); CHAR_Skillupsend(s->index);
    CHAR_PartyUpdate(s->index, CHAR_N_STRING_MAXHP);
    attr_save(s); attr_view(s, 0); return 1;
}
void stone_attributes_saving(int fd)
{
    AttrSession *s = attr_session(fd); if (!s) return;
    if (s->sent == UINT64_MAX) s->blocked = 1; else ++s->sent;
}
void stone_attributes_saved(int fd, const char *result)
{
    AttrSession *s = attr_session(fd); if (!s || s->acknowledged == s->sent) return;
    uint64_t sequence = ++s->acknowledged;
    if (!s->pending || s->blocked || sequence < s->required || !attr_same(s)) return;
    if (result && !strcmp(result, SUCCESSFUL)) {
        s->pending = 0;
        if (attr_reconcile(s)) attr_view(s, 0);
    } else attr_notice(s, "配点方案保存失败，请稍后重试。");
}
void stone_attributes_logout(int fd)
{
    AttrSession *s = attr_session(fd);
    if (s) { s->sequence = 0; s->initialized = 0; }
}
int stone_attributes_busy(int fd)
{
    if (!CONNECT_checkfd(fd)) return 0;
    for (AttrSession *s = attr_sessions; s; s = s->next)
        if (s->fd == fd && s->epoch == CONNECT_getFdid(fd)) return s->pending || s->uncertain;
    return 0;
}
