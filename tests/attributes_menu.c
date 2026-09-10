/* Execute the real adapter; only native APIs and SQL transport are fixtures.
 * No services or database connections. The SQL fixture models an atomic joined
 * debit/entitlement update, including lost responses and automatic retries. */
#include <assert.h>
#include "../native/app_attributes.c"
MYSQL mysql;
typedef struct {
    char name[192], field[192], disk[192];
    int stat[CHAR_DATAINTNUM], disk_stat[CHAR_DATAINTNUM];
} Role;
static Role roles[4];
static int active=2, epoch=1, connected=1, profile=4, logged=1, client=1;
static int trade_mode, battle_mode, dead, balance=40000, queries, debits;
static int db_down, update_down, response_lost, ledger_hidden, hide_after_update, bad_engine, duplicate_account;
static int saves, fail_save, fail_field, windows, updates, statuses, point_updates;
static int sequence, notices, random_counter=1, maxhp=1000, gold_updates, foreign_busy;
static time_t now=100;
static char payload[ATTR_PAYLOAD_SIZE], last_notice[512];
typedef struct { char nonce[33], name[385]; int owned, created, slot; } Ledger;
static Ledger ledger[16]; static int ledgers;
typedef struct { int role; char field[192]; int stat[CHAR_DATAINTNUM]; } Snapshot;
static Snapshot snapshots[128]; static int written, acknowledged;
static char result_value[128]; static char *result_row[1];
static int result_count; static unsigned long result_length[1];

