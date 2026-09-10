/* Four target-20260805 storage views, backed by complete native object blobs.
 * A row is its durable transfer journal. No asset is published/consumed until
 * the same native snapshot containing caa15 has a matching FIFO save ACK.
 * All entrypoints run on the GMSV/SAAC event loop. */
#include "version.h"
#include "char.h"
#include "char_base.h"
#include "net.h"
#include "item.h"
#include "pet.h"
#include "pet_skill.h"
#include "petmail.h"
#include "npcutil.h"
#include "battle.h"
#include "magic_base.h"
#include "util.h"
#include "lssproto_serv.h"
#include "longzoro/sasql.h"
#include "mylua/mylua.h"
#include "app_storage.h"
#include <mysql/mysql.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STORAGE_PET 1
#define STORAGE_ITEM 2
#define STORAGE_SEQUENCE_BASE 0x53000000
#define STORAGE_SEQUENCE_MASK 0x00ffffff
#define STORAGE_BLOB_SIZE 32768
#define STORAGE_PAYLOAD_SIZE 30000
#define STORAGE_UID_SIZE 128
#define STORAGE_SHARED_ITEM_CAPACITY 100
#define STORAGE_SHARED_PET_CAPACITY 80
enum { STORE_DEPOSIT, STORE_AVAILABLE, STORE_WITHDRAW, STORE_CONSUMED, STORE_CANCELLED };

typedef struct {
    int id, kind, state, bound;
    char scope[512], uid[STORAGE_UID_SIZE];
    char depositor[64], depname[128], withdrawer[64], withname[128];
    char blob[STORAGE_BLOB_SIZE], summary[768], detail[4096];
} StorageRow;

typedef struct StorageSession {
    struct StorageSession *next;
    int fd, epoch, index, initialized, blocked, closing;
    int kind, shared, sequence, master;
    int pending, persisted;
    char operation;
    uint64_t sent, acknowledged, required;
    char account[64], name[128], owner[512], scope[512];
} StorageSession;
static StorageSession *storage_sessions;
static unsigned storage_serial;
/* Root integrates all four modules in the same native target. Check only the
 * other modules here; this module must still reconcile its own saved receipt. */
extern int stone_mail_busy(int fd);
extern int stone_attributes_busy(int fd);
extern int stone_market_busy(int fd);

static const int item_fees[] = {10,10,10,10,10,10,10,10,10,10,20,20,40,40,60,60,
                                80,80,100,100,120,120,140,140,160,160,180,180,200,200,-1};
static const int pet_fees[] = {10,10,20,20,30,30,40,40,50,50,100,100,150,150,200,200,
                               250,250,300,300,-1};

static int storage_number(const char *text, int positive, int *out)
{
    if (!text || !*text) return 0;
    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (INT_MAX - (*p - '0')) / 10) return 0;
        value = value * 10 + (*p - '0');
    }
    if (positive && !value) return 0;
    *out = value; return 1;
}

static void storage_hex(const char *input, char *out)
{
    static const char digits[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        *out++ = digits[*p >> 4]; *out++ = digits[*p & 15];
    }
    *out = 0;
}

static int storage_append(char *out, size_t capacity, const char *format, ...)
{
    size_t used = strlen(out);
    if (used >= capacity) return 0;
    va_list args; va_start(args, format);
    int count = vsnprintf(out + used, capacity - used, format, args);
    va_end(args);
    return count >= 0 && (size_t)count < capacity - used;
}

static int storage_fee(int kind, int capacity)
{
    /* Target's L token 4 supplies the expansion quote. Observed 20->21 quotes
     * are item 300/pet 500; later tiers remain the original table until verified. */
    if (kind == STORAGE_ITEM && capacity == 20) return 300;
    if (kind == STORAGE_PET && capacity == 20) return 500;
    int base = kind == STORAGE_ITEM ? 15 : 10;
    const int *fees = kind == STORAGE_ITEM ? item_fees : pet_fees;
    size_t count = kind == STORAGE_ITEM ? sizeof(item_fees)/sizeof(*item_fees)
                                       : sizeof(pet_fees)/sizeof(*pet_fees);
    return capacity < base || (size_t)(capacity-base) >= count ? -1 : fees[capacity-base];
}

/* 0 empty, -1 reserved/malformed, positive canonical row id. */
static int storage_receipt(const char *text, char *operation)
{
    if (!text || !*text) return 0;
    if (strncmp(text, "storage:", 8)) return -1;
    const char *end = strchr(text + 8, ':');
    if (!end || end-text > 18 || (end[1] != 'D' && end[1] != 'W') || end[2]) return -1;
    char number[12], canonical[40]; int id;
    memcpy(number, text+8, end-(text+8)); number[end-(text+8)] = 0;
    if (!storage_number(number, 1, &id)) return -1;
    snprintf(canonical, sizeof(canonical), "storage:%d:%c", id, end[1]);
    if (strcmp(canonical, text)) return -1;
    *operation = end[1]; return id;
}

static int storage_uid(const char *uid)
{
    if (!uid || !*uid || strlen(uid) >= STORAGE_UID_SIZE) return 0;
    for (const unsigned char *p = (const unsigned char *)uid; *p; ++p)
        if (*p < 33 || *p > 126 || *p == '|' || *p == '\\') return 0;
    return 1;
}

/* SQL adapter: tests replace these boundaries with durable state fixtures;
 * the production SQL is separately compiled and exercised by DB acceptance.
 * Return -1 for uncertainty/errors, 0 for a refused/absent record, 1 success. */
static int storage_db_ready(void);
static int storage_db_master(StorageSession *s);
static int storage_db_capacity(StorageSession *s, int *capacity);
static int storage_db_find(int id, StorageRow *row);
static int storage_db_pending(StorageSession *s, StorageRow *row);
static int storage_db_uid(StorageSession *s, const char *uid, StorageRow *row);
static int storage_db_list(StorageSession *s, char *out, size_t capacity, int *count);
static int storage_db_deposit(StorageSession *s, StorageRow *row);
static int storage_db_withdraw(StorageSession *s, StorageRow *row);
static int storage_db_transition(StorageSession *s, StorageRow *row, int target);
static int storage_db_expand(StorageSession *s, int old_capacity, int cost);

#ifndef STONE_STORAGE_TEST_DB
extern MYSQL mysql;
static int storage_schema_ready;

/* Mutations use mysql_query directly. sasql_mysql_query reconnects and retries
 * arbitrary SQL; retrying a debit/INSERT or continuing a lost transaction can
 * duplicate a charge or publish an uncommitted transfer. Never do that here. */
static int storage_query(const char *sql) { return mysql_query(&mysql, sql) == 0; }

static int storage_db_ready(void)
{
    char probe[] = "DO 0";
    if (sasql_mysql_query(probe)) return 0; /* Only an idempotent reconnect probe. */
    if (storage_schema_ready) return 1;
    if (!storage_query("CREATE TABLE IF NOT EXISTS local_storage_entries ("
        "id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,kind TINYINT NOT NULL,"
        "scope VARBINARY(512) NOT NULL,uid VARBINARY(128) NOT NULL,"
        "active_uid VARBINARY(132) NULL,pending_owner VARBINARY(512) NULL,"
        "state TINYINT NOT NULL,native_blob MEDIUMBLOB NOT NULL,summary BLOB NOT NULL,detail BLOB NOT NULL,"
        "bound TINYINT NOT NULL DEFAULT 0,depositor VARBINARY(64) NOT NULL,depname VARBINARY(128) NOT NULL,"
        "withdrawer VARBINARY(64) NOT NULL DEFAULT '',withname VARBINARY(128) NOT NULL DEFAULT '',"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "UNIQUE KEY active_object(active_uid),UNIQUE KEY one_pending_role(pending_owner),"
        "KEY inventory(scope,kind,state,id)) ENGINE=InnoDB")) return 0;
    if (!storage_query("CREATE TABLE IF NOT EXISTS local_storage_capacity ("
        "scope VARBINARY(512) NOT NULL,kind TINYINT NOT NULL,capacity INT NOT NULL,"
        "PRIMARY KEY(scope,kind)) ENGINE=InnoDB")) return 0;
    storage_schema_ready = 1; return 1;
}

