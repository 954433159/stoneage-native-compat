/* Target 20260805 global exchange. A SQL journal escrows complete native
 * objects and coins. Only a successful full native save containing the caa13
 * receipt publishes a listing or completes delivery. Uncertain outcomes stay
 * reserved until persisted receipts/inventory reconcile on a fresh login. */
#include "version.h"
#include "char.h"
#include "char_base.h"
#include "net.h"
#include "item.h"
#include "battle.h"
#include "util.h"
#include "md5_encrypt.h"
#include "lssproto_serv.h"
#include "longzoro/sasql.h"
#ifndef STONE_MARKET_TEST_RULES
#include "mylua/mylua.h"
#endif
#include "app_storage.h"
#include "app_mail.h"
#include "app_market.h"
#include <mysql/mysql.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* Target 1123 has two columns and four rows above its fixed pager. */
#define MARKET_PAGE 8
#define MARKET_SEQUENCE_BASE 0x4b000000
#define MARKET_SEQUENCE_MASK 0x00ffffff
#define MARKET_PAYLOAD 30000
enum { MARKET_DEPOSIT, MARKET_AVAILABLE, MARKET_BUY, MARKET_RETURN,
       MARKET_SOLD, MARKET_RETURNED, MARKET_CANCELLED };
typedef struct {
    int id, category, state, unit, quantity, total, fee, duration, expires;
    StoneNativeAsset asset;
    char name[512], detail[6144];
    int picture;
    char buyer[64], buyname[128];
} MarketRow;
typedef struct MarketSession {
    struct MarketSession *next;
    int fd, epoch, index, initialized, closing, blocked, sequence;
    int mine, category, page, pending, persisted, priming;
    char operation, account[64], name[128], owner[512];
    uint64_t sent, acknowledged, required;
} MarketSession;
static MarketSession *market_sessions;
static unsigned market_serial;
extern int stone_attributes_busy(int fd);

static int market_number(const char *s, int negative_one, int *out)
{
    if (!s || !*s) return 0;
    if (negative_one && !strcmp(s,"-1")) { *out=-1; return 1; }
    int value=0;
    for (const unsigned char *p=(const unsigned char*)s;*p;++p) {
        if (*p<'0' || *p>'9' || value>(INT_MAX-(*p-'0'))/10) return 0;
        value=value*10+(*p-'0');
    }
    *out=value;return 1;
}
static void market_hex(const char *s, char *out)
{
    static const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char*)s;*p;++p) { *out++=hex[*p>>4]; *out++=hex[*p&15]; }
    *out=0;
}
static int market_append(char *out,size_t size,const char *format,...)
{
    size_t used=strlen(out);if (used>=size) return 0;
    va_list args;va_start(args,format);int n=vsnprintf(out+used,size-used,format,args);va_end(args);
    return n>=0 && (size_t)n<size-used;
}
static int market_receipt(const char *text,char *operation)
{
    if (!text || !*text) return 0;
    if (strncmp(text,"market:",7)) return -1;
    const char *end=strchr(text+7,':');
    if (!end || end-text>17 || !strchr("DBW",end[1]) || !end[1] || end[2]) return -1;
    char number[12],canonical[40];int id;
    memcpy(number,text+7,end-(text+7));number[end-(text+7)]=0;
    if (!market_number(number,0,&id) || id<1) return -1;
    snprintf(canonical,sizeof(canonical),"market:%d:%c",id,end[1]);
    if (strcmp(canonical,text)) return -1;
    *operation=end[1];return id;
}
static int market_owned(MarketSession *s,const MarketRow *r)
{ return !strcmp(s->account,r->asset.owner_account) && !strcmp(s->name,r->asset.owner_name); }
static int market_actor(MarketSession *s,const MarketRow *r,char operation)
{
    return operation=='B' ? !strcmp(s->account,r->buyer) && !strcmp(s->name,r->buyname) : market_owned(s,r);
}

/* 1 committed/success, 0 known refusal/absence, -1 error or ambiguous result.
 * Production mutations never use the legacy automatic reconnect/retry path. */
static time_t market_now(void);
static int market_db_ready(void);
static int market_db_find(int id,MarketRow *r);
static int market_db_pending(MarketSession *s,MarketRow *r);
static int market_db_list(MarketSession *s,int mine,int category,int page,MarketRow *rows,int *count,int *pages);
static int market_db_stage(MarketSession *s,MarketRow *r,char operation);
static int market_db_finish(MarketSession *s,MarketRow *r,int target);
static int market_db_pay(const char *account);