static void capture_hex(const char *query, const char *prefix, char *out, size_t size)
{
    const char *p=strstr(query,prefix); assert(p); p+=strlen(prefix);
    const char *end=strchr(p,'\''); assert(end && (size_t)(end-p)<size);
    memcpy(out,p,end-p); out[end-p]=0;
}
static Ledger *find_ledger(const char *query)
{
    char nonce[33]; capture_hex(query,"p.nonce=X'",nonce,sizeof(nonce));
    for (int n=0;n<ledgers;++n) if (!strcmp(ledger[n].nonce,nonce)) return &ledger[n];
    return NULL;
}
static int ledger_matches(Ledger *row,const char *query)
{
    if (!row) return 0;
    char name[385]; capture_hex(query,"p.role_name=X'",name,sizeof(name));
    int created,slot;
    assert(sscanf(strstr(query,"p.created="),"p.created=%d AND p.slot=%d",&created,&slot)==2);
    return !strcmp(row->name,name) && row->created==created && row->slot==slot;
}
int sasql_mysql_query(char *q)
{
    ++queries; result_count=0;
    if (db_down) return 1;
    if (!strncmp(q,"CREATE TABLE IF NOT EXISTS " ATTR_TABLE,strlen("CREATE TABLE IF NOT EXISTS " ATTR_TABLE))) return 0;
    if (!strncmp(q,"SELECT ENGINE",13)) {
        snprintf(result_value,sizeof(result_value),"%s",bad_engine?"MyISAM":"InnoDB"); result_count=1; return 0;
    }
    if (!strncmp(q,"INSERT IGNORE INTO " ATTR_TABLE,strlen("INSERT IGNORE INTO " ATTR_TABLE))) {
        const char *p=strstr(q,"VALUES (X'"); assert(p);
        char account[129],nonce[33],name[385]; int created,slot;
        assert(sscanf(p,"VALUES (X'%128[^']',X'%32[^']',X'%384[^']',%d,%d,0)",account,nonce,name,&created,&slot)==5);
        for(int n=0;n<ledgers;++n) if(!strcmp(ledger[n].nonce,nonce)) return 0;
        assert(ledgers<16); Ledger *r=&ledger[ledgers++];
        snprintf(r->nonce,sizeof(r->nonce),"%s",nonce); snprintf(r->name,sizeof(r->name),"%s",name);
        r->created=created;r->slot=slot;r->owned=0;return 0;
    }
    if (!strncmp(q,"SELECT p.owned",14)) {
        if (ledger_hidden) return 1;
        Ledger *r=find_ledger(q);
        if(ledger_matches(r,q)) { snprintf(result_value,sizeof(result_value),"%d",r->owned);result_count=1; }
        return 0;
    }
    if (!strncmp(q,"SELECT COUNT(*)",15)) {
        snprintf(result_value,sizeof(result_value),"%d",duplicate_account?2:1);result_count=1;return 0;
    }
    if (!strncmp(q,"UPDATE `accounts` a JOIN " ATTR_TABLE,strlen("UPDATE `accounts` a JOIN " ATTR_TABLE))) {
        assert(strstr(q,"ON BINARY a.`Name`=p.cdkey"));
        int price,id,prior,minimum;
        assert(sscanf(strstr(q,"SET a.VipPoint="),"SET a.VipPoint=a.VipPoint-%d,p.owned=%d",&price,&id)==2);
        assert(sscanf(strstr(q,"AND p.owned="),"AND p.owned=%d AND a.VipPoint>=%d",&prior,&minimum)==2);
        assert(id>=1 && id<=5 && price==attr_prices[id-1] && minimum==price && prior==id-1);
        if (update_down) return 1;
        Ledger *r=find_ledger(q);
        if (ledger_matches(r,q) && r->owned==prior && balance>=price) {
            balance-=price;r->owned=id;++debits;
        }
        if(hide_after_update)ledger_hidden=1;
        if(response_lost) { /* The production helper can retry this same statement. */
            if(ledger_matches(r,q) && r->owned==prior && balance>=price) {balance-=price;r->owned=id;++debits;}
            return 1;
        }
        return 0;
    }
    fprintf(stderr,"Unexpected SQL: %s\n",q);assert(0);return 1;
}
MYSQL_RES *mysql_store_result(MYSQL *unused) { return (MYSQL_RES *)&result_count; }
my_ulonglong mysql_num_rows(MYSQL_RES *unused) { return result_count; }
unsigned int mysql_num_fields(MYSQL_RES *unused) { return 1; }
MYSQL_ROW mysql_fetch_row(MYSQL_RES *unused) { result_row[0]=result_value;return result_row; }
unsigned long *mysql_fetch_lengths(MYSQL_RES *unused) { result_length[0]=strlen(result_value);return result_length; }
void mysql_free_result(MYSQL_RES *unused) {}
const char *stone_attributes_account_table(void) { return "accounts"; }
const char *stone_attributes_account_key(void) { return "Name"; }
time_t time(time_t *out) { if(out)*out=now; return now; }
ssize_t getrandom(void *buf,size_t length,unsigned flags) {
    unsigned char *p=buf;for(size_t n=0;n<length;++n)p[n]=(unsigned char)(n+random_counter);++random_counter;return length;
}
int CONNECT_checkfd(int fd) { return fd==7 && connected; }
int CONNECT_getloginmark(int fd) { return profile; }
int CONNECT_getFdid(int fd) { return epoch; }
BOOL CONNECT_isCLI(int fd) { return client; }
BOOL CONNECT_isLOGIN(int fd) { return logged; }
int CONNECT_getCharaindex(int fd) { return active; }
void CONNECT_getCdkey(int fd,char *out,int size) { snprintf(out,size,"fixture"); }
void CONNECT_getCharname(int fd,char *out,int size) { snprintf(out,size,"%s",roles[active].name); }
BOOL _CHAR_CHECKINDEX(char *file,int line,int index) { return index>=2 && index<=3; }
int _CHAR_getInt(char *file,int line,int index,CHAR_DATAINT key) { return roles[index].stat[key]; }
int _CHAR_setInt(char *file,int line,int index,CHAR_DATAINT key,int value) {
    int old=roles[index].stat[key];roles[index].stat[key]=value;return old;
}
int _CHAR_getWorkInt(char *file,int line,int index,CHAR_WORKDATAINT key) {
    if(key==CHAR_WORKFD)return 7;
    if(key==CHAR_WORKTRADEMODE)return trade_mode;
    if(key==CHAR_WORKBATTLEMODE)return battle_mode;
    if(key==CHAR_WORKMAXHP)return maxhp;
    assert(0);return 0;
}
BOOL _CHAR_getFlg(char *file,int line,int index,CHAR_DATAFLG key) { assert(key==CHAR_ISDIE);return dead; }
char *_CHAR_getChar(char *file,int line,int index,CHAR_DATACHAR key) {
    if(key==CHAR_CDKEY)return "fixture";
    if(key==CHAR_NAME)return roles[index].name;
    assert(key==CHAR_ACTIVE_ACT14);return roles[index].field;
}
BOOL _CHAR_setChar(char *file,int line,int index,CHAR_DATACHAR key,const char *value) {
    assert(key==CHAR_ACTIVE_ACT14);
    if(fail_field)return FALSE;
    assert(strlen(value)<128);snprintf(roles[index].field,sizeof(roles[index].field),"%s",value);return TRUE;
}
BOOL CHAR_talkToCli(int index,int from,char *s,CHAR_COLOR color) {
    ++notices;snprintf(last_notice,sizeof(last_notice),"%s",s);return TRUE;
}
BOOL CHAR_charSaveFromConnect(int index,BOOL unlock) {
    ++saves;if(fail_save)return FALSE;
    stone_attributes_saving(7);
    Snapshot *snap=&snapshots[written++];assert(written<128);snap->role=index;
    snprintf(snap->field,sizeof(snap->field),"%s",roles[index].field);
    memcpy(snap->stat,roles[index].stat,sizeof(snap->stat));return TRUE;
}
static void ack(const char *result) {
    assert(acknowledged<written);Snapshot *snap=&snapshots[acknowledged++];
    if(!strcmp(result,SUCCESSFUL)) {
        snprintf(roles[snap->role].disk,sizeof(roles[snap->role].disk),"%s",snap->field);
        memcpy(roles[snap->role].disk_stat,snap->stat,sizeof(snap->stat));
    }
    stone_attributes_saved(7,result);
}
static void flush(void) { while(acknowledged<written)ack(SUCCESSFUL); }
void lssproto_WN_send(int fd,int type,int buttons,int seq,int object,char *data) {
    assert(fd==7 && type==1119 && buttons==0 && object==-1);++windows;sequence=seq;
    snprintf(payload,sizeof(payload),"%s",data);
}
void lssproto_WNUpdate_send(int fd,int type,int buttons,int seq,int object,char *data) {
    assert(fd==7 && type==1119 && buttons==0 && object==-1);++updates;sequence=seq;
    snprintf(payload,sizeof(payload),"%s",data);
}
BOOL _CHAR_sendStatusString(int index,char *category,char *file,int line) { assert(!strcmp(category,"P"));++statuses;return TRUE; }
BOOL CHAR_send_P_StatusString(int index,unsigned int flags) { assert(flags==CHAR_P_STRING_GOLD);return TRUE; }
BOOL CHAR_Skillupsend(int index) {++point_updates;return TRUE;}
int FreeDispatchMessage(int fd,int function) { assert(fd==7 && function==261);++gold_updates;return 1; }
int stone_mail_busy(int fd) {return foreign_busy==1;}
int stone_storage_busy(int fd) {return foreign_busy==2;}
int stone_market_busy(int fd) {return foreign_busy==3;}
int _CHAR_complianceParameter(int index,char *file,int line) {return 1;}
void CHAR_PartyUpdate(int index,int fields) {assert(fields==CHAR_N_STRING_MAXHP);}