static int storage_scalar(const char *query, int *value)
{
    if (!storage_query(query)) return -1;
    MYSQL_RES *res = mysql_store_result(&mysql);
    if (!res) return -1;
    int status = -1;
    if (mysql_num_fields(res) == 1) {
        MYSQL_ROW cols = mysql_fetch_row(res);
        status = cols ? (storage_number(cols[0], 0, value) ? 1 : -1) : 0;
        if (mysql_fetch_row(res)) status = -1;
    }
    mysql_free_result(res); return status;
}

static int storage_db_master(StorageSession *s)
{
    char account[128], query[256]; storage_hex(s->account, account);
    snprintf(query, sizeof(query), "SELECT MasterId FROM csalogin WHERE BINARY Name=X'%s'", account);
    int master = 0;
    return storage_scalar(query, &master) == 1 && master > 0 ? master : -1;
}

static int storage_db_capacity(StorageSession *s, int *capacity)
{
    char scope[1024], account[128], query[1600];
    storage_hex(s->scope, scope); storage_hex(s->account, account);
    int initial = 20;
    int maximum = s->kind == STORAGE_ITEM ? 45 : 30;
    if (s->shared) {
        initial = s->kind == STORAGE_ITEM ? STORAGE_SHARED_ITEM_CAPACITY : STORAGE_SHARED_PET_CAPACITY;
        maximum = initial;
    } else {
        /* Never silently assign old account-wide assets to a character, and
         * never execute the old item's DELETE-other-UID branch. */
        snprintf(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE BINARY cdkey=X'%s'",
                 s->kind == STORAGE_ITEM ? "poolitem" : "poolpet", account);
        int legacy = 0;
        if (storage_scalar(query, &legacy) != 1) return -1;
        if (legacy) return 0;
        snprintf(query, sizeof(query), "SELECT %s FROM pooldata WHERE BINARY cdkey=X'%s'",
                 s->kind == STORAGE_ITEM ? "itemnum" : "petnum", account);
        int found = storage_scalar(query, &initial);
        if (found < 0 || initial < (s->kind == STORAGE_ITEM ? 15 : 10) || initial > maximum) return -1;
        /* Both target App personal windows have 20 base slots. Preserve the
         * old pooldata table while importing at least that observed base. */
        if (initial < 20) initial = 20;
    }
    snprintf(query, sizeof(query), "INSERT IGNORE INTO local_storage_capacity(scope,kind,capacity) VALUES(X'%s',%d,%d)",
             scope, s->kind, initial);
    if (!storage_query(query)) return -1;
    {
        /* Upgrade old personal 15/10 and shared 50/30 defaults by authenticated
         * scope only, retaining higher purchased capacities. */
        int minimum=s->shared ? (s->kind==STORAGE_ITEM ? 50 : 30) : (s->kind==STORAGE_ITEM ? 15 : 10);
        snprintf(query,sizeof(query),"UPDATE local_storage_capacity SET capacity=GREATEST(capacity,%d) "
                 "WHERE scope=X'%s' AND kind=%d AND capacity BETWEEN %d AND %d",initial,scope,s->kind,minimum,maximum);
        if (!storage_query(query)) return -1;
    }
    snprintf(query, sizeof(query), "SELECT capacity FROM local_storage_capacity WHERE scope=X'%s' AND kind=%d", scope, s->kind);
    if (storage_scalar(query, capacity) != 1 || *capacity < initial || *capacity > maximum) return -1;
    return 1;
}

static int storage_column(char *out, size_t capacity, const char *column, unsigned long length)
{
    if (!column || length >= capacity || strlen(column) != length) return 0;
    memcpy(out, column, length); out[length] = 0; return 1;
}

#define STORAGE_COLUMNS "id,kind,state,bound,scope,uid,depositor,depname,withdrawer,withname,native_blob,summary,detail"
static int storage_read_row(MYSQL_RES *res, MYSQL_ROW c, StorageRow *r)
{
    unsigned long *n = mysql_fetch_lengths(res);
    memset(r, 0, sizeof(*r));
    return n && storage_number(c[0],1,&r->id) && storage_number(c[1],1,&r->kind) &&
        (r->kind == STORAGE_ITEM || r->kind == STORAGE_PET) && storage_number(c[2],0,&r->state) &&
        r->state <= STORE_CANCELLED && storage_number(c[3],0,&r->bound) && r->bound <= 1 &&
        storage_column(r->scope,sizeof(r->scope),c[4],n[4]) &&
        storage_column(r->uid,sizeof(r->uid),c[5],n[5]) && storage_uid(r->uid) &&
        storage_column(r->depositor,sizeof(r->depositor),c[6],n[6]) &&
        storage_column(r->depname,sizeof(r->depname),c[7],n[7]) &&
        storage_column(r->withdrawer,sizeof(r->withdrawer),c[8],n[8]) &&
        storage_column(r->withname,sizeof(r->withname),c[9],n[9]) &&
        storage_column(r->blob,sizeof(r->blob),c[10],n[10]) && *r->blob &&
        storage_column(r->summary,sizeof(r->summary),c[11],n[11]) &&
        storage_column(r->detail,sizeof(r->detail),c[12],n[12]);
}

static int storage_one_row(const char *query, StorageRow *row)
{
    if (!storage_query(query)) return -1;
    MYSQL_RES *res = mysql_store_result(&mysql);
    if (!res) return -1;
    int status = -1;
    if (mysql_num_fields(res) == 13) {
        MYSQL_ROW c = mysql_fetch_row(res);
        status = c ? (storage_read_row(res,c,row) ? 1 : -1) : 0;
        if (mysql_fetch_row(res)) status = -1;
    }
    mysql_free_result(res); return status;
}

static int storage_db_find(int id, StorageRow *row)
{
    char query[512];
    snprintf(query,sizeof(query),"SELECT " STORAGE_COLUMNS " FROM local_storage_entries WHERE id=%d",id);
    return storage_one_row(query,row);
}

static int storage_db_pending(StorageSession *s, StorageRow *row)
{
    char owner[1024],query[1600]; storage_hex(s->owner,owner);
    snprintf(query,sizeof(query),"SELECT " STORAGE_COLUMNS " FROM local_storage_entries WHERE pending_owner=X'%s'",owner);
    return storage_one_row(query,row);
}

static int storage_db_uid(StorageSession *s, const char *uid, StorageRow *row)
{
    char scope[1024],code[256],query[1900]; storage_hex(s->scope,scope); storage_hex(uid,code);
    snprintf(query,sizeof(query),"SELECT " STORAGE_COLUMNS " FROM local_storage_entries "
             "WHERE scope=X'%s' AND kind=%d AND uid=X'%s' AND state=1",scope,s->kind,code);
    return storage_one_row(query,row);
}

static int storage_db_list(StorageSession *s, char *out, size_t capacity, int *count)
{
    char scope[1024],query[1400]; storage_hex(s->scope,scope);
    snprintf(query,sizeof(query),"SELECT summary FROM local_storage_entries WHERE scope=X'%s' AND kind=%d AND state=1 ORDER BY id",scope,s->kind);
    if (!storage_query(query)) return -1;
    MYSQL_RES *res = mysql_store_result(&mysql);
    if (!res) return -1;
    int ok = mysql_num_fields(res) == 1; *count = 0; *out = 0;
    int maximum=s->kind==STORAGE_PET ? STORAGE_SHARED_PET_CAPACITY : STORAGE_SHARED_ITEM_CAPACITY;
    MYSQL_ROW c;
    while (ok && (c = mysql_fetch_row(res))) {
        unsigned long *n = mysql_fetch_lengths(res);
        if (!n || !c[0] || strlen(c[0]) != n[0] || n[0] >= sizeof(((StorageRow*)0)->summary) ||
            ++*count > maximum || !storage_append(out,capacity,"%s",c[0])) ok = 0;
    }
    mysql_free_result(res); return ok ? 1 : -1;
}