#ifndef STONE_MARKET_TEST_DB
extern MYSQL mysql;
static int market_schema_ready;
static time_t market_now(void) { return time(NULL); }
static int market_query(const char *sql) { return mysql_query(&mysql,sql)==0; }
static int market_scalar64(const char *sql,long long *out)
{
    if (!market_query(sql)) return -1;
    MYSQL_RES *res=mysql_store_result(&mysql);if (!res) return -1;
    int status=-1;
    if (mysql_num_fields(res)==1) {
        MYSQL_ROW row=mysql_fetch_row(res);status=row ? -1 : 0;
        if (row && row[0] && *row[0]) {
            long long value=0;int valid=1;
            for (const unsigned char *p=(const unsigned char*)row[0];*p;++p) {
                if (*p<'0' || *p>'9' || value>(LLONG_MAX-(*p-'0'))/10) { valid=0;break; }
                value=value*10+(*p-'0');
            }
            if (valid) { *out=value;status=1; }
        }
        if (mysql_fetch_row(res)) status=-1;
    }
    mysql_free_result(res);return status;
}
static int market_db_ready(void)
{
    char probe[]="DO 0";if (sasql_mysql_query(probe)) return 0;
    if (market_schema_ready) return 1;
    if (!market_query("CREATE TABLE IF NOT EXISTS local_market_entries ("
        "id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,category TINYINT NOT NULL,state TINYINT NOT NULL,"
        "unitprice INT NOT NULL,quantity INT NOT NULL,totalprice INT NOT NULL,fee INT NOT NULL,"
        "duration INT NOT NULL,expires INT NOT NULL,kind TINYINT NOT NULL,bound TINYINT NOT NULL,"
        "uid VARBINARY(128) NOT NULL,active_uid VARBINARY(132) NULL,pending_owner VARBINARY(512) NULL,"
        "seller VARBINARY(64) NOT NULL,sellname VARBINARY(128) NOT NULL,"
        "buyer VARBINARY(64) NOT NULL DEFAULT '',buyname VARBINARY(128) NOT NULL DEFAULT '',"
        "native_blob MEDIUMBLOB NOT NULL,summary BLOB NOT NULL,asset_detail BLOB NOT NULL,"
        "display_name BLOB NOT NULL,picture INT NOT NULL,market_detail BLOB NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "UNIQUE KEY active_object(active_uid),UNIQUE KEY one_pending_role(pending_owner),"
        "KEY browsing(category,state,expires,id),KEY ownership(seller,sellname,kind,state,id)) ENGINE=InnoDB")) return 0;
    if (!market_query("CREATE TABLE IF NOT EXISTS local_market_income ("
        "account VARBINARY(64) NOT NULL PRIMARY KEY,amount BIGINT NOT NULL DEFAULT 0,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP) ENGINE=InnoDB")) return 0;
    long long engines=0;
    if (market_scalar64("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() "
        "AND table_name IN('local_market_entries','local_market_income','csalogin','vippointlog') AND engine='InnoDB'",&engines)!=1 || engines!=4) return 0;
    market_schema_ready=1;return 1;
}
static int market_column(char *out,size_t size,const char *s,unsigned long n)
{
    if (!s || n>=size || strlen(s)!=n) return 0;
    memcpy(out,s,n);out[n]=0;return 1;
}
#define MARKET_COLUMNS "id,category,state,unitprice,quantity,totalprice,fee,duration,expires,kind,bound,uid,seller,sellname,buyer,buyname,native_blob,summary,asset_detail,display_name,picture,market_detail"
static int market_read_row(MYSQL_RES *res,MYSQL_ROW c,MarketRow *r)
{
    unsigned long *n=mysql_fetch_lengths(res);if (!n) return 0;memset(r,0,sizeof(*r));
    int *numbers[]={&r->id,&r->category,&r->state,&r->unit,&r->quantity,&r->total,&r->fee,&r->duration,&r->expires,&r->asset.kind,&r->asset.bound};
    for (int i=0;i<11;++i) if (!market_number(c[i],0,numbers[i])) return 0;
    if (r->id<1 || r->category<1 || r->category>3 || r->state>MARKET_CANCELLED || r->unit<100 || r->quantity<1 ||
        (long long)r->unit*r->quantity!=r->total || r->fee!=((long long)r->total+19)/20 ||
        (r->duration!=24 && r->duration!=48 && r->duration!=168) || r->expires<1 ||
        (r->asset.kind!=STONE_ASSET_PET && r->asset.kind!=STONE_ASSET_ITEM) || r->asset.bound!=0) return 0;
    return market_column(r->asset.uid,sizeof(r->asset.uid),c[11],n[11]) && *r->asset.uid &&
        market_column(r->asset.owner_account,sizeof(r->asset.owner_account),c[12],n[12]) && *r->asset.owner_account &&
        market_column(r->asset.owner_name,sizeof(r->asset.owner_name),c[13],n[13]) && *r->asset.owner_name &&
        market_column(r->buyer,sizeof(r->buyer),c[14],n[14]) && market_column(r->buyname,sizeof(r->buyname),c[15],n[15]) &&
        market_column(r->asset.blob,sizeof(r->asset.blob),c[16],n[16]) && *r->asset.blob &&
        market_column(r->asset.summary,sizeof(r->asset.summary),c[17],n[17]) &&
        market_column(r->asset.detail,sizeof(r->asset.detail),c[18],n[18]) &&
        market_column(r->name,sizeof(r->name),c[19],n[19]) && market_number(c[20],0,&r->picture) &&
        market_column(r->detail,sizeof(r->detail),c[21],n[21]);
}
static int market_one(const char *sql,MarketRow *r)
{
    if (!market_query(sql)) return -1;
    MYSQL_RES *res=mysql_store_result(&mysql);if (!res) return -1;
    int status=-1;
    if (mysql_num_fields(res)==22) {
        MYSQL_ROW c=mysql_fetch_row(res);status=c ? (market_read_row(res,c,r)?1:-1) : 0;
        if (mysql_fetch_row(res)) status=-1;
    }
    mysql_free_result(res);return status;
}
static int market_db_find(int id,MarketRow *r)
{
    char sql[600];snprintf(sql,sizeof(sql),"SELECT " MARKET_COLUMNS " FROM local_market_entries WHERE id=%d",id);
    return market_one(sql,r);
}
static int market_db_pending(MarketSession *s,MarketRow *r)
{
    char owner[1024],sql[1800];market_hex(s->owner,owner);
    snprintf(sql,sizeof(sql),"SELECT " MARKET_COLUMNS " FROM local_market_entries WHERE pending_owner=X'%s'",owner);
    return market_one(sql,r);
}
static int market_db_list(MarketSession *s,int mine,int category,int page,MarketRow *rows,int *count,int *pages)
{
    char filter[800],sql[1500],account[128],name[256];market_hex(s->account,account);market_hex(s->name,name);
    if (mine) snprintf(filter,sizeof(filter),"seller=X'%s' AND sellname=X'%s' AND kind=%d AND state IN(1,4)",account,name,category);
    else snprintf(filter,sizeof(filter),"category=%d AND state=1 AND expires>%lld",category,(long long)market_now());
    snprintf(sql,sizeof(sql),"SELECT COUNT(*) FROM local_market_entries WHERE %s",filter);
    long long total=0;if (market_scalar64(sql,&total)!=1 || total>INT_MAX) return -1;
    *pages=(int)((total+MARKET_PAGE-1)/MARKET_PAGE);if (*pages<1) *pages=1;if (page>*pages) page=*pages;
    snprintf(sql,sizeof(sql),"SELECT " MARKET_COLUMNS " FROM local_market_entries WHERE %s ORDER BY id DESC LIMIT %d OFFSET %lld",filter,MARKET_PAGE,(long long)(page-1)*MARKET_PAGE);
    if (!market_query(sql)) return -1;
    MYSQL_RES *res=mysql_store_result(&mysql);if (!res) return -1;
    int ok=mysql_num_fields(res)==22;*count=0;MYSQL_ROW c;
    while (ok && (c=mysql_fetch_row(res))) {
        if (*count>=MARKET_PAGE || !market_read_row(res,c,&rows[*count])) ok=0;
        else ++*count;
    }
    mysql_free_result(res);return ok ? 1 : -1;
}
static char *market_hex_alloc(const char *s)
{ char *p=malloc(strlen(s)*2+1);if (p) market_hex(s,p);return p; }
static int market_log(const char *account,int amount,const char *reason,int id)
{
    char key[128],sql[1300];market_hex(account,key);
    snprintf(sql,sizeof(sql),"INSERT INTO vippointlog(cdkey,point,oldpoint,newpoint,buff,time) "
        "SELECT Name,%d,VipPoint-(%d),VipPoint,CONCAT('local market ',%d,' %s'),NOW() FROM csalogin WHERE BINARY Name=X'%s'",amount,amount,id,reason,key);
    return market_query(sql) && mysql_affected_rows(&mysql)==1;
}
static int market_debit(const char *account,int cost,int id,const char *reason)
{
    char key[128],sql[600];market_hex(account,key);
    snprintf(sql,sizeof(sql),"UPDATE csalogin SET VipPoint=VipPoint-%d WHERE BINARY Name=X'%s' AND VipPoint>=%d",cost,key,cost);
    if (!market_query(sql)) return -1;
    if (mysql_affected_rows(&mysql)!=1) return 0;
    return market_log(account,-cost,reason,id) ? 1 : -1;
}
static int market_credit_ledger(const char *account,int amount)
{
    char key[128],sql[600];market_hex(account,key);
    snprintf(sql,sizeof(sql),"INSERT IGNORE INTO local_market_income(account,amount) VALUES(X'%s',0)",key);
    if (!market_query(sql)) return 0;
    snprintf(sql,sizeof(sql),"UPDATE local_market_income SET amount=amount+%d WHERE account=X'%s' AND amount<=%lld",amount,key,LLONG_MAX-amount);
    return market_query(sql) && mysql_affected_rows(&mysql)==1;
}
static int market_db_stage(MarketSession *s,MarketRow *r,char operation)
{
    char owner[1024],account[128],name[256],sql[2400];market_hex(s->owner,owner);market_hex(s->account,account);market_hex(s->name,name);
    int status=-1;
    if (!market_query("START TRANSACTION")) return -1;
    if (operation=='D') {
        char active[132];snprintf(active,sizeof(active),"%d:%s",r->asset.kind,r->asset.uid);
        const char *values[]={r->asset.uid,active,r->asset.blob,r->asset.summary,r->asset.detail,r->name,r->detail};
        char *hex[7]={0},*insert=malloc(130000);int ok=insert!=NULL;
        for (int i=0;i<7;++i) { hex[i]=market_hex_alloc(values[i]);if (!hex[i]) ok=0; }
        if (ok) {
            snprintf(insert,130000,"INSERT INTO local_market_entries(category,state,unitprice,quantity,totalprice,fee,duration,expires,kind,bound,uid,active_uid,pending_owner,seller,sellname,native_blob,summary,asset_detail,display_name,picture,market_detail) "
                "VALUES(%d,0,%d,%d,%d,%d,%d,%d,%d,0,X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',%d,X'%s')",
                r->category,r->unit,r->quantity,r->total,r->fee,r->duration,r->expires,r->asset.kind,hex[0],hex[1],owner,account,name,hex[2],hex[3],hex[4],hex[5],r->picture,hex[6]);
            if (!market_query(insert)) { if (mysql_errno(&mysql)==1062) status=0;ok=0; }
        }
        for (int i=0;i<7;++i) free(hex[i]);
        free(insert);
        if (!ok) goto rollback;
        my_ulonglong id=mysql_insert_id(&mysql);if (!id || id>INT_MAX) goto rollback;r->id=(int)id;
        status=market_debit(s->account,r->fee,r->id,"listing fee");if (status!=1) goto rollback;
        r->state=MARKET_DEPOSIT;
    } else {
        MarketRow *locked=malloc(sizeof(*locked));if (!locked) goto rollback;
        snprintf(sql,sizeof(sql),"SELECT " MARKET_COLUMNS " FROM local_market_entries WHERE id=%d FOR UPDATE",r->id);
        int found=market_one(sql,locked);
        if (found!=1) { free(locked);status=found;goto rollback; }
        int valid=locked->state==MARKET_AVAILABLE && (operation=='W' ? market_owned(s,locked) :
            operation=='B' && strcmp(s->account,locked->asset.owner_account) && locked->expires>market_now());
        if (!valid) { free(locked);status=0;goto rollback; }
        *r=*locked;free(locked);
        if (operation=='B') { status=market_debit(s->account,r->total,r->id,"purchase escrow");if (status!=1) goto rollback; }
        snprintf(sql,sizeof(sql),"UPDATE local_market_entries SET state=%d,pending_owner=X'%s',buyer=X'%s',buyname=X'%s' WHERE id=%d AND state=1",
            operation=='B' ? MARKET_BUY : MARKET_RETURN,owner,operation=='B'?account:"",operation=='B'?name:"",r->id);
        if (!market_query(sql) || mysql_affected_rows(&mysql)!=1) { status=-1;goto rollback; }
        r->state=operation=='B' ? MARKET_BUY : MARKET_RETURN;
        if (operation=='B') { strcpy(r->buyer,s->account);strcpy(r->buyname,s->name); }
    }
    if (!market_query("COMMIT")) { status=-1;goto rollback; }
    return 1;
rollback:
    if (!market_query("ROLLBACK")) status=-1;
    return status;
}
static int market_db_finish(MarketSession *s,MarketRow *r,int target)
{
    char sql[2400];int result=-1;MarketRow *locked=malloc(sizeof(*locked));if (!locked) return -1;
    if (!market_query("START TRANSACTION")) goto done;
    snprintf(sql,sizeof(sql),"SELECT " MARKET_COLUMNS " FROM local_market_entries WHERE id=%d FOR UPDATE",r->id);
    int found=market_one(sql,locked);if (found!=1) { result=found;goto rollback; }
    char operation=locked->state==MARKET_DEPOSIT?'D':locked->state==MARKET_BUY?'B':locked->state==MARKET_RETURN?'W':0;
    int valid=operation && locked->state==r->state && market_actor(s,locked,operation) &&
        (operation=='D' ? target==MARKET_AVAILABLE || target==MARKET_CANCELLED :
         operation=='B' ? target==MARKET_SOLD || target==MARKET_AVAILABLE : target==MARKET_RETURNED || target==MARKET_AVAILABLE);
    if (!valid) { result=0;goto rollback; }
    if (target==MARKET_SOLD && !market_credit_ledger(locked->asset.owner_account,locked->total)) goto rollback;
    if (target==MARKET_CANCELLED && !market_credit_ledger(locked->asset.owner_account,locked->fee)) goto rollback;
    if (operation=='B' && target==MARKET_AVAILABLE && !market_credit_ledger(locked->buyer,locked->total)) goto rollback;
    const char *terminal=target==MARKET_SOLD || target==MARKET_RETURNED || target==MARKET_CANCELLED ? ",active_uid=NULL" : "";
    const char *clearbuyer=operation=='B' && target==MARKET_AVAILABLE ? ",buyer='',buyname=''" : "";
    snprintf(sql,sizeof(sql),"UPDATE local_market_entries SET state=%d,pending_owner=NULL%s%s WHERE id=%d AND state=%d",target,terminal,clearbuyer,r->id,locked->state);
    if (!market_query(sql) || mysql_affected_rows(&mysql)!=1) goto rollback;
    if (!market_query("COMMIT")) goto rollback;
    r->state=target;result=1;goto done;
rollback:
    if (!market_query("ROLLBACK")) result=-1;
done:
    free(locked);return result;
}
static int market_db_pay(const char *account)
{
    char key[128],sql[900];market_hex(account,key);int result=-1;
    if (!market_query("START TRANSACTION")) return -1;
    snprintf(sql,sizeof(sql),"SELECT amount FROM local_market_income WHERE account=X'%s' FOR UPDATE",key);
    long long income=0;int found=market_scalar64(sql,&income);if (found<0) goto rollback;
    if (!found || !income) { if (market_query("COMMIT")) return 1;goto rollback; }
    snprintf(sql,sizeof(sql),"SELECT VipPoint FROM csalogin WHERE BINARY Name=X'%s' FOR UPDATE",key);
    long long balance=0;if (market_scalar64(sql,&balance)!=1 || balance>INT_MAX) goto rollback;
    int payment=(int)(income<INT_MAX-balance ? income : INT_MAX-balance);
    if (payment==0) { if (market_query("COMMIT")) return 1;goto rollback; }
    snprintf(sql,sizeof(sql),"UPDATE csalogin SET VipPoint=VipPoint+%d WHERE BINARY Name=X'%s' AND VipPoint=%lld",payment,key,balance);
    if (!market_query(sql) || mysql_affected_rows(&mysql)!=1) goto rollback;
    snprintf(sql,sizeof(sql),"UPDATE local_market_income SET amount=amount-%d WHERE account=X'%s' AND amount=%lld",payment,key,income);
    if (!market_query(sql) || mysql_affected_rows(&mysql)!=1 || !market_log(account,payment,"income/refund",0)) goto rollback;
    if (market_query("COMMIT")) return 1;
rollback:
    market_query("ROLLBACK");return result;
}
#endif