static void init_role(int index,const char *name,int slot)
{
    memset(&roles[index],0,sizeof(Role));snprintf(roles[index].name,sizeof(roles[index].name),"%s",name);
    roles[index].stat[CHAR_WHICHTYPE]=CHAR_TYPEPLAYER;
    roles[index].stat[CHAR_CREATETIME]=1000+index;roles[index].stat[CHAR_SAVEINDEXNUMBER]=slot;
    roles[index].stat[CHAR_HP]=50;roles[index].stat[CHAR_VITAL]=1000;roles[index].stat[CHAR_STR]=1000;
    roles[index].stat[CHAR_EARTHAT]=100;
}
static void open_menu(void) {++now;assert(stone_attributes_menu(7,122)==1);}
static void request(const char *data) {assert(stone_attributes_window(7,sequence,-1,0,data)==1);}
static int budget(void) {
    int total=roles[active].stat[CHAR_SKILLUPPOINT]*100;
    for(int n=0;n<4;++n)total+=roles[active].stat[attr_fields[n]];return total;
}
static void reconnect(int role)
{
    active=role;++epoch;
    snprintf(roles[role].field,sizeof(roles[role].field),"%s",roles[role].disk);
    memcpy(roles[role].stat,roles[role].disk_stat,sizeof(roles[role].stat));
    written=acknowledged=0;open_menu();flush();
}
int main(void)
{
    init_role(2,"RoleA",0);init_role(3,"RoleB",1);
    assert(stone_attributes_menu(7,1)==0);
    profile=3;assert(stone_attributes_menu(7,122)==0);profile=4;
    /* An older autosave ACK cannot confirm the newly created random identity. */
    CHAR_charSaveFromConnect(2,FALSE);
    open_menu();assert(windows==1 && !strcmp(payload,"L|4000|4000|5000|8000|10000|0"));
    assert(stone_attributes_busy(7)==1 && stone_attributes_busy(8)==0);
    request("B|1|");assert(balance==40000 && debits==0); /* wait for durable random identity */
    ack(SUCCESSFUL);assert(stone_attributes_busy(7));
    int count=windows;assert(stone_attributes_menu(7,122)==1 && windows==count);flush();
    assert(strlen(roles[2].disk)==20 && ledgers==1);
    assert(stone_attributes_busy(7)==0);
    for(int busy=1;busy<=3;++busy) {
        foreign_busy=busy;request("B|1|");assert(debits==0 && balance==40000);
    }
    foreign_busy=0;
    request("B|2|");request("B|0|");request("B|6|");request("B|1|junk");request("B|01|");request("B|-1|");
    assert(balance==40000 && debits==0);
    stone_attributes_window(7,sequence+1,-1,0,"B|1|");stone_attributes_window(7,sequence,0,0,"B|1|");
    assert(debits==0);
    battle_mode=1;request("B|1|");battle_mode=0;trade_mode=1;request("B|1|");trade_mode=0;
    dead=1;request("B|1|");dead=0;assert(debits==0);
    bad_engine=1;request("B|1|");bad_engine=0;assert(debits==0);
    duplicate_account=1;request("B|1|");duplicate_account=0;assert(debits==0);
    balance=3999;request("B|1|");assert(balance==3999 && debits==0);
    balance=40000;update_down=1;request("B|1|");update_down=0;assert(balance==40000 && debits==0);
    response_lost=1;request("B|1|");response_lost=0;
    assert(balance==36000 && debits==1 && ledger[0].owned==1);flush();
    assert(gold_updates==1);
    request("B|1|");assert(balance==36000 && debits==1);
    request("S|1|");flush();
    assert(strstr(payload,"|1|1000|1000|0|0|100|0|0|0|2000"));
    request("C|1|");assert(point_updates==0); /* two purchased schemes required */
    request("B|2|");flush();assert(balance==32000 && debits==2);
    request("C|2|");assert(point_updates==0); /* bought but not saved */
    roles[2].stat[CHAR_VITAL]=0;roles[2].stat[CHAR_STR]=2000;
    request("S|2|");flush();assert(budget()==2000);
    for(int busy=1;busy<=3;++busy) {
        int prior_saves=saves,prior_points=point_updates;foreign_busy=busy;
        request("S|1|");request("C|1|");assert(saves==prior_saves && point_updates==prior_points);
    }
    foreign_busy=0;
    roles[2].stat[CHAR_SKILLUPPOINT]=3; /* level up after the snapshot */
    request("C|1|");flush();assert(budget()==2300 && roles[2].stat[CHAR_SKILLUPPOINT]==3 && roles[2].stat[CHAR_VITAL]==1000);
    roles[2].stat[CHAR_VITAL]+=200;roles[2].stat[CHAR_SKILLUPPOINT]-=2; /* ordinary allocation */
    request("C|2|");flush();assert(budget()==2300 && roles[2].stat[CHAR_SKILLUPPOINT]==3 && roles[2].stat[CHAR_VITAL]==0);
    request("C|1|");flush();assert(budget()==2300 && roles[2].stat[CHAR_SKILLUPPOINT]==3);
    /* Revoked/transmigration budget cannot load a previously larger allocation. */
    roles[2].stat[CHAR_STR]=0;roles[2].stat[CHAR_SKILLUPPOINT]=0;
    int changed=point_updates;request("C|2|");assert(point_updates==changed && budget()==1000);
    roles[2].stat[CHAR_VITAL]=1001;roles[2].stat[CHAR_STR]=1000;
    request("C|2|");assert(point_updates==changed && budget()==2001); /* no rounding of raw remainders */
    roles[2].stat[CHAR_VITAL]=1000;roles[2].stat[CHAR_STR]=1000;
    roles[2].stat[CHAR_VITAL]=262144;request("S|1|");assert(point_updates==changed);
    roles[2].stat[CHAR_VITAL]=1000;
    /* A snapshot may become incompatible after changing profession. */
    roles[2].stat[CHAR_VITAL]=0;roles[2].stat[CHAR_STR]=20100;
    request("S|2|");flush();roles[2].stat[PROFESSION_CLASS]=PROFESSION_CLASS_WIZARD;
    int before_class=point_updates;request("C|2|");assert(point_updates==before_class);
    roles[2].stat[PROFESSION_CLASS]=PROFESSION_CLASS_NONE;
    roles[2].stat[CHAR_VITAL]=0;roles[2].stat[CHAR_STR]=2000;request("S|2|");flush();
    roles[2].stat[CHAR_VITAL]=1000;roles[2].stat[CHAR_STR]=1000;
    /* Five exact prices, maximum storage size, canonical decoding. */
    request("B|3|");flush();request("B|4|");flush();request("B|5|");flush();
    assert(balance==9000 && debits==5 && ledger[0].owned==5 && strlen(roles[2].disk)==125);
    const char *directory=getenv("STONE_CHECK_OUTPUT_DIR");if(!directory || !*directory) directory="evidence";
    char fixture_path[4096];int path_length=snprintf(fixture_path,sizeof(fixture_path),"%s/attributes-window-fixture.txt",directory);
    assert(path_length>0 && (size_t)path_length<sizeof(fixture_path));
    FILE *fixture=fopen(fixture_path,"wb");assert(fixture);
    assert(fprintf(fixture,"%s\n",payload)>0);assert(fclose(fixture)==0);
    AttrState state;assert(attr_decode(roles[2].disk,&state) && state.count==5);
    char encoded[128];assert(attr_encode(&state,encoded) && !strcmp(encoded,roles[2].disk));
    encoded[strlen(encoded)-1]='B';assert(!attr_decode(encoded,&state)); /* noncanonical pad bits */
    assert(sizeof(STRING128)>=128);
    for(int n=0;n<5;++n) {
        AttrState edge={.count=5};snprintf(edge.nonce,sizeof(edge.nonce),"ABCDEFGHIJKLMNOP");
        for(int k=0;k<5;++k) {
            for(int a=0;a<4;++a)edge.plan[k].value[a]=262143;
            edge.plan[k].value[4+n%4]=100;edge.plan[k].value[8]=4*262143+100;
        }
        AttrState decoded;assert(attr_encode(&edge,encoded) && strlen(encoded)==125 && attr_decode(encoded,&decoded));
        assert(!memcmp(&edge,&decoded,sizeof(edge)));
    }
    reconnect(2);assert(balance==9000 && roles[2].stat[CHAR_VITAL]==1000);
    request("C|2|");flush();assert(budget()==2000 && roles[2].stat[CHAR_VITAL]==0);
    /* Another native role in the same account gets its own saved nonce and ledger. */
    int old_sequence=sequence;stone_attributes_logout(7);active=3;open_menu();flush();
    assert(ledgers==2 && ledger[1].owned==0 && strstr(payload,"10000|0"));
    stone_attributes_window(7,old_sequence,-1,0,"C|2|");assert(roles[3].stat[CHAR_VITAL]==1000);
    request("B|1|");assert(balance==5000 && ledger[1].owned==1);
    /* Simulated crash AFTER DB commit but BEFORE its new CAA14 save: reconstruct
     * paid entitlement from the previously saved random identity, no second debit. */
    written=acknowledged=0;reconnect(3);assert(balance==5000 && ledger[1].owned==1);
    assert(attr_decode(roles[3].field,&state) && state.count==1);request("B|1|");assert(balance==5000);
    /* Lost query result leaves recovery retryable and does not fabricate a purchase. */
    db_down=1;request("B|2|");db_down=0;assert(balance==5000);
    /* Death and HP cannot be healed by toggling a scheme. */
    active=2;stone_attributes_logout(7);open_menu();flush();maxhp=20;roles[2].stat[CHAR_HP]=50;
    request("C|1|");flush();assert(roles[2].stat[CHAR_HP]==20 && budget()==2000);
    /* Failed SAAC save remains busy. Reopening requests a new native snapshot;
     * neither elapsed time nor a failed reply is confirmation. */
    request("S|1|");assert(stone_attributes_busy(7));ack("failed");assert(stone_attributes_busy(7));
    open_menu();assert(stone_attributes_busy(7));flush();assert(!stone_attributes_busy(7));
    fail_save=1;request("S|1|");assert(stone_attributes_busy(7));fail_save=0;
    foreign_busy=2;int before_retry=saves;open_menu();assert(saves==before_retry);foreign_busy=0;
    open_menu();flush();assert(!stone_attributes_busy(7));
    /* A committed purchase whose read-back is unavailable blocks Q switching.
     * Reopening reconciles the paid entitlement, with no second debit. */
    active=3;stone_attributes_logout(7);open_menu();flush();
    hide_after_update=1;request("B|2|");assert(balance==1000 && stone_attributes_busy(7));
    hide_after_update=ledger_hidden=0;open_menu();flush();assert(balance==1000 && !stone_attributes_busy(7));
    assert(ledger[1].owned==2);
    /* A newly created role with the same name/slot/time still receives a fresh
     * random identity. Existing paid ownership must not leak into the new role. */
    stone_attributes_logout(7);init_role(3,"RoleB",1);
    foreign_busy=1;open_menu();assert(!*roles[3].field);foreign_busy=0;open_menu();flush();
    assert(ledgers==3 && ledger[2].owned==0 && strstr(payload,"10000|0"));
    /* Unknown pre-existing CAA14 data is preserved rather than overwritten. */
    active=3;stone_attributes_logout(7);snprintf(roles[3].field,sizeof(roles[3].field),"existing:other");
    open_menu();request("B|2|");assert(!strcmp(roles[3].field,"existing:other") && balance==1000);
    printf("attributes_menu: protocol, prices, strict requests, durable identity, idempotent debit, recovery, budget and role isolation passed\n");
    return 0;
}