static int storage_db_deposit(StorageSession *s, StorageRow *r)
{
    char scope[1024], owner[1024], account[128], name[256], uid[256], active[260];
    char *blob = malloc(strlen(r->blob)*2+1), *summary = malloc(strlen(r->summary)*2+1),
         *detail = malloc(strlen(r->detail)*2+1), *query = malloc(STORAGE_BLOB_SIZE*2+16000);
    if (!blob || !summary || !detail || !query) { free(blob);free(summary);free(detail);free(query); return -1; }
    storage_hex(s->scope,scope); storage_hex(s->owner,owner); storage_hex(s->account,account);
    storage_hex(s->name,name); storage_hex(r->uid,uid); storage_hex(r->blob,blob);
    storage_hex(r->summary,summary); storage_hex(r->detail,detail);
    char active_text[132]; snprintf(active_text,sizeof(active_text),"%d:%s",s->kind,r->uid); storage_hex(active_text,active);
    int result = -1, capacity = 0, count = 0;
    if (!storage_query("START TRANSACTION")) goto done;
    snprintf(query,STORAGE_BLOB_SIZE*2+16000,"SELECT capacity FROM local_storage_capacity WHERE scope=X'%s' AND kind=%d FOR UPDATE",scope,s->kind);
    if (storage_scalar(query,&capacity) != 1) goto rollback;
    snprintf(query,STORAGE_BLOB_SIZE*2+16000,"SELECT COUNT(*) FROM local_storage_entries WHERE scope=X'%s' AND kind=%d AND state IN(0,1,2)",scope,s->kind);
    if (storage_scalar(query,&count) != 1) goto rollback;
    if (count >= capacity) { result = 0; goto rollback; }
    snprintf(query,STORAGE_BLOB_SIZE*2+16000,"INSERT INTO local_storage_entries"
        "(kind,scope,uid,active_uid,pending_owner,state,native_blob,summary,detail,bound,depositor,depname) "
        "VALUES(%d,X'%s',X'%s',X'%s',X'%s',0,X'%s',X'%s',X'%s',%d,X'%s',X'%s')",
        s->kind,scope,uid,active,owner,blob,summary,detail,r->bound,account,name);
    if (!storage_query(query)) goto rollback;
    my_ulonglong id = mysql_insert_id(&mysql);
    if (!id || id > INT_MAX) goto rollback;
    r->id = (int)id;
    if (!storage_query("COMMIT")) goto rollback; /* Ambiguous outcome remains invisible. */
    result = 1; goto done;
rollback:
    storage_query("ROLLBACK");
done:
    free(blob);free(summary);free(detail);free(query);return result;
}

static int storage_db_withdraw(StorageSession *s, StorageRow *r)
{
    char scope[1024],owner[1024],account[128],name[256],query[3000];
    storage_hex(s->scope,scope);storage_hex(s->owner,owner);storage_hex(s->account,account);storage_hex(s->name,name);
    snprintf(query,sizeof(query),"UPDATE local_storage_entries SET state=2,pending_owner=X'%s',withdrawer=X'%s',withname=X'%s' "
        "WHERE id=%d AND scope=X'%s' AND kind=%d AND state=1 AND (bound=0 OR (depositor=X'%s' AND depname=X'%s'))",
        owner,account,name,r->id,scope,s->kind,account,name);
    if (!storage_query(query)) return -1;
    return mysql_affected_rows(&mysql) == 1 ? 1 : 0;
}

static int storage_db_transition(StorageSession *s, StorageRow *r, int target)
{
    int expected = target == STORE_AVAILABLE && r->state == STORE_DEPOSIT ? STORE_DEPOSIT :
                   target == STORE_CANCELLED ? STORE_DEPOSIT : STORE_WITHDRAW;
    char account[128],name[256],query[1600];storage_hex(s->account,account);storage_hex(s->name,name);
    const char *account_field = expected == STORE_DEPOSIT ? "depositor" : "withdrawer";
    const char *name_field = expected == STORE_DEPOSIT ? "depname" : "withname";
    const char *extra = target == STORE_CONSUMED || target == STORE_CANCELLED ? ",active_uid=NULL" :
        expected == STORE_WITHDRAW ? ",withdrawer='',withname=''" : "";
    snprintf(query,sizeof(query),"UPDATE local_storage_entries SET state=%d,pending_owner=NULL%s "
        "WHERE id=%d AND state=%d AND %s=X'%s' AND %s=X'%s'",
        target,extra,r->id,expected,account_field,account,name_field,name);
    if (!storage_query(query)) return -1;
    return mysql_affected_rows(&mysql) == 1 ? 1 : 0;
}

static int storage_db_expand(StorageSession *s, int old_capacity, int cost)
{
    char scope[1024],account[128],query[2300];storage_hex(s->scope,scope);storage_hex(s->account,account);
    int result = -1;
    if (!storage_query("START TRANSACTION")) return -1;
    /* Both tables and vippointlog are InnoDB in the imported schema. A single
     * guarded debit+capacity change and its log commit in the same transaction. */
    snprintf(query,sizeof(query),"UPDATE local_storage_capacity c JOIN csalogin a ON BINARY a.Name=X'%s' "
        "SET c.capacity=c.capacity+1,a.VipPoint=a.VipPoint-%d WHERE c.scope=X'%s' AND c.kind=%d "
        "AND c.capacity=%d AND a.VipPoint>=%d",account,cost,scope,s->kind,old_capacity,cost);
    if (!storage_query(query)) goto rollback;
    if (!mysql_affected_rows(&mysql)) { result = 0; goto rollback; }
    snprintf(query,sizeof(query),"INSERT INTO vippointlog(cdkey,point,oldpoint,newpoint,buff,time) "
        "SELECT Name,-%d,VipPoint+%d,VipPoint,CONCAT('local storage ',%d,' capacity ',%d),NOW() "
        "FROM csalogin WHERE BINARY Name=X'%s'",cost,cost,s->kind,old_capacity+1,account);
    if (!storage_query(query) || mysql_affected_rows(&mysql) != 1) goto rollback;
    if (!storage_query("COMMIT")) goto rollback;
    return 1;
rollback:
    storage_query("ROLLBACK");return result;
}
#endif

static int storage_character(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd) != 4 ||
        !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd)) return -1;
    int index = CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index,CHAR_WHICHTYPE) != CHAR_TYPEPLAYER ||
        CHAR_getWorkInt(index,CHAR_WORKFD) != fd) return -1;
    char account[64],name[128];CONNECT_getCdkey(fd,account,sizeof(account));CONNECT_getCharname(fd,name,sizeof(name));
    return *account && *name && !strcmp(account,CHAR_getChar(index,CHAR_CDKEY)) &&
        !strcmp(name,CHAR_getChar(index,CHAR_NAME)) ? index : -1;
}

static StorageSession *storage_session(int fd)
{
    StorageSession **link = &storage_sessions;
    while (*link) {
        StorageSession *s = *link;
        if (!CONNECT_checkfd(s->fd) || CONNECT_getFdid(s->fd) != s->epoch) {
            *link=s->next;free(s);continue;
        }
        if (s->fd == fd) return s;
        link=&s->next;
    }
    if (!CONNECT_checkfd(fd)) return NULL;
    StorageSession *s=calloc(1,sizeof(*s));if (!s) return NULL;
    s->fd=fd;s->epoch=CONNECT_getFdid(fd);s->index=-1;s->next=storage_sessions;storage_sessions=s;return s;
}

static int storage_same(StorageSession *s)
{
    return s && s->initialized && storage_character(s->fd) == s->index &&
        !strcmp(s->account,CHAR_getChar(s->index,CHAR_CDKEY)) && !strcmp(s->name,CHAR_getChar(s->index,CHAR_NAME));
}

static void storage_notice(StorageSession *s,const char *message)
{
    if (storage_same(s)) CHAR_talkToCli(s->index,-1,(char*)message,CHAR_COLORYELLOW);
}

static int storage_eligible(StorageSession *s)
{
    if (!storage_same(s) || s->closing || s->blocked) return 0;
    if (CHAR_getWorkInt(s->index,CHAR_WORKBATTLEMODE) != BATTLE_CHARMODE_NONE ||
        CHAR_getWorkInt(s->index,CHAR_WORKTRADEMODE) != CHAR_TRADE_FREE) {
        storage_notice(s,"战斗或交易期间不能使用仓库。");return 0;
    }
    int floor=CHAR_getInt(s->index,CHAR_FLOOR);
    if ((floor>=40030 && floor<=40034) || floor==41011 || floor==41012) {
        storage_notice(s,"此地图不可以使用仓库。");return 0;
    }
    return 1;
}