static int market_character(int fd)
{
    if (!CONNECT_checkfd(fd) || CONNECT_getloginmark(fd)!=4 || !CONNECT_isCLI(fd) || !CONNECT_isLOGIN(fd)) return -1;
    int index=CONNECT_getCharaindex(fd);
    if (!CHAR_CHECKINDEX(index) || CHAR_getInt(index,CHAR_WHICHTYPE)!=CHAR_TYPEPLAYER || CHAR_getWorkInt(index,CHAR_WORKFD)!=fd) return -1;
    char account[64],name[128];CONNECT_getCdkey(fd,account,sizeof(account));CONNECT_getCharname(fd,name,sizeof(name));
    return *account && *name && !strcmp(account,CHAR_getChar(index,CHAR_CDKEY)) && !strcmp(name,CHAR_getChar(index,CHAR_NAME)) ? index : -1;
}
static MarketSession *market_session(int fd)
{
    MarketSession **link=&market_sessions;
    while (*link) {
        MarketSession *s=*link;
        if (!CONNECT_checkfd(s->fd) || CONNECT_getFdid(s->fd)!=s->epoch) { *link=s->next;free(s);continue; }
        if (s->fd==fd) return s;
        link=&s->next;
    }
    if (!CONNECT_checkfd(fd)) return NULL;
    MarketSession *s=calloc(1,sizeof(*s));if (!s) return NULL;
    s->fd=fd;s->epoch=CONNECT_getFdid(fd);s->index=-1;s->next=market_sessions;market_sessions=s;return s;
}
static int market_same(MarketSession *s)
{
    return s && s->initialized && market_character(s->fd)==s->index &&
        !strcmp(s->account,CHAR_getChar(s->index,CHAR_CDKEY)) && !strcmp(s->name,CHAR_getChar(s->index,CHAR_NAME));
}
static void market_notice(MarketSession *s,const char *message)
{ if (market_same(s) && !s->closing) CHAR_talkToCli(s->index,-1,(char*)message,CHAR_COLORYELLOW); }
static int market_eligible(MarketSession *s,int mutation)
{
    if (!market_same(s) || s->closing || s->blocked) return 0;
    if (CHAR_getWorkInt(s->index,CHAR_WORKBATTLEMODE)!=BATTLE_CHARMODE_NONE || CHAR_getWorkInt(s->index,CHAR_WORKTRADEMODE)!=CHAR_TRADE_FREE) {
        market_notice(s,"战斗或交易期间不能使用交易所。");return 0;
    }
    int floor=CHAR_getInt(s->index,CHAR_FLOOR);
    if ((floor>=40030 && floor<=40034) || floor==41011 || floor==41012) { market_notice(s,"此地图不可以使用交易所。");return 0; }
    if (mutation && (stone_storage_busy(s->fd) || stone_mail_busy(s->fd) || stone_attributes_busy(s->fd))) {
        market_notice(s,"其他角色操作正在保存，请稍候再使用交易所。");return 0;
    }
    return 1;
}
static void market_gold(const char *account)
{
    extern MY_Lua MYLua;
    for (MarketSession *s=market_sessions;s;s=s->next) {
        if (!market_same(s) || s->closing || strcmp(account,s->account)) continue;
        for (MY_Lua *entry=&MYLua;entry && entry->next;entry=entry->next) {
            if (!entry->luapath || strcmp(entry->luapath,"data/ablua/dispatchmessage.lua") || !entry->lua) continue;
            lua_State *lua=entry->lua;int top=lua_gettop(lua);lua_getglobal(lua,"GetGoldSend");
            if (lua_isfunction(lua,-1)) {
                lua_pushinteger(lua,s->index);
                if (lua_pcall(lua,1,0,0)!=LUA_OK) fprintf(stderr,"[market] gold display refresh failed\n");
            }
            lua_settop(lua,top);break;
        }
    }
}
static int market_header(char *out,size_t size,const char *kind)
{
    time_t now=market_now();struct tm local;char text[80]="cwdzzkeys",date[32];
    if (!localtime_r(&now,&local) || !strftime(date,sizeof(date),"%m-%d:%H",&local)) return 0;
    strcat(text,date);char *digest=MD5String(text);if (!digest || strlen(digest)<10) return 0;
    return market_append(out,size,"%.10s|交易所|%s|",digest,kind);
}
static int market_sequence(MarketSession *s)
{
    if (market_serial==MARKET_SEQUENCE_MASK) { s->blocked=1;return 0; }
    s->sequence=MARKET_SEQUENCE_BASE|++market_serial;return 1;
}
static void market_send(MarketSession *s,char *payload,int opening)
{
    if (!market_same(s) || s->closing || !s->sequence) return;
    if (opening) lssproto_WN_send(s->fd,1123,0,s->sequence,-1,payload);
    else lssproto_WNUpdate_send(s->fd,1123,0,s->sequence,-1,payload);
}
static int market_list(MarketSession *s,int mine,int category,int page,int opening)
{
    MarketRow *rows=calloc(MARKET_PAGE,sizeof(*rows));char *payload=calloc(1,MARKET_PAYLOAD);
    if (!rows || !payload) { free(rows);free(payload);return 0; }
    int count=0,pages=0;
    int ok=market_db_list(s,mine,category,page,rows,&count,&pages)==1 && count>=0 && count<=MARKET_PAGE && pages>=1;
    if (page>pages) page=pages;
    if (ok) ok=market_header(payload,MARKET_PAYLOAD,mine ? "M" : "L");
    if (ok && !mine) ok=market_append(payload,MARKET_PAYLOAD,"3|1|宠物|2|装备|3|物品|");
    if (ok) ok=market_append(payload,MARKET_PAYLOAD,"%d|%d|%d|%d|",category,page,pages,count);
    for (int i=0;ok && i<count;++i) {
        MarketRow *r=&rows[i];
        /* Target 1123 draws a static graphic, while pet records retain their
         * native animation number. External client lists use common icon
         * 61159; apply it here also for existing listings and seller history. */
        int picture=r->asset.kind==STONE_ASSET_PET ? 61159 : r->picture;
        ok=market_append(payload,MARKET_PAYLOAD,"%s|%d|%d|%d|%d|",r->name,picture,r->total,r->quantity,r->id);
        if (ok && mine) ok=market_append(payload,MARKET_PAYLOAD,"%d|%d|",r->expires,r->state==MARKET_SOLD);
    }
    if (ok) { s->mine=mine;s->category=category;s->page=page;market_send(s,payload,opening); }
    else market_notice(s,"交易所列表暂时无法读取，请稍后重试。");
    free(rows);free(payload);return ok;
}
static void market_refresh(MarketSession *changed)
{
    for (MarketSession *s=market_sessions;s;s=s->next) {
        if (!market_same(s) || s->closing || s->blocked || s->pending || !s->sequence) continue;
        /* A fresh WN rotates callback credentials and clears stale selections.
         * Replaying a completed U/B/G with its old sequence cannot sell again. */
        if (s==changed && !market_sequence(s)) continue;
        market_list(s,s->mine,s->category,s->page,s==changed);
    }
}
static int market_finish(MarketSession *s)
{
    if (!s->pending) return 1;
    if (!s->persisted || s->blocked) return 0;
    MarketRow *r=malloc(sizeof(*r));if (!r) return 0;
    int found=market_db_find(s->pending,r),ok=found==1 && market_actor(s,r,s->operation);
    int target=s->operation=='D' ? MARKET_AVAILABLE : s->operation=='B' ? MARKET_SOLD : MARKET_RETURNED;
    int expected=s->operation=='D' ? MARKET_DEPOSIT : s->operation=='B' ? MARKET_BUY : MARKET_RETURN;
    if (ok && r->state==expected) ok=market_db_finish(s,r,target)==1;
    else if (ok) ok=s->operation=='D' ? (r->state>=MARKET_AVAILABLE && r->state<=MARKET_RETURNED) : r->state==target;
    if (ok) {
        s->pending=0;s->persisted=0;
        if (s->operation=='B') { market_db_pay(r->asset.owner_account);market_gold(r->asset.owner_account); }
        market_db_pay(s->account);market_gold(s->account);market_refresh(s);
        market_notice(s,"交易所操作已保存完成。");
    } else market_notice(s,"角色已保存，交易所记录正在确认；请稍候，不要重复提交。");
    free(r);return ok;
}
void stone_market_login(int fd)
{
    int index=market_character(fd);if (index<0) return;
    MarketSession *s=market_session(fd);if (!s) return;
    if (s->initialized && !s->closing && market_same(s)) return;
    if (s->initialized && (s->pending || s->priming || s->sent!=s->acknowledged)) { s->blocked=1;market_notice(s,"上一角色保存未完成，请重新连接。");return; }
    s->initialized=1;s->index=index;s->closing=0;s->blocked=0;s->sequence=0;
    s->mine=0;s->category=1;s->page=1;
    snprintf(s->account,sizeof(s->account),"%s",CHAR_getChar(index,CHAR_CDKEY));
    snprintf(s->name,sizeof(s->name),"%s",CHAR_getChar(index,CHAR_NAME));
    char account[128],name[256];market_hex(s->account,account);market_hex(s->name,name);
    snprintf(s->owner,sizeof(s->owner),"P:%s:%s",account,name);
    if (!market_db_ready()) { s->blocked=1;market_notice(s,"交易所数据库暂时不可用，请稍后重新登录。");return; }
    char op=0;int receipt=market_receipt(CHAR_getChar(index,CHAR_ACTIVE_ACT13),&op);
    if (receipt<0) { s->blocked=1;market_notice(s,"交易所保存字段已有其他数据，请联系管理员核对。");return; }
    if (receipt>0) { s->pending=receipt;s->operation=op;s->persisted=1;if (!market_finish(s)) { s->blocked=1;return; } }
    MarketRow *r=malloc(sizeof(*r));if (!r) { s->blocked=1;return; }
    int found=market_db_pending(s,r);
    if (found==1) {
        int count=stone_asset_uid_count(index,r->asset.kind,r->asset.uid),target=-1;
        if (r->state==MARKET_DEPOSIT && market_owned(s,r) && count==1) target=MARKET_CANCELLED;
        if (r->state==MARKET_BUY && market_actor(s,r,'B') && count==0) target=MARKET_AVAILABLE;
        if (r->state==MARKET_RETURN && market_owned(s,r) && count==0) target=MARKET_AVAILABLE;
        if (target<0 || market_db_finish(s,r,target)!=1) s->blocked=1;
    } else if (found<0) s->blocked=1;
    free(r);market_db_pay(s->account);market_gold(s->account);
    if (s->blocked) market_notice(s,"交易所与角色存档需要核对，资产和款项已保留。");
}
static int market_token(const char *source,int field,char *out,size_t size)
{ return getStringFromIndexWithDelim((char*)source,"|",field,out,(int)size); }
static int market_meta(MarketRow *r,int object)
{
    char t[32][512];memset(t,0,sizeof(t));
    if (r->asset.kind==STONE_ASSET_ITEM) {
        for (int i=1;i<=9;++i) if (!market_token(r->asset.detail,i,t[i],sizeof(t[i]))) return 0;
        int type=ITEM_getInt(object,ITEM_TYPE);
        r->category=(type==ITEM_OTHER || (type>=ITEM_DISH && type<=ITEM_WARES) || type==ITEM_TRAINBOOK) ? 3 : 2;
        r->quantity=ITEM_getInt(object,ITEM_USEPILENUMS);if (r->quantity<1) r->quantity=1;
        if (!market_token(r->asset.summary,2,t[10],sizeof(t[10])) || !market_number(t[10],0,&r->picture)) return 0;
        snprintf(r->name,sizeof(r->name),"%s",t[3]);
        /* 1123 itself parses inlay fields as integer tokens; retain the exact
         * native strings here and in the full asset blob, without conversion. */
        return market_append(r->detail,sizeof(r->detail),"I|%s|%s|%s|%d|%s|%d|%d|%s|%s|%s|",t[6],t[3],t[4],r->picture,t[5],
            ITEM_getInt(object,ITEM_NEEDTRANS),ITEM_getInt(object,ITEM_LEVEL),t[7],t[8],t[9]);
    }
    for (int i=1;i<=8;++i) if (!market_token(r->asset.summary,i,t[i],sizeof(t[i]))) return 0;
    char d[14][128];for (int i=1;i<=13;++i) if (!market_token(r->asset.detail,i,d[i],sizeof(d[i]))) return 0;
    int skills=0;if (!market_number(d[3],0,&r->picture) || !market_number(d[13],0,&skills) || skills>CHAR_MAXPETSKILLHAVE) return 0;
    r->category=1;r->quantity=1;snprintf(r->name,sizeof(r->name),"%s",t[1]);
    if (!market_append(r->detail,sizeof(r->detail),"X|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|",
        d[3],d[4],t[3],t[2],d[5],d[6],d[7],t[4],t[5],t[6],t[7],d[8],d[9],d[10],d[11],d[12],skills)) return 0;
    for (int i=0;i<skills+8;++i) {
        char value[512];if (!market_token(r->asset.detail,14+i,value,sizeof(value)) || !market_append(r->detail,sizeof(r->detail),"%s|",value)) return 0;
    }
    /* This native baseline has no passive-skill character fields. Four empty
     * target slots convey absence; all available skill and equipment data is
     * still preserved by the original serializer. */
    return market_append(r->detail,sizeof(r->detail),"%s|||||",r->name);
}
static int market_trade_allowed(MarketSession *s,int kind,int object);
#ifndef STONE_MARKET_TEST_RULES
static int market_trade_allowed(MarketSession *s,int kind,int object)
{
    extern MY_Lua MYLua;
    if (CHAR_getInt(s->index,CHAR_TRANSMIGRATION)<1 && CHAR_getInt(s->index,CHAR_LV)<50) {
        market_notice(s,"未转生角色达到 50 级后才可以寄售。");return 0;
    }
    /* The existing wrappers return numeric zero even after Lua errors. Invoke
     * the same loaded resource with strict error/type handling instead. */
    for (MY_Lua *entry=&MYLua;entry && entry->next;entry=entry->next) {
        if (!entry->luapath || strcmp(entry->luapath,"data/ablua/trade.lua") || !entry->lua) continue;
        lua_State *lua=entry->lua;int top=lua_gettop(lua),ok=0;
        lua_getglobal(lua,kind==STONE_ASSET_ITEM ? "FreeTradeItem" : "FreeTradePet");
        if (lua_isfunction(lua,-1)) {
            lua_pushinteger(lua,s->index);lua_pushinteger(lua,object);
            if (lua_pcall(lua,2,1,0)==LUA_OK && lua_isinteger(lua,-1)) ok=lua_tointeger(lua,-1)==0;
        }
        lua_settop(lua,top);return ok;
    }
    market_notice(s,"原生交易规则暂时不可用，请稍后重试。");return 0;
}
#endif
static void market_asset_status(MarketSession *s,int kind,int slot)
{
    if (kind==STONE_ASSET_ITEM) CHAR_sendItemDataOne(s->index,slot);
    else { char cat[12];snprintf(cat,sizeof(cat),"K%d",slot);CHAR_sendStatusString(s->index,cat);snprintf(cat,sizeof(cat),"W%d",slot);CHAR_sendStatusString(s->index,cat); }
}
static int market_save(MarketSession *s,MarketRow *r,char operation)
{
    char receipt[40];snprintf(receipt,sizeof(receipt),"market:%d:%c",r->id,operation);
    s->pending=r->id;s->operation=operation;s->persisted=0;s->required=s->sent+1;
    if (!CHAR_setChar(s->index,CHAR_ACTIVE_ACT13,receipt) || strcmp(CHAR_getChar(s->index,CHAR_ACTIVE_ACT13),receipt)) {
        /* The caller must undo its local move before this known refusal can
         * be rolled back in SQL. Never save a moved asset without a receipt. */
        s->blocked=1;return 0;
    }
    if (!CHAR_charSaveFromConnect(s->index,FALSE)) market_notice(s,"角色保存暂未发出，资产与凭据已保留，等待下一次保存。");
    else market_notice(s,"交易所操作正在保存，请稍候。");
    return 1;
}
static void market_deposit(MarketSession *s,int category,int unit,int duration,int pet,int item)
{
    int kind=pet>=0 ? STONE_ASSET_PET : STONE_ASSET_ITEM,slot=pet>=0 ? pet : item;
    if (category<1 || category>3 || unit<100 || duration<1 || duration>3 ||
        (pet>=0 ? pet>=CHAR_MAXPETHAVE || item!=0 : pet!=-1 || item<CHAR_EQUIPPLACENUM || item>=CHAR_MAXITEMHAVE)) return;
    int object=kind==STONE_ASSET_ITEM ? CHAR_getItemIndex(s->index,slot) : CHAR_getCharPet(s->index,slot);
    if (kind==STONE_ASSET_ITEM ? !ITEM_CHECKINDEX(object) : !CHAR_CHECKINDEX(object)) return;
    /* Native FAMILY_Add marks the pet as 1; 3 is the character leader flag. */
    if (kind==STONE_ASSET_PET && (CHAR_getInt(object,CHAR_PETFAMILY)==1 || CHAR_getInt(object,CHAR_MAILMODE)!=CHAR_PETMAIL_NONE || CHAR_getWorkInt(object,CHAR_WORKOBJINDEX)>=0)) {
        market_notice(s,"家族守护兽、邮件宠物或放出宠物不能寄售。");return;
    }
    if (!market_trade_allowed(s,kind,object)) return;
    int had_uid=kind==STONE_ASSET_ITEM ? *ITEM_getChar(object,ITEM_UNIQUECODE)!=0 : *CHAR_getChar(object,CHAR_UNIQUECODE)!=0;
    MarketRow *r=calloc(1,sizeof(*r));if (!r) return;
    if (!stone_asset_serialize(s->index,kind,slot,&r->asset) || r->asset.bound || !market_meta(r,object)) {
        market_notice(s,"此资产被绑定，或原生资料无法完整保存，不能寄售。");free(r);return;
    }
    if (!had_uid) {
        /* A newly generated UID must already exist in the persisted source
         * snapshot before escrow starts, so a crash before movement can prove
         * that the original asset is still present and refund the fee. */
        s->priming=1;s->required=s->sent+1;
        CHAR_charSaveFromConnect(s->index,FALSE);
        market_notice(s,"资产编号正在保存，请保存完成后再次寄售。");free(r);return;
    }
    /* Client category is a browsing choice. Classify the selected native
     * object server-side; a pet cannot be disguised as an equipment listing. */
    long long total=(long long)unit*r->quantity;
    static const int hours[]={24,48,168};
    long long expires=(long long)market_now()+hours[duration-1]*3600;
    if (total>INT_MAX || expires>INT_MAX || expires<=0) { market_notice(s,"整组总价超过原生金币上限，请调整单价。");free(r);return; }
    r->unit=unit;r->total=(int)total;r->fee=(int)((total+19)/20);r->duration=hours[duration-1];r->expires=(int)expires;
    int status=market_db_stage(s,r,'D');
    if (status!=1) {
        if (status<0) s->blocked=1;
        market_notice(s,status<0 ? "寄售结果尚未确认，资产仍在角色中，请重新连接核对。" : "金币不足以支付 5% 上架手续费，或该资产已有寄售记录。");free(r);return;
    }
    if (!stone_asset_remove(s->index,kind,slot,r->asset.uid)) {
        if (stone_asset_uid_count(s->index,kind,r->asset.uid)!=1 || market_db_finish(s,r,MARKET_CANCELLED)!=1) s->blocked=1;
        market_db_pay(s->account);market_gold(s->account);free(r);return;
    }
    if (!market_save(s,r,'D')) {
        if (stone_asset_restore(s->index,slot,&r->asset)<0) s->blocked=1;
        /* Keep the staged row: a reserved/failed receipt must be reconciled
         * after a fresh native login, never guessed from mutable memory. */
    }
    market_asset_status(s,kind,slot);market_gold(s->account);free(r);
}
static void market_take(MarketSession *s,MarketRow *r,int returning)
{
    if (r->state!=MARKET_AVAILABLE || (returning ? !market_owned(s,r) :
        !strcmp(s->account,r->asset.owner_account) || r->expires<=market_now())) {
        market_notice(s,"此记录已变化、已过期，或不允许此账号购买，请刷新列表。");return;
    }
    int slot=r->asset.kind==STONE_ASSET_ITEM ? CHAR_findEmptyItemBox(s->index) : CHAR_getCharPetElement(s->index);
    if (slot<0) { market_notice(s,r->asset.kind==STONE_ASSET_ITEM ? "道具栏没有空位，请先整理背包。" : "宠物栏没有空位，请先整理宠物。");return; }
    if (stone_asset_uid_count(s->index,r->asset.kind,r->asset.uid)!=0) { market_notice(s,"角色中已有相同资产编号，无法重复领取。");return; }
    char operation=returning ? 'W' : 'B';int status=market_db_stage(s,r,operation);
    if (status!=1) {
        if (status<0) s->blocked=1;
        market_notice(s,status<0 ? "交易结果尚未确认，请重新连接核对。" : "金币不足，或该记录已由其他玩家处理。");return;
    }
    if (stone_asset_restore(s->index,slot,&r->asset)<0) {
        if (stone_asset_uid_count(s->index,r->asset.kind,r->asset.uid)!=0 || market_db_finish(s,r,MARKET_AVAILABLE)!=1) s->blocked=1;
        market_db_pay(s->account);market_gold(s->account);market_notice(s,"原生资产暂时无法取出，记录已保留。");return;
    }
    if (!market_save(s,r,operation)) {
        if (!stone_asset_remove(s->index,r->asset.kind,slot,r->asset.uid)) s->blocked=1;
    }
    market_asset_status(s,r->asset.kind,slot);market_gold(s->account);
}
static int market_args(const char *data,char *operation,int out[5])
{
    if (!data || !data[0] || data[1]!='|' || strlen(data)>120) return -1;
    *operation=data[0];const char *p=data+2;int count=0;
    while (*p) {
        const char *end=strchr(p,'|');size_t n=end ? (size_t)(end-p) : strlen(p);
        if (!n || n>11 || count==5) return -1;
        char token[12];memcpy(token,p,n);token[n]=0;
        if (!market_number(token,1,&out[count++])) return -1;
        if (!end) break;
        p=end+1;
    }
    return count;
}
static int market_command(MarketSession *s,const char *data)
{
    char operation=0;int args[5],n=market_args(data,&operation,args);if (n<0) return 1;
    if (!market_eligible(s,0) || !market_db_ready()) return 1;
    if (s->priming) { market_notice(s,"资产编号仍在保存，请稍候再操作。");return 1; }
    if (s->pending) {
        if (!market_finish(s)) market_notice(s,"上一笔操作正在保存，请稍候。");
        return 1; /* A pending operation may rotate this very request's sequence. */
    }
    if ((operation=='L' || operation=='P') && n==(operation=='L'?1:2) && args[0]>=1 && args[0]<=3 && (n==1 || args[1]>=1)) {
        market_db_pay(s->account);market_gold(s->account);market_list(s,0,args[0],n==1?1:args[1],0);return 1;
    }
    if ((operation=='M' || operation=='A') && n==(operation=='M'?1:2) && args[0]>=1 && args[0]<=2 && (n==1 || args[1]>=1)) {
        market_db_pay(s->account);market_gold(s->account);market_list(s,1,args[0],n==1?1:args[1],0);return 1;
    }
    if (operation=='X' && n==3 && args[0]>=1 && args[0]<=3 && args[1]>0 && (args[2]==0 || args[2]==2 || args[2]==3)) {
        MarketRow *r=malloc(sizeof(*r));if (!r) return 1;
        if (market_db_find(args[1],r)==1 && (args[2]==0 ? r->state==MARKET_AVAILABLE && r->expires>market_now() && r->category==args[0] :
            market_owned(s,r) && (r->state==MARKET_AVAILABLE || r->state==MARKET_SOLD) && r->asset.kind==args[2]-1)) {
            char payload[8192]="";if (market_header(payload,sizeof(payload),"")) {
                /* header with empty kind ends in ||; detail starts with I/X. */
                payload[strlen(payload)-1]=0;if (market_append(payload,sizeof(payload),"%s",r->detail)) market_send(s,payload,0);
            }
        }
        free(r);return 1;
    }
    if (!market_eligible(s,1) || s->sent==UINT64_MAX) return 1;
    char oldop=0;if (market_receipt(CHAR_getChar(s->index,CHAR_ACTIVE_ACT13),&oldop)<0) { s->blocked=1;return 1; }
    if (operation=='U' && n==5) { market_deposit(s,args[0],args[1],args[2],args[3],args[4]);return 1; }
    if ((operation=='B' || operation=='G') && n==2 && args[1]>0 && args[0]>=(operation=='G'?2:1) && args[0]<=3) {
        MarketRow *r=malloc(sizeof(*r));if (!r) return 1;
        if (market_db_find(args[1],r)==1 && (operation=='G' ? r->asset.kind==args[0]-1 : r->category==args[0])) market_take(s,r,operation=='G');
        free(r);
    }
    return 1;
}
int stone_market_menu(int fd,int menu)
{
    if (menu!=130) return 0;
    stone_market_login(fd);MarketSession *s=market_session(fd);
    if (!s || !market_eligible(s,0) || !market_db_ready() || (s->pending && !market_finish(s))) return 1;
    market_db_pay(s->account);market_gold(s->account);
    if (market_sequence(s)) market_list(s,0,1,1,1);
    return 1;
}
int stone_market_window(int fd,int sequence,int object,int select,const char *data)
{
    if (((unsigned)sequence & ~MARKET_SEQUENCE_MASK)!=MARKET_SEQUENCE_BASE) return 0;
    MarketSession *s=market_session(fd);
    if (s && market_same(s) && s->sequence==sequence && object==-1 && select==0) market_command(s,data);
    return 1;
}
void stone_market_saving(int fd)
{
    MarketSession *s=market_session(fd);if (!s) return;
    if (s->sent==UINT64_MAX) { s->blocked=1;return; }++s->sent;
}
void stone_market_saved(int fd,const char *result)
{
    MarketSession *s=market_session(fd);if (!s || s->acknowledged==s->sent) return;
    uint64_t sequence=++s->acknowledged;
    if (s->priming && sequence>=s->required && result && !strcmp(result,SUCCESSFUL)) s->priming=0;
    if (!s->pending || s->blocked || sequence<s->required) return;
    if (result && !strcmp(result,SUCCESSFUL)) { s->persisted=1;if (market_db_ready()) market_finish(s); }
    else if (!s->persisted) market_notice(s,"角色保存失败，交易记录仍保留，等待下一次完整保存。");
}
void stone_market_logout(int fd)
{ MarketSession *s=market_session(fd);if (s) { s->closing=1;s->sequence=0; } }
int stone_market_busy(int fd)
{
    if (!CONNECT_checkfd(fd)) return 0;
    for (MarketSession *s=market_sessions;s;s=s->next)
        if (s->fd==fd && s->epoch==CONNECT_getFdid(fd)) return s->pending || s->priming || s->blocked || s->sent!=s->acknowledged;
    return 0;
}