static int storage_other_busy(StorageSession *s)
{
    if (!stone_mail_busy(s->fd) && !stone_attributes_busy(s->fd) && !stone_market_busy(s->fd)) return 0;
    storage_notice(s,"其他操作正在保存，请等待保存完成后再存取或扩容仓库。");return 1;
}

static int storage_owns(StorageSession *s,StorageRow *row,char operation)
{
    return operation=='D' ? !strcmp(row->depositor,s->account) && !strcmp(row->depname,s->name)
                          : !strcmp(row->withdrawer,s->account) && !strcmp(row->withname,s->name);
}

static int storage_inventory_uid(StorageSession *s,int kind,const char *uid)
{
    int count=0;
    if (kind==STORAGE_ITEM) {
        for (int n=0;n<CHAR_MAXITEMHAVE;++n) {
            int index=CHAR_getItemIndex(s->index,n);
            if (ITEM_CHECKINDEX(index) && !strcmp(ITEM_getChar(index,ITEM_UNIQUECODE),uid)) ++count;
        }
    } else {
        for (int n=0;n<CHAR_MAXPETHAVE;++n) {
            int index=CHAR_getCharPet(s->index,n);
            if (CHAR_CHECKINDEX(index) && !strcmp(CHAR_getChar(index,CHAR_UNIQUECODE),uid)) ++count;
        }
    }
    return count;
}

static void storage_send(StorageSession *s,char *data,int opening)
{
    if (!storage_same(s) || s->closing || !s->kind) return;
    if (s->shared) {
        int window=s->kind==STORAGE_ITEM ? 1125 : 1124;
        if (opening) lssproto_WN_send(s->fd,window,0,s->sequence,-1,data);
        else lssproto_WNUpdate_send(s->fd,window,0,s->sequence,-1,data);
    } else lssproto_NewSaMenu_send(s->fd,s->kind,data);
}

static int storage_list(StorageSession *s,int opening)
{
    int capacity,count;
    int status=storage_db_capacity(s,&capacity);
    if (status!=1) {
        storage_notice(s,status==0 ? "旧仓库中有待核对的资产，请联系管理员迁移后使用。" : "仓库容量暂时无法确认，请稍后重试。");
        return 0;
    }
    char *rows=calloc(1,STORAGE_PAYLOAD_SIZE),*payload=calloc(1,STORAGE_PAYLOAD_SIZE);
    if (!rows || !payload) { free(rows);free(payload);return 0; }
    int ok=storage_db_list(s,rows,STORAGE_PAYLOAD_SIZE,&count)==1 && count<=capacity;
    if (ok) {
        ok=storage_append(payload,STORAGE_PAYLOAD_SIZE,"L|%d|%d|",count,capacity);
        if (!s->shared) ok=ok && storage_append(payload,STORAGE_PAYLOAD_SIZE,"%d|",storage_fee(s->kind,capacity));
        ok=ok && storage_append(payload,STORAGE_PAYLOAD_SIZE,"%s",rows);
    }
    if (ok) storage_send(s,payload,opening);
    else storage_notice(s,"仓库列表暂时无法读取，请稍后重试。");
    free(rows);free(payload);return ok;
}

static void storage_refresh_scope(StorageRow *row)
{
    for (StorageSession *s=storage_sessions;s;s=s->next) {
        if (storage_same(s) && !s->closing && s->kind==row->kind && !strcmp(s->scope,row->scope)) {
            /* Full L replaces stale selections and avoids duplicate Z after an
             * uncertain SQL response or another master's character refresh. */
            storage_list(s,0);
        }
    }
}

static int storage_finish(StorageSession *s)
{
    if (!s->pending) return 1;
    if (!s->persisted || s->blocked) return 0;
    StorageRow *r=malloc(sizeof(*r));if (!r) return 0;
    int ok=storage_db_find(s->pending,r)==1 && storage_owns(s,r,s->operation);
    if (ok && s->operation=='D') {
        if (r->state==STORE_DEPOSIT) ok=storage_db_transition(s,r,STORE_AVAILABLE)==1;
        else ok=r->state==STORE_AVAILABLE || r->state==STORE_WITHDRAW || r->state==STORE_CONSUMED;
    } else if (ok) {
        if (r->state==STORE_WITHDRAW) ok=storage_db_transition(s,r,STORE_CONSUMED)==1;
        else ok=r->state==STORE_CONSUMED;
    }
    if (ok) {
        s->pending=0;s->persisted=0;
        storage_refresh_scope(r);
    } else storage_notice(s,"角色已保存，仓库状态尚未确认；请稍后重试，暂时不能继续存取。");
    free(r);return ok;
}

void stone_storage_login(int fd)
{
    int index=storage_character(fd);if (index<0) return;
    StorageSession *s=storage_session(fd);if (!s) return;
    if (s->initialized && !s->closing && storage_same(s)) return;
    if (s->initialized && (s->pending || s->sent!=s->acknowledged)) {
        s->blocked=1;storage_notice(s,"上一角色仓库保存尚未完成，请重新连接。");return;
    }
    s->initialized=1;s->index=index;s->closing=0;s->blocked=0;s->kind=0;s->sequence=0;
    snprintf(s->account,sizeof(s->account),"%s",CHAR_getChar(index,CHAR_CDKEY));
    snprintf(s->name,sizeof(s->name),"%s",CHAR_getChar(index,CHAR_NAME));
    char account[128],name[256];storage_hex(s->account,account);storage_hex(s->name,name);
    snprintf(s->owner,sizeof(s->owner),"P:%s:%s",account,name);
    if (!storage_db_ready()) { s->blocked=1;storage_notice(s,"仓库数据库暂时无法连接，请稍后重新登录。");return; }
    char operation=0;int receipt=storage_receipt(CHAR_getChar(index,CHAR_ACTIVE_ACT15),&operation);
    if (receipt<0) { s->blocked=1;storage_notice(s,"角色的仓库保存字段已被占用，请联系管理员核对。");return; }
    if (receipt>0) {
        s->pending=receipt;s->operation=operation;s->persisted=1;
        if (!storage_finish(s)) { s->blocked=1;return; }
    }
    StorageRow *r=malloc(sizeof(*r));if (!r) { s->blocked=1;return; }
    int found=storage_db_pending(s,r);
    if (found==1) {
        int count=storage_inventory_uid(s,r->kind,r->uid);
        int target=-1;
        if (r->state==STORE_DEPOSIT && storage_owns(s,r,'D') && count==1) target=STORE_CANCELLED;
        if (r->state==STORE_WITHDRAW && storage_owns(s,r,'W') && count==0) target=STORE_AVAILABLE;
        /* Missing receipts are rollback evidence only with matching inventory
         * membership. Ambiguous/corrupt saves retain the row for reconciliation. */
        if (target<0 || storage_db_transition(s,r,target)!=1) s->blocked=1;
    } else if (found<0) s->blocked=1;
    free(r);
    if (s->blocked) storage_notice(s,"仓库与角色存档需要核对，资产已保留，请联系管理员。");
}

/* Details use escaped target fields, while the database retains complete raw
 * native blobs. Round-trip validation refuses assets the original serializer
 * cannot represent, before anything is detached from the player. */
static int storage_item_equal(const ITEM_Item *a,const ITEM_Item *b)
{
    if (memcmp(a->data,b->data,sizeof(a->data))) return 0;
    for (int i=0;i<ITEM_DATACHARNUM;++i) if (strcmp(a->string[i].string,b->string[i].string)) return 0;
    return 1;
}

static int storage_item_bound(const ITEM_Item *item)
{
    return item->string[ITEM_NAME].string[0]=='*' || item->data[ITEM_LOCKED]>0 ||
        *item->string[ITEM_FORUSERCDKEY].string || *item->string[ITEM_FORUSERNAME].string;
}

/* Native pet decoding allocates equipped items but can still return TRUE when
 * an item allocation failed. Verify every serialized equipment slot against
 * the registered result before allowing the enclosing pet to leave the bank. */
static int storage_pet_equipment(const char *blob,Char *pet)
{
    int seen[CHAR_MAXPETITEMHAVE]={0};char token[8192];
    for (int n=1;getStringFromIndexWithDelim((char*)blob,"|",n,token,sizeof(token));++n) {
        if (strncmp(token,"pitem",5)) continue;
        char *colon=strchr(token+5,':');if (!colon) return 0;
        *colon=0;int slot;
        if (!storage_number(token+5,0,&slot) || slot>=CHAR_MAXPETITEMHAVE || seen[slot]) return 0;
        seen[slot]=1;int object=pet->indexOfExistItems[slot];
        if (!ITEM_CHECKINDEX(object)) return 0;
        ITEM_Item expected;memset(&expected,0,sizeof(expected));
        if (!ITEM_makeExistItemsFromStringToArg(colon+1,&expected,1) ||
            !storage_item_equal(&expected,ITEM_getItemPointer(object))) return 0;
    }
    for (int i=0;i<CHAR_MAXPETITEMHAVE;++i)
        if (seen[i] != !!ITEM_CHECKINDEX(pet->indexOfExistItems[i])) return 0;
    return 1;
}

static int storage_item_display(StorageRow *row,ITEM_Item *it)
{
    char name[256],memo[256],typecode[256],inlay[256],magic[256]="",damage[96];
    makeEscapeString(*it->string[ITEM_SECRETNAME].string ? it->string[ITEM_SECRETNAME].string : it->string[ITEM_NAME].string,name,sizeof(name));
    makeEscapeString(it->string[ITEM_EFFECTSTRING].string,memo,sizeof(memo));
    makeEscapeString(it->string[ITEM_TYPECODE].string,typecode,sizeof(typecode));
    makeEscapeString(it->string[ITEM_INLAYCODE].string,inlay,sizeof(inlay));
    int image=it->data[ITEM_BASEIMAGENUMBER];
    if (image<0) image=ITEMTBL_getInt(it->data[ITEM_ID],ITEM_BASEIMAGENUMBER);
    if (it->data[ITEM_MAXDAMAGECRUSHE]<1) snprintf(damage,sizeof(damage),"耐久度：无限");
    else {
        int max=it->data[ITEM_MAXDAMAGECRUSHE]/1000;if (max<1) max=1;
        snprintf(damage,sizeof(damage),"耐久度：%lld%%",(long long)(it->data[ITEM_DAMAGECRUSHE]/1000)*100/max);
    }
    int type=it->data[ITEM_TYPE],use=1;
    if (type==16 || type==20 || type==21 || type==22 || type==23) use=*it->string[ITEM_USEFUNC].string ? 2 : 0;
    if (it->data[ITEM_MAGICID]>0) {
        int m=MAGIC_getMagicArray(it->data[ITEM_MAGICID]);
        if (m>=0) { char *value=MAGIC_getChar(m,MAGIC_NAME);if (value) makeEscapeString(value,magic,sizeof(magic)); }
    }
    return storage_append(row->summary,sizeof(row->summary),"%d|%d|%d|%s|",it->data[ITEM_COLOER],image,it->data[ITEM_USEPILENUMS],row->uid) &&
        storage_append(row->detail,sizeof(row->detail),"I|%s|%s|%s|%s|%d|%s|%s|%s",row->uid,name,damage,memo,use,magic,typecode,inlay);
}

static int storage_value(const char *text,int slot)
{
    char token[64];
    if (!getStringFromIndexWithDelim((char*)text,"|",slot,token,sizeof(token))) return 0;
    char *end;long value=strtol(token,&end,10);
    return *end || value<INT_MIN || value>INT_MAX ? 0 : (int)value;
}

static void storage_pet_stats(const Char *pet,const int bless[4],int values[4])
{
    long long v=(long long)pet->data[CHAR_VITAL]-bless[0]*100LL;
    long long a=(long long)pet->data[CHAR_STR]-bless[1]*100LL;
    long long d=(long long)pet->data[CHAR_TOUGH]-bless[2]*100LL;
    long long q=(long long)pet->data[CHAR_DEX]-bless[3]*100LL;
    values[0]=(int)((v*4+a+d+q)/100);
    values[1]=(int)((a*20+d*2+v*2+q)/2000);
    values[2]=(int)((d*20+a*2+v*2+q)/2000);
    values[3]=(int)(q/100);
}

static int storage_pet_display(StorageRow *row,Char *pet)
{
    char name[512];makeEscapeString(pet->string[CHAR_NAME].string,name,sizeof(name));
    int zero[4]={0},stats[4],bless[4]={0},base[4];storage_pet_stats(pet,zero,stats);
    int skills=pet->data[CHAR_SLOT];
    if (skills<0 || skills>CHAR_MAXPETSKILLHAVE) return 0;
    if (!storage_append(row->summary,sizeof(row->summary),"%s|%d|%d|%d|%d|%d|%d|%s|",name,pet->data[CHAR_LV],stats[0],
        pet->data[CHAR_EARTHAT],pet->data[CHAR_WATERAT],pet->data[CHAR_FIREAT],pet->data[CHAR_WINDAT],row->uid)) return 0;
    char *old=pet->string[CHAR_PET_4V].string;
    if (!storage_append(row->detail,sizeof(row->detail),"I|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|",row->uid,
        pet->data[CHAR_BASEIMAGENUMBER],pet->data[CHAR_TRANSMIGRATION],stats[1],stats[2],stats[3],
        storage_value(old,5),storage_value(old,1),storage_value(old,2),storage_value(old,3),storage_value(old,4),skills)) return 0;
    for (int i=0;i<skills;++i) {
        char escaped[256]="";int skill=PETSKILL_getPetskillArray(pet->unionTable.indexOfPetskill[i]);
        if (skill>=0) { char *value=PETSKILL_getChar(skill,PETSKILL_NAME);if (value) makeEscapeString(value,escaped,sizeof(escaped)); }
        if (!storage_append(row->detail,sizeof(row->detail),"%s|",escaped)) return 0;
    }
    char *bless_text=pet->string[CHAR_NEWNAME].string;
    for (int i=0;i<4;++i) bless[i]=storage_value(bless_text,i+1);
    /* Preserve the source's special 718 display (allocation display only). */
    storage_pet_stats(pet,bless,base);
    if (!*bless_text && pet->data[CHAR_PETID]==718)
        for (int i=0;i<4;++i) bless[i]=((unsigned)pet->data[CHAR_ALLOCPOINT]>>(24-i*8))&255;
    return storage_append(row->detail,sizeof(row->detail),"%d|%d|%d|%d|%d|%d|%d|%d",
        bless[0],bless[1],bless[2],bless[3],base[0],base[1],base[2],base[3]);
}

static int storage_pet_equal(Char *a,Char *b)
{
    if (memcmp(a->data,b->data,sizeof(a->data))) return 0;
    for (int i=0;i<CHAR_DATACHARNUM;++i) if (strcmp(a->string[i].string,b->string[i].string)) return 0;
    for (int i=0;i<CHAR_MAXPETSKILLHAVE;++i)
        if (a->unionTable.indexOfPetskill[i]!=b->unionTable.indexOfPetskill[i]) return 0;
    for (int i=0;i<CHAR_MAXPETITEMHAVE;++i) {
        int ia=a->indexOfExistItems[i],ib=b->indexOfExistItems[i];
        if (ITEM_CHECKINDEX(ia)!=ITEM_CHECKINDEX(ib)) return 0;
        if (ITEM_CHECKINDEX(ia) && !storage_item_equal(ITEM_getItemPointer(ia),ITEM_getItemPointer(ib))) return 0;
    }
    return 1;
}

static int storage_serialize(StorageSession *s,int object,StorageRow *row)
{
    memset(row,0,sizeof(*row));row->kind=s->kind;row->state=STORE_DEPOSIT;
    snprintf(row->scope,sizeof(row->scope),"%s",s->scope);
    snprintf(row->depositor,sizeof(row->depositor),"%s",s->account);
    snprintf(row->depname,sizeof(row->depname),"%s",s->name);
    if (s->kind==STORAGE_ITEM) {
        ITEM_setItemUniCode(object);
        snprintf(row->uid,sizeof(row->uid),"%s",ITEM_getChar(object,ITEM_UNIQUECODE));
        if (!storage_uid(row->uid)) return 0;
        char *blob=ITEM_makeStringFromItemIndex(object,0);
        if (!blob || !*blob || strlen(blob)>=sizeof(row->blob)) return 0;
        strcpy(row->blob,blob);
        ITEM_Item restored;memset(&restored,0,sizeof(restored));
        if (!ITEM_makeExistItemsFromStringToArg(row->blob,&restored,0) ||
            !storage_item_equal(ITEM_getItemPointer(object),&restored)) return 0;
        row->bound=storage_item_bound(ITEM_getItemPointer(object));
        return storage_item_display(row,&restored);
    }
    CHAR_setPetUniCode(object);
    snprintf(row->uid,sizeof(row->uid),"%s",CHAR_getChar(object,CHAR_UNIQUECODE));
    if (!storage_uid(row->uid)) return 0;
    char *blob=CHAR_makePetStringFromPetIndex(object);
    if (!blob || !*blob || strlen(blob)>=sizeof(row->blob)) return 0;
    strcpy(row->blob,blob);
    Char *restored=calloc(1,sizeof(*restored));if (!restored) return 0;
    int ret=CHAR_makePetFromStringToArg(row->blob,restored,0);
    int ok=ret==TRUE && storage_pet_equal(CHAR_getCharPointer(object),restored);
    row->bound=CHAR_getChar(object,CHAR_NAME)[0]=='*' || CHAR_getInt(object,CHAR_LOCKED)>0;
    for (int i=0;i<CHAR_MAXPETITEMHAVE;++i) {
        int item=CHAR_getItemIndex(object,i);
        if (ITEM_CHECKINDEX(item) && storage_item_bound(ITEM_getItemPointer(item))) row->bound=1;
    }
    if (ok) ok=storage_pet_display(row,restored);
    CHAR_endCharData(restored);free(restored);return ok;
}

static void storage_pet_send(StorageSession *s,int slot)
{
    char category[12];snprintf(category,sizeof(category),"K%d",slot);CHAR_sendStatusString(s->index,category);
    snprintf(category,sizeof(category),"W%d",slot);CHAR_sendStatusString(s->index,category);
}

static int storage_attach(StorageSession *s,StorageRow *row,int slot)
{
    if (row->kind==STORAGE_ITEM) {
        ITEM_Item item;memset(&item,0,sizeof(item));
        if (!ITEM_makeExistItemsFromStringToArg(row->blob,&item,0) || strcmp(item.string[ITEM_UNIQUECODE].string,row->uid)) return -1;
        int object=ITEM_initExistItemsOne(&item);if (!ITEM_CHECKINDEX(object)) return -1;
        if (!storage_item_equal(&item,ITEM_getItemPointer(object))) { ITEM_endExistItemsOne(object);return -1; }
        CHAR_setItemIndex(s->index,slot,object);
        if (CHAR_getItemIndex(s->index,slot)!=object || strcmp(ITEM_getChar(object,ITEM_UNIQUECODE),row->uid)) {
            if (CHAR_getItemIndex(s->index,slot)==object) CHAR_setItemIndex(s->index,slot,-1);
            ITEM_endExistItemsOne(object);return -1;
        }
        ITEM_setWorkInt(object,ITEM_WORKCHARAINDEX,s->index);ITEM_setWorkInt(object,ITEM_WORKOBJINDEX,-1);
        return object;
    }
    Char *pet=calloc(1,sizeof(*pet));if (!pet) return -1;
    int ret=CHAR_makePetFromStringToArg(row->blob,pet,slot);
    if (ret!=TRUE || strcmp(pet->string[CHAR_UNIQUECODE].string,row->uid) ||
        !storage_pet_equipment(row->blob,pet)) { CHAR_endCharData(pet);free(pet);return -1; }
    snprintf(pet->string[CHAR_OWNERCDKEY].string,sizeof(pet->string[CHAR_OWNERCDKEY].string),"%s",s->account);
    snprintf(pet->string[CHAR_OWNERCHARANAME].string,sizeof(pet->string[CHAR_OWNERCHARANAME].string),"%s",s->name);
    int object=PET_initCharOneArray(pet);
    if (!CHAR_CHECKINDEX(object)) { CHAR_endCharData(pet);free(pet);return -1; }
    free(pet); /* Registered native pet owns its restored equipment now. */
    CHAR_setCharPet(s->index,slot,object);
    if (CHAR_getCharPet(s->index,slot)!=object || strcmp(CHAR_getChar(object,CHAR_UNIQUECODE),row->uid)) {
        if (CHAR_getCharPet(s->index,slot)==object) CHAR_setCharPet(s->index,slot,-1);
        CHAR_endCharOneArray(object);return -1;
    }
    CHAR_setWorkInt(object,CHAR_WORKPLAYERINDEX,s->index);CHAR_setWorkInt(object,CHAR_WORKOBJINDEX,-1);
    CHAR_complianceParameter(object);return object;
}

static int storage_detach(StorageSession *s,int kind,int slot,int object)
{
    if (kind==STORAGE_ITEM) {
        CHAR_setItemIndex(s->index,slot,-1);
        if (CHAR_getItemIndex(s->index,slot)!=-1) return 0;
        ITEM_endExistItemsOne(object);return 1;
    }
    CHAR_setCharPet(s->index,slot,-1);
    if (CHAR_getCharPet(s->index,slot)!=-1) return 0;
    if (CHAR_getInt(s->index,CHAR_RIDEPET)==slot) {
        CHAR_setInt(s->index,CHAR_RIDEPET,-1);CHAR_send_P_StatusString(s->index,CHAR_P_STRING_RIDEPET);
        CHAR_complianceParameter(s->index);CHAR_sendCToArroundCharacter(CHAR_getWorkInt(s->index,CHAR_WORKOBJINDEX));
    }
    if (CHAR_getInt(s->index,CHAR_DEFAULTPET)==slot) {
        CHAR_setInt(s->index,CHAR_DEFAULTPET,-1);lssproto_KS_send(s->fd,slot,0);
    }
    CHAR_endCharOneArray(object);return 1;
}

static int storage_start(StorageSession *s,StorageRow *row,char operation,int slot,int object)
{
    char old[128],receipt[40];snprintf(old,sizeof(old),"%s",CHAR_getChar(s->index,CHAR_ACTIVE_ACT15));
    snprintf(receipt,sizeof(receipt),"storage:%d:%c",row->id,operation);
    CHAR_setChar(s->index,CHAR_ACTIVE_ACT15,receipt);
    if (strcmp(CHAR_getChar(s->index,CHAR_ACTIVE_ACT15),receipt)) {
        CHAR_setChar(s->index,CHAR_ACTIVE_ACT15,old);
        if (strcmp(CHAR_getChar(s->index,CHAR_ACTIVE_ACT15),old)) s->blocked=1;
        return 0;
    }
    if (operation=='D' && !storage_detach(s,row->kind,slot,object)) {
        CHAR_setChar(s->index,CHAR_ACTIVE_ACT15,old);
        if (strcmp(CHAR_getChar(s->index,CHAR_ACTIVE_ACT15),old)) s->blocked=1;
        return 0;
    }
    s->pending=row->id;s->operation=operation;s->persisted=0;s->required=s->sent+1;
    if (row->kind==STORAGE_ITEM) CHAR_sendItemDataOne(s->index,slot);else storage_pet_send(s,slot);
    if (!CHAR_charSaveFromConnect(s->index,FALSE))
        storage_notice(s,"仓库存取尚未保存，请稍候；资产已保留，暂时不能继续存取。");
    return 1;
}

static void storage_deposit(StorageSession *s,int slot)
{
    if (storage_other_busy(s)) return;
    int object;
    if (s->kind==STORAGE_ITEM) {
        if (slot<CHAR_EQUIPPLACENUM || slot>=CHAR_MAXITEMHAVE) return;
        object=CHAR_getItemIndex(s->index,slot);if (!ITEM_CHECKINDEX(object)) return;
        if (ITEM_getInt(object,ITEM_VANISHATDROP)!=0 || !strcmp(ITEM_getChar(object,ITEM_USEFUNC),"ITEM_useSkup")) {
            storage_notice(s,"特殊道具无法存入仓库。");return;
        }
    } else {
        if (slot<0 || slot>=CHAR_MAXPETHAVE) return;
        object=CHAR_getCharPet(s->index,slot);if (!CHAR_CHECKINDEX(object)) return;
        /* The target App storage has no old NPC event-304/rebirth gate.
         * See evidence/extended-reference-pet-storage-deposit.png. */
        /* FAMILY_Add uses 1 for guardian pets; role leader status uses 3. */
        if (CHAR_getInt(object,CHAR_PETFAMILY)==1) { storage_notice(s,"家族守护兽无法存入仓库。");return; }
        if (CHAR_getInt(object,CHAR_MAILMODE)!=CHAR_PETMAIL_NONE || CHAR_getWorkInt(object,CHAR_WORKOBJINDEX)>=0) {
            storage_notice(s,"请先收回外出或跟随中的宠物。");return;
        }
    }
    StorageRow *row=malloc(sizeof(*row));if (!row) return;
    if (!storage_serialize(s,object,row)) { storage_notice(s,"当前物品或宠物资料无法完整保存，已保留在身上，请联系管理员。");free(row);return; }
    if (storage_inventory_uid(s,s->kind,row->uid)!=1) { storage_notice(s,"物品或宠物编码需要核对，已保留原物。");free(row);return; }
    int status=storage_db_deposit(s,row);
    if (status!=1) {
        if (status<0) s->blocked=1;
        storage_notice(s,status==0 ? "您的仓库已满。" : "仓库预留状态无法确认，原物已保留，请重新连接后核对。");
        free(row);return;
    }
    if (!storage_start(s,row,'D',slot,object)) {
        if (s->blocked || storage_db_transition(s,row,STORE_CANCELLED)!=1) s->blocked=1;
        storage_notice(s,"存入未完成，原物已保留，请稍后重试。");
    }
    free(row);
}

static void storage_withdraw(StorageSession *s,StorageRow *row)
{
    if (storage_other_busy(s)) return;
    if (row->bound && (!storage_owns(s,row,'D'))) { storage_notice(s,"绑定资产只允许原角色取出。");return; }
    int slot=s->kind==STORAGE_ITEM ? CHAR_findEmptyItemBox(s->index) : CHAR_getCharPetElement(s->index);
    if (slot<0) { storage_notice(s,s->kind==STORAGE_ITEM ? "背包已满，请先空出一个位置。" : "宠物栏已满，请先空出一个位置。");return; }
    if (storage_inventory_uid(s,s->kind,row->uid)) { storage_notice(s,"身上已有相同编码，请先核对保存状态。");return; }
    if (s->kind==STORAGE_PET && CHAR_getInt(s->index,CHAR_TRANSMIGRATION)<1) {
        /* Level is from the complete native blob, never from client data. */
        Char *pet=calloc(1,sizeof(*pet));if (!pet) return;
        int ret=CHAR_makePetFromStringToArg(row->blob,pet,slot);
        int allowed=ret==TRUE && pet->data[CHAR_LV]<=(long long)CHAR_getInt(s->index,CHAR_LV)+5;
        CHAR_endCharData(pet);free(pet);
        if (!allowed) { storage_notice(s,"宠物等级不符合原生取出条件，或资料暂时无法恢复。");return; }
    }
    int status=storage_db_withdraw(s,row);
    if (status!=1) {
        if (status<0) s->blocked=1;
        storage_notice(s,status==0 ? "该资产已被取走或正在保存，请刷新仓库。" : "仓库预留状态无法确认，请重新连接后核对。");return;
    }
    row->state=STORE_WITHDRAW;snprintf(row->withdrawer,sizeof(row->withdrawer),"%s",s->account);snprintf(row->withname,sizeof(row->withname),"%s",s->name);
    int object=storage_attach(s,row,slot);
    if (object<0) {
        if (storage_db_transition(s,row,STORE_AVAILABLE)!=1) s->blocked=1;
        storage_notice(s,"资产恢复失败，仓库记录已保留，请稍后重试。");return;
    }
    if (!storage_start(s,row,'W',slot,object)) {
        if (!storage_detach(s,row->kind,slot,object)) s->blocked=1;
        if (!s->blocked && storage_db_transition(s,row,STORE_AVAILABLE)!=1) s->blocked=1;
        storage_notice(s,"取出未完成，仓库记录已保留，请稍后重试。");
    }
}

/* Invoke existing, loaded resource hooks after a confirmed capacity purchase.
 * Missing optional activity resources are not invented; currency/log/capacity
 * are already atomic in SQL and the existing UI gold refresh remains native. */
static void storage_lua(const char *path,const char *function,int index,int a,int b,int nargs)
{
    extern MY_Lua MYLua;
    for (MY_Lua *entry=&MYLua;entry && entry->next;entry=entry->next) {
        if (!entry->luapath || strcmp(entry->luapath,path) || !entry->lua) continue;
        lua_State *lua=entry->lua;int top=lua_gettop(lua);
        lua_getglobal(lua,function);
        if (!lua_isfunction(lua,-1)) { lua_settop(lua,top);return; }
        lua_pushinteger(lua,index);if (nargs>1) lua_pushinteger(lua,a);if (nargs>2) lua_pushinteger(lua,b);
        if (lua_pcall(lua,nargs,0,0)!=LUA_OK) fprintf(stderr,"[storage] optional resource hook failed: %s\n",function);
        lua_settop(lua,top);return;
    }
}

static void storage_expand(StorageSession *s)
{
    if (storage_other_busy(s)) return;
    int capacity;
    if (storage_db_capacity(s,&capacity)!=1) { storage_notice(s,"仓库容量暂时无法确认。");return; }
    int fee=storage_fee(s->kind,capacity);
    if (fee<0) { storage_notice(s,"仓库已经达到原生容量上限。");return; }
    int status=storage_db_expand(s,capacity,fee);
    if (status==1) {
        char data[80];snprintf(data,sizeof(data),"H|%d|%d",capacity+1,storage_fee(s->kind,capacity+1));storage_send(s,data,0);
        storage_lua("data/ablua/npc/huodong/huodong.lua","setCostData",s->index,fee,0,2);
        storage_lua("data/ablua/npc/huoyue/huoyue.lua","updateHuoyue",s->index,4,fee,3);
        storage_lua("data/ablua/dispatchmessage.lua","GetGoldSend",s->index,0,0,1);
        storage_notice(s,"仓库已扩容一个位置，费用已按原规则扣除。");
    } else {
        if (status<0) s->blocked=1;
        storage_notice(s,status==0 ? "您的金币不足，或仓库容量已变化，请刷新后重试。" : "扩容结果尚未确认，请重新连接后查看容量和金币，不要重复付款。");
    }
}

static int storage_command(StorageSession *s,const char *data)
{
    if (!data || !*data || strlen(data)>STORAGE_UID_SIZE+4 || data[1]!='|') return 1;
    if (s->shared && !strcmp(data,"C|")) { s->kind=0;s->sequence=0;return 1; }
    if (!storage_eligible(s) || !storage_db_ready()) return 1;
    if (s->pending && !storage_finish(s)) { storage_notice(s,"上一笔仓库存取正在保存，暂时不能继续操作。");return 1; }
    char operation=0;
    if (storage_receipt(CHAR_getChar(s->index,CHAR_ACTIVE_ACT15),&operation)<0 || s->sent==UINT64_MAX) {
        s->blocked=1;storage_notice(s,"仓库保存字段需要核对，请联系管理员。");return 1;
    }
    if (!strcmp(data,"O|") && !s->shared) { storage_list(s,0);return 1; }
    if (!strcmp(data,"H|")) { if (s->shared) storage_list(s,0);else storage_expand(s);return 1; }
    char arg[STORAGE_UID_SIZE];size_t n=strlen(data+2);
    if (n && data[2+n-1]=='|') --n;
    if (!n || n>=sizeof(arg)) return 1;
    memcpy(arg,data+2,n);arg[n]=0;
    if (strchr(arg,'|')) return 1;
    if (data[0]==(s->shared ? 'I' : 'C')) {
        int slot;if (storage_number(arg,0,&slot)) storage_deposit(s,slot);return 1;
    }
    if ((data[0]!='B' && data[0]!='X') || !storage_uid(arg)) return 1;
    StorageRow *row=malloc(sizeof(*row));if (!row) return 1;
    int found=storage_db_uid(s,arg,row);
    if (found==1 && row->kind==s->kind && !strcmp(row->scope,s->scope) && row->state==STORE_AVAILABLE) {
        if (data[0]=='X') storage_send(s,row->detail,0);else storage_withdraw(s,row);
    } else if (found<0) storage_notice(s,"仓库记录暂时无法读取，请稍后重试。");
    else storage_list(s,0);
    free(row);return 1;
}

static StorageSession *storage_open(int fd,int kind,int shared)
{
    if (storage_character(fd)<0) return NULL;
    stone_storage_login(fd);StorageSession *s=storage_session(fd);
    if (!s || !storage_eligible(s) || !storage_db_ready()) return NULL;
    if (s->pending && !storage_finish(s)) { storage_notice(s,"上一笔仓库存取正在保存，请稍候。");return NULL; }
    s->kind=kind;s->shared=shared;
    if (shared) {
        int master=storage_db_master(s);
        if (master<=0) { s->kind=0;storage_notice(s,"主账号归属暂时无法确认，不能打开共享仓库。");return NULL; }
        s->master=master;snprintf(s->scope,sizeof(s->scope),"S:%d",master);
        if (storage_serial==STORAGE_SEQUENCE_MASK) { s->kind=0;return NULL; }
        s->sequence=STORAGE_SEQUENCE_BASE|++storage_serial;
    } else { snprintf(s->scope,sizeof(s->scope),"%s",s->owner);s->sequence=0; }
    return s;
}

int stone_storage_menu(int fd,int menu)
{
    if (menu!=35 && menu!=36) return 0;
    StorageSession *s=storage_open(fd,menu==35 ? STORAGE_ITEM : STORAGE_PET,1);
    if (s) storage_list(s,1);return 1;
}

int stone_storage_personal(int fd,int index,const char *data)
{
    if (index!=STORAGE_ITEM && index!=STORAGE_PET) return 0;
    if (data && !strcmp(data,"O|")) {
        StorageSession *s=storage_open(fd,index,0);if (s) storage_list(s,0);return 1;
    }
    StorageSession *s=storage_session(fd);
    if (s && storage_same(s) && s->kind==index && !s->shared) storage_command(s,data);
    return 1;
}

int stone_storage_window(int fd,int sequence,int object,int select,const char *data)
{
    if (((unsigned)sequence & ~STORAGE_SEQUENCE_MASK)!=STORAGE_SEQUENCE_BASE) return 0;
    StorageSession *s=storage_session(fd);
    if (s && storage_same(s) && s->kind && s->shared && s->sequence==sequence && object==-1 && select==0) {
        /* Recheck the live authenticated child mapping on every shared action. */
        if (!storage_db_ready() || storage_db_master(s)!=s->master) { s->blocked=1;storage_notice(s,"主账号归属发生变化，请重新连接后打开仓库。");return 1; }
        storage_command(s,data);
    }
    return 1;
}

void stone_storage_saving(int fd)
{
    StorageSession *s=storage_session(fd);if (!s) return;
    if (s->sent==UINT64_MAX) { s->blocked=1;return; }
    ++s->sent;
}

void stone_storage_saved(int fd,const char *result)
{
    StorageSession *s=storage_session(fd);if (!s || s->acknowledged==s->sent) return;
    uint64_t sequence=++s->acknowledged;
    if (!s->pending || s->blocked || sequence<s->required) return;
    if (result && !strcmp(result,SUCCESSFUL)) {
        s->persisted=1;
        if (storage_db_ready()) storage_finish(s);
    } else if (!s->persisted) storage_notice(s,"仓库存取保存失败，资产与凭据已保留；等待下一次完整保存后恢复。");
}

void stone_storage_logout(int fd)
{
    StorageSession *s=storage_session(fd);if (!s) return;
    s->closing=1;s->kind=0;s->sequence=0;
}

int stone_storage_busy(int fd)
{
    if (!CONNECT_checkfd(fd)) return 0;
    for (StorageSession *s=storage_sessions;s;s=s->next)
        if (s->fd==fd && s->epoch==CONNECT_getFdid(fd))
            return s->pending || s->blocked || s->sent!=s->acknowledged;
    return 0;
}

static int storage_asset_session(int index,int kind,StorageSession *s)
{
    if ((kind!=STORAGE_ITEM && kind!=STORAGE_PET) || !CHAR_CHECKINDEX(index)) return 0;
    int fd=CHAR_getWorkInt(index,CHAR_WORKFD);
    if (storage_character(fd)!=index) return 0;
    memset(s,0,sizeof(*s));s->index=index;s->fd=fd;s->kind=kind;
    snprintf(s->account,sizeof(s->account),"%s",CHAR_getChar(index,CHAR_CDKEY));
    snprintf(s->name,sizeof(s->name),"%s",CHAR_getChar(index,CHAR_NAME));return 1;
}

static int storage_asset_slot(int kind,int slot)
{
    return kind==STORAGE_ITEM ? slot>=CHAR_EQUIPPLACENUM && slot<CHAR_MAXITEMHAVE :
           kind==STORAGE_PET && slot>=0 && slot<CHAR_MAXPETHAVE;
}

int stone_asset_serialize(int index,int kind,int slot,StoneNativeAsset *out)
{
    StorageSession s;
    if (!out || !storage_asset_session(index,kind,&s) || !storage_asset_slot(kind,slot)) return 0;
    int object=kind==STORAGE_ITEM ? CHAR_getItemIndex(index,slot) : CHAR_getCharPet(index,slot);
    if (kind==STORAGE_ITEM ? !ITEM_CHECKINDEX(object) : !CHAR_CHECKINDEX(object)) return 0;
    StorageRow *row=malloc(sizeof(*row));if (!row) return 0;
    int ok=storage_serialize(&s,object,row) && storage_inventory_uid(&s,kind,row->uid)==1;
    if (ok) {
        memset(out,0,sizeof(*out));out->kind=kind;out->bound=row->bound;
        strcpy(out->uid,row->uid);strcpy(out->blob,row->blob);strcpy(out->summary,row->summary);strcpy(out->detail,row->detail);
        strcpy(out->owner_account,s.account);strcpy(out->owner_name,s.name);
    }
    free(row);return ok;
}

int stone_asset_restore(int index,int slot,const StoneNativeAsset *asset)
{
    StorageSession s;
    if (!asset || !storage_asset_session(index,asset->kind,&s) || !storage_asset_slot(asset->kind,slot) ||
        !memchr(asset->uid,0,sizeof(asset->uid)) || !storage_uid(asset->uid) ||
        !memchr(asset->blob,0,sizeof(asset->blob)) || !*asset->blob ||
        !memchr(asset->owner_account,0,sizeof(asset->owner_account)) ||
        !memchr(asset->owner_name,0,sizeof(asset->owner_name)) ||
        (asset->bound && (strcmp(asset->owner_account,s.account) || strcmp(asset->owner_name,s.name))) ||
        storage_inventory_uid(&s,asset->kind,asset->uid)) return -1;
    if (asset->kind==STORAGE_ITEM ? CHAR_getItemIndex(index,slot)>=0 : CHAR_getCharPet(index,slot)>=0) return -1;
    StorageRow *row=calloc(1,sizeof(*row));if (!row) return -1;
    row->kind=asset->kind;strcpy(row->uid,asset->uid);strcpy(row->blob,asset->blob);
    int object=storage_attach(&s,row,slot);free(row);return object;
}

int stone_asset_remove(int index,int kind,int slot,const char *uid)
{
    StorageSession s;
    if (!storage_asset_session(index,kind,&s) || !storage_asset_slot(kind,slot) || !storage_uid(uid)) return 0;
    int object=kind==STORAGE_ITEM ? CHAR_getItemIndex(index,slot) : CHAR_getCharPet(index,slot);
    if (kind==STORAGE_ITEM) {
        if (!ITEM_CHECKINDEX(object) || strcmp(ITEM_getChar(object,ITEM_UNIQUECODE),uid)) return 0;
    } else if (!CHAR_CHECKINDEX(object) || strcmp(CHAR_getChar(object,CHAR_UNIQUECODE),uid)) return 0;
    return storage_detach(&s,kind,slot,object);
}

int stone_asset_uid_count(int index,int kind,const char *uid)
{
    StorageSession s;
    if (!storage_asset_session(index,kind,&s) || !storage_uid(uid)) return -1;
    return storage_inventory_uid(&s,kind,uid);
}
