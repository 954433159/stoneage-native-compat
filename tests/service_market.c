/* Execute the real market state machine and the shared native asset codec.
 * The storage fixture supplies isolated native objects, codecs and snapshots;
 * market SQL is replaced only at its durable transaction boundary. */
#define main storage_fixture_main
#define CHAR_charSaveFromConnect storage_fixture_charSaveFromConnect
#define _CHAR_setChar storage_fixture_setChar
#define stone_market_busy storage_fixture_market_busy
#include "app_storage.c"
#undef CHAR_charSaveFromConnect
#undef _CHAR_setChar
#undef stone_market_busy
#undef main
#ifndef STONE_MARKET_REAL_DB
#define STONE_MARKET_TEST_DB
#else
static int market_fixture_query(MYSQL *connection,const char *sql);
#define mysql_query market_fixture_query
#endif
#define STONE_MARKET_TEST_RULES
BOOL CHAR_charSaveFromConnect(int index,int unlock);
BOOL _CHAR_setChar(char *file,int line,int index,CHAR_DATACHAR field,const char *value);
#include "../native/app_market.c"
#ifdef STONE_MARKET_REAL_DB
#undef mysql_query
#endif

static int rule_refusal,market_receipt_failure;
static int market_trade_allowed(MarketSession *s,int kind,int object) {
    return !rule_refusal && (CHAR_getInt(s->index,CHAR_TRANSMIGRATION)>0 || CHAR_getInt(s->index,CHAR_LV)>=50);
}
char *MD5String(char *input) { return "0123456789abcdef0123456789abcdef"; }
BOOL _CHAR_setChar(char *file,int line,int index,CHAR_DATACHAR field,const char *value) {
    if(field==CHAR_ACTIVE_ACT13 && market_receipt_failure) return FALSE;
    return storage_fixture_setChar(file,line,index,field,value);
}
BOOL CHAR_charSaveFromConnect(int index,int unlock) {
    if(fail_save) return FALSE;assert(sent_count<32);int fd=index+5;
    copy_snapshot(&snapshots[sent_count++],fd);stone_storage_saving(fd);stone_market_saving(fd);return TRUE;
}
static void m_ack(const char *status) {
    assert(ack_count<sent_count);Snapshot *s=&snapshots[ack_count++];
    if(!strcmp(status,SUCCESSFUL)) disk[conn(s->fd)]=*s;
    if(epochs[conn(s->fd)]==s->epoch) { stone_storage_saved(s->fd,status);stone_market_saved(s->fd,status); }
}
static void m_request(int fd,const char *data) {
    MarketSession *s=market_session(fd);assert(s && s->sequence);
    assert(stone_market_window(fd,s->sequence,-1,0,data)==1);
}
static void m_relog(int fd) {
    Snapshot *s=&disk[conn(fd)];memcpy(characters,s->chars,sizeof(characters));memcpy(objects,s->items,sizeof(objects));memcpy(object_used,s->used,sizeof(object_used));
    ++epochs[conn(fd)];stone_market_login(fd);
}
static void m_reset(void) {
    while(market_sessions) { MarketSession *s=market_sessions;market_sessions=s->next;free(s); }
    reset();rule_refusal=market_receipt_failure=0;
    characters[2].data[CHAR_LV]=characters[3].data[CHAR_LV]=80;
}
static void capture(const char *name,int fd) {
    const char *directory=getenv("STONE_CHECK_OUTPUT_DIR");if(!directory || !*directory) directory="evidence";
    char path[4096];int n=snprintf(path,sizeof(path),"%s/services-market-%s.txt",directory,name);
    assert(n>0 && (size_t)n<sizeof(path));
    FILE *out=fopen(path,"wb");assert(out);fputs(last_payload[conn(fd)],out);fclose(out);
}

#ifndef STONE_MARKET_REAL_DB
static MarketRow market_rows[128];
static int market_count,market_stage_down,market_finish_down,market_ambiguous_stage,market_ambiguous_finish;
static long long market_income[2];
static time_t fixture_now=1789000000;
static int actor_number(const char *account) {
    for(int i=0;i<2;++i) if(!strcmp(account,CHAR_getChar(i+2,CHAR_CDKEY))) return i;
    assert(0);return -1;
}
static time_t market_now(void) { return fixture_now; }
static int market_db_ready(void) { return !db_down; }
static int market_db_find(int id,MarketRow *r) {
    if(db_down) return -1;if(id<1 || id>market_count) return 0;*r=market_rows[id];return 1;
}
static int market_db_pending(MarketSession *s,MarketRow *r) {
    if(db_down) return -1;
    for(int i=1;i<=market_count;++i) {
        MarketRow *a=&market_rows[i];
        if((a->state==MARKET_DEPOSIT || a->state==MARKET_RETURN) && market_owned(s,a)) { *r=*a;return 1; }
        if(a->state==MARKET_BUY && market_actor(s,a,'B')) { *r=*a;return 1; }
    }
    return 0;
}
static int market_db_list(MarketSession *s,int mine,int category,int page,MarketRow *out,int *count,int *pages) {
    if(db_down) return -1;int selected[128],total=0;
    for(int i=market_count;i>0;--i) {
        MarketRow *r=&market_rows[i];
        if(mine ? market_owned(s,r) && r->asset.kind==category && (r->state==MARKET_AVAILABLE || r->state==MARKET_SOLD) :
            r->category==category && r->state==MARKET_AVAILABLE && r->expires>fixture_now) selected[total++]=i;
    }
    *pages=(total+MARKET_PAGE-1)/MARKET_PAGE;if(*pages<1) *pages=1;if(page>*pages) page=*pages;
    *count=0;for(int i=(page-1)*MARKET_PAGE;i<total && *count<MARKET_PAGE;++i) out[(*count)++]=market_rows[selected[i]];
    return 1;
}
static int market_db_stage(MarketSession *s,MarketRow *r,char op) {
    if(db_down || market_stage_down) return -1;MarketRow pending;
    if(market_db_pending(s,&pending)==1) return 0;
    int *balance=&gold[conn(s->fd)];
    if(op=='D') {
        for(int i=1;i<=market_count;++i) if(market_rows[i].state<=MARKET_RETURN && market_rows[i].asset.kind==r->asset.kind && !strcmp(market_rows[i].asset.uid,r->asset.uid)) return 0;
        if(*balance<r->fee) return 0;
        *balance-=r->fee;r->id=++market_count;r->state=MARKET_DEPOSIT;market_rows[r->id]=*r;
    } else {
        MarketRow *stored=&market_rows[r->id];
        if(stored->state!=MARKET_AVAILABLE || (op=='B' ? *balance<stored->total || stored->expires<=fixture_now || !strcmp(s->account,stored->asset.owner_account) : !market_owned(s,stored))) return 0;
        if(op=='B') { *balance-=stored->total;strcpy(stored->buyer,s->account);strcpy(stored->buyname,s->name); }
        stored->state=op=='B' ? MARKET_BUY : MARKET_RETURN;*r=*stored;
    }
    return market_ambiguous_stage ? -1 : 1;
}
static int market_db_finish(MarketSession *s,MarketRow *r,int target) {
    if(db_down || market_finish_down) return -1;MarketRow *stored=&market_rows[r->id];
    char op=stored->state==MARKET_DEPOSIT?'D':stored->state==MARKET_BUY?'B':stored->state==MARKET_RETURN?'W':0;
    if(!op || !market_actor(s,stored,op) || stored->state!=r->state) return 0;
    assert(op=='D' ? target==MARKET_AVAILABLE || target==MARKET_CANCELLED :
        op=='B' ? target==MARKET_SOLD || target==MARKET_AVAILABLE : target==MARKET_RETURNED || target==MARKET_AVAILABLE);
    if(target==MARKET_SOLD) market_income[actor_number(stored->asset.owner_account)]+=stored->total;
    if(target==MARKET_CANCELLED) market_income[actor_number(stored->asset.owner_account)]+=stored->fee;
    if(op=='B' && target==MARKET_AVAILABLE) market_income[actor_number(stored->buyer)]+=stored->total;
    stored->state=target;r->state=target;
    if(op=='B' && target==MARKET_AVAILABLE) stored->buyer[0]=stored->buyname[0]=0;
    return market_ambiguous_finish ? -1 : 1;
}
static int market_db_pay(const char *account) {
    if(db_down) return -1;int a=actor_number(account);
    long long pay=market_income[a]<INT_MAX-gold[a] ? market_income[a] : INT_MAX-gold[a];
    gold[a]+=(int)pay;market_income[a]-=pay;return 1;
}
static void fresh(void) {
    m_reset();memset(market_rows,0,sizeof(market_rows));memset(market_income,0,sizeof(market_income));
    market_count=market_stage_down=market_finish_down=market_ambiguous_stage=market_ambiguous_finish=0;
    fixture_now=1789000000;
}
static void test_roundtrip_purchase(void) {
    fresh();int object=give_item(7,9,"market-item-purchase");copy_snapshot(&disk[0],7);
    assert(stone_market_menu(7,1)==0 && stone_market_menu(7,130)==1);
    assert(last_window[0]==1123 && strstr(last_payload[0],"|L|3|1|宠物|2|装备|3|物品|1|1|1|0|"));
    capture("empty",7);int original=market_session(7)->sequence;
    m_request(7,"U|1|101|1|-1|9|");
    assert(market_count==1 && market_rows[1].state==MARKET_DEPOSIT && CHAR_getItemIndex(2,9)==-1);
    assert(market_rows[1].category==2 && market_rows[1].quantity==3 && market_rows[1].total==303 && market_rows[1].fee==16);
    assert(market_rows[1].duration==24 && market_rows[1].expires==fixture_now+24*3600 && gold[0]==984);
    assert(!strcmp(CHAR_getChar(2,CHAR_ACTIVE_ACT13),"market:1:D"));
    m_request(7,"U|1|101|1|-1|9|");assert(market_count==1);
    m_ack(SUCCESSFUL);assert(market_rows[1].state==MARKET_AVAILABLE && !stone_market_busy(7));
    assert(market_session(7)->sequence!=original);
    object=give_item(7,9,"replacement-never-replayed");
    stone_market_window(7,original,-1,0,"U|1|101|1|-1|9|");assert(market_count==1 && CHAR_getItemIndex(2,9)==object);
    m_request(7,"L|2|");capture("list",7);m_request(7,"X|2|1|0|");capture("item-detail",7);
    assert(strstr(last_payload[0],"|I|1|原生'测试斧|耐久度：44%|20033|攻 +37|"));
    m_request(7,"B|2|1|");assert(market_rows[1].state==MARKET_AVAILABLE && gold[0]==984);
    stone_market_menu(8,130);m_request(8,"L|2|");gold[1]=302;m_request(8,"B|2|1|");assert(market_rows[1].state==MARKET_AVAILABLE);
    gold[1]=303;m_request(8,"B|2|1|");assert(market_rows[1].state==MARKET_BUY && gold[1]==0 && gold[0]==984);
    object=CHAR_getItemIndex(3,9);assert(object>0 && ITEM_getInt(object,ITEM_MODIFYATTACK)==37 && ITEM_getInt(object,ITEM_USEPILENUMS)==3);
    m_request(8,"B|2|1|");assert(stone_asset_uid_count(3,2,"market-item-purchase")==1);
    m_ack(SUCCESSFUL);assert(market_rows[1].state==MARKET_SOLD && gold[0]==1287 && gold[1]==0 && market_income[0]==0);
    m_request(8,"B|2|1|");assert(gold[0]==1287 && stone_asset_uid_count(3,2,"market-item-purchase")==1);
    m_request(7,"M|2|");capture("mine-sold",7);assert(strstr(last_payload[0],"|M|2|1|1|1|"));
    m_request(7,"G|3|1|");assert(market_rows[1].state==MARKET_SOLD);
}
static void test_pets_returns_expiry(void) {
    fresh();int pet=give_pet(7,0,"market-pet-return",30);int gear=give_item(7,9,"pet-gear");
    CHAR_setItemIndex(2,9,-1);CHAR_setItemIndex(pet,0,gear);copy_snapshot(&disk[0],7);
    stone_market_menu(7,130);m_request(7,"U|3|100|3|0|0|");m_ack(SUCCESSFUL);
    assert(market_rows[1].category==1 && market_rows[1].duration==168 && gold[0]==995);
    /* Target 1123 uses StockDispBuffer for both lists. The external client
     * shows common graphic 61159 for every pet, not the animation number. */
    m_request(7,"L|1|");capture("pet-list",7);
    assert(strstr(last_payload[0],"|61159|100|1|1|"));
    m_request(7,"M|1|");capture("pet-mine",7);
    assert(strstr(last_payload[0],"|61159|100|1|1|"));
    m_request(7,"X|1|1|0|");capture("pet-detail",7);
    assert(strstr(last_payload[0],"|X|100001|0|590|30|98|80|50|80|20|0|0|1|200|40|30|20|2|攻击|防御|"));
    stone_market_menu(8,130);fixture_now=market_rows[1].expires;m_request(8,"B|1|1|");assert(market_rows[1].state==MARKET_AVAILABLE && gold[1]==1000);
    m_request(7,"M|1|");capture("mine-expired",7);m_request(7,"G|2|1|");assert(market_rows[1].state==MARKET_RETURN);
    pet=CHAR_getCharPet(2,0);assert(pet>=5 && CHAR_getItemIndex(pet,0)>0);m_ack(SUCCESSFUL);
    assert(market_rows[1].state==MARKET_RETURNED && gold[0]==995);
    m_request(7,"U|1|100|2|0|0|");m_ack(SUCCESSFUL);assert(market_count==2 && market_rows[2].duration==48);
    m_request(8,"B|1|2|");m_ack(SUCCESSFUL);pet=CHAR_getCharPet(3,0);
    assert(!strcmp(CHAR_getChar(pet,CHAR_OWNERCDKEY),"fixture-b") && !strcmp(CHAR_getChar(pet,CHAR_UNIQUECODE),"market-pet-return"));
    assert(CHAR_getItemIndex(pet,0)>0 && ITEM_getInt(CHAR_getItemIndex(pet,0),ITEM_MODIFYATTACK)==37);
}
static void test_failures_recovery(void) {
    fresh();give_item(7,9,"market-order");copy_snapshot(&disk[0],7);stone_market_menu(7,130);
    /* Earlier ACKs cannot publish, even if the initial save was already sent. */
    stone_market_saving(7);m_request(7,"U|2|100|1|-1|9|");
    stone_market_saved(7,SUCCESSFUL);assert(market_rows[1].state==MARKET_DEPOSIT);
    m_ack("failed");assert(market_rows[1].state==MARKET_DEPOSIT);
    CHAR_charSaveFromConnect(2,FALSE);m_ack(SUCCESSFUL);assert(market_rows[1].state==MARKET_AVAILABLE);
    stone_market_menu(8,130);copy_snapshot(&disk[1],8);market_ambiguous_stage=1;m_request(8,"B|2|1|");
    assert(market_rows[1].state==MARKET_BUY && gold[1]==700 && CHAR_getItemIndex(3,9)<0 && stone_market_busy(8));
    market_ambiguous_stage=0;m_relog(8);assert(market_rows[1].state==MARKET_AVAILABLE && gold[1]==1000 && !stone_market_busy(8));
    stone_market_menu(8,130);fail_allocate=1;m_request(8,"B|2|1|");assert(market_rows[1].state==MARKET_AVAILABLE && gold[1]==1000);fail_allocate=0;
    market_finish_down=1;m_request(8,"B|2|1|");m_ack(SUCCESSFUL);assert(market_rows[1].state==MARKET_BUY && stone_market_busy(8));
    market_finish_down=0;m_relog(8);assert(market_rows[1].state==MARKET_SOLD && gold[0]==1285 && !stone_market_busy(8));
    m_relog(8);assert(gold[0]==1285);
    fresh();give_item(7,9,"market-stage-crash");copy_snapshot(&disk[0],7);stone_market_menu(7,130);
    market_ambiguous_stage=1;m_request(7,"U|2|100|1|-1|9|");assert(gold[0]==985 && CHAR_getItemIndex(2,9)>0);
    market_ambiguous_stage=0;m_relog(7);assert(market_rows[1].state==MARKET_CANCELLED && gold[0]==1000);
    fresh();give_item(7,9,"market-receipt");copy_snapshot(&disk[0],7);stone_market_menu(7,130);market_receipt_failure=1;
    m_request(7,"U|2|100|1|-1|9|");assert(CHAR_getItemIndex(2,9)>0 && !sent_count && stone_market_busy(7));
    market_receipt_failure=0;m_relog(7);assert(market_rows[1].state==MARKET_CANCELLED && gold[0]==1000);
    fresh();give_item(7,9,"market-commit-ack");copy_snapshot(&disk[0],7);stone_market_menu(7,130);m_request(7,"U|2|100|1|-1|9|");
    market_ambiguous_finish=1;m_ack(SUCCESSFUL);assert(market_rows[1].state==MARKET_AVAILABLE && stone_market_busy(7));
    market_ambiguous_finish=0;m_request(7,"U|2|100|1|-1|9|");assert(!stone_market_busy(7) && market_count==1);
}
static void test_native_guardian_rejected(void) {
    fresh();int pet=give_pet(7,0,"native-guardian",30);
    CHAR_setInt(pet,CHAR_PETFAMILY,1); /* Actual FAMILY_Add / native trade.c value. */
    copy_snapshot(&disk[0],7);stone_market_menu(7,130);
    m_request(7,"U|1|100|1|0|0|");
    assert(!market_count && gold[0]==1000 && CHAR_getCharPet(2,0)==pet);
    assert(CHAR_getInt(pet,CHAR_PETFAMILY)==1 && !stone_market_busy(7));
}
static void test_rejections_capacity_paging_income(void) {
    fresh();int item=give_item(7,9,"market-validation");stone_market_menu(7,130);
    const char *invalid[]={"U|2|100|0|-1|9|","U|2|99|1|-1|9|","U|2|9999999999|1|-1|9|","U|2|2147483647|1|-1|9|",
        "U|2|100|1|0|9|","U|2|100|1|-1|8|","U|2|100|1|-1|54|","U|2|100|1|-1|9||","U|2|100;DROP|1|-1|9|","B|2|-1|"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) m_request(7,invalid[i]);assert(!market_count && gold[0]==1000);
    other_mail_busy=1;m_request(7,"U|2|100|1|-1|9|");assert(!market_count);other_mail_busy=0;
    other_attributes_busy=1;m_request(7,"U|2|100|1|-1|9|");assert(!market_count);other_attributes_busy=0;
    rule_refusal=1;m_request(7,"U|2|100|1|-1|9|");assert(!market_count);rule_refusal=0;
    objects[item].data[ITEM_LOCKED]=1;m_request(7,"U|2|100|1|-1|9|");assert(!market_count);objects[item].data[ITEM_LOCKED]=0;
    strcpy(CHAR_getChar(2,CHAR_ACTIVE_ACT13),"foreign-data");m_request(7,"U|2|100|1|-1|9|");assert(!market_count && !strcmp(CHAR_getChar(2,CHAR_ACTIVE_ACT13),"foreign-data"));
    fresh();give_item(7,9,"market-capacity");stone_market_menu(7,130);m_request(7,"U|2|100|1|-1|9|");m_ack(SUCCESSFUL);stone_market_menu(8,130);
    for(int i=CHAR_STARTITEMARRAY;i<CHAR_MAXITEMHAVE;++i) { char uid[40];snprintf(uid,sizeof(uid),"full-%d",i);give_item(8,i,uid); }
    m_request(8,"B|2|1|");assert(market_rows[1].state==MARKET_AVAILABLE && gold[1]==1000);
    int freeitem=CHAR_getItemIndex(3,9);CHAR_setItemIndex(3,9,-1);ITEM_endExistItemsOne(freeitem);
    gold[0]=INT_MAX-10;m_request(8,"B|2|1|");m_ack(SUCCESSFUL);assert(gold[0]==INT_MAX && market_income[0]==290);
    gold[0]-=100;m_request(7,"M|2|");assert(gold[0]==INT_MAX && market_income[0]==190);
    /* Seed multiple already-published journal records, exercise real renderer. */
    for(int i=2;i<=13;++i) { market_rows[i]=market_rows[1];market_rows[i].id=i;market_rows[i].state=MARKET_AVAILABLE; }
    market_count=13;m_request(8,"P|2|1|");capture("page-one",8);
    m_request(8,"P|2|2|");assert(strstr(last_payload[1],"|2|2|2|4|"));capture("page-two",8);
    fresh();give_item(7,9,"");stone_market_menu(7,130);fail_save=1;m_request(7,"U|2|100|1|-1|9|");
    assert(!market_count && !sent_count && stone_market_busy(7));m_request(7,"U|2|100|1|-1|9|");assert(!market_count);
    fail_save=0;CHAR_charSaveFromConnect(2,FALSE);m_ack("failed");assert(stone_market_busy(7) && !market_count);
    CHAR_charSaveFromConnect(2,FALSE);m_ack(SUCCESSFUL);m_request(7,"U|2|100|1|-1|9|");assert(market_count==1);m_ack(SUCCESSFUL);
    stone_market_logout(7);assert(!market_session(7)->sequence);
}
int main(void) {
    test_roundtrip_purchase();test_pets_returns_expiry();test_failures_recovery();test_native_guardian_rejected();test_rejections_capacity_paging_income();
    fresh();puts("PASS market: original 1123 payloads, native item/pet/gear transfer, FIFO ACK, escrow/recovery, replay/rules/capacity/expiry/income");return 0;
}
#else
/* Actual SQL, native asset codec and save state machine. This executable
 * refuses any non-empty DB or a DB outside the dedicated test-name prefix.
 * It leaves all test evidence in that isolated database and never drops it. */
MYSQL mysql;
int sasql_mysql_query(char *sql) { return mysql_query(&mysql,sql); }
static int lost_commit_reply,fail_debit_query;
static int market_fixture_query(MYSQL *connection,const char *sql) {
    if(fail_debit_query && !strncmp(sql,"UPDATE csalogin SET VipPoint=VipPoint-",38)) { fail_debit_query=0;return 1; }
    int result=mysql_query(connection,sql);
    if(!result && lost_commit_reply && !strcmp(sql,"COMMIT")) { lost_commit_reply=0;return 1; }
    if(result) fprintf(stderr,"isolated market SQL error: %s\n",mysql_error(connection));
    return result;
}
static long long db_scalar(const char *sql) { long long value=-1;assert(market_scalar64(sql,&value)==1);return value; }
static int db_balance(int actor) {
    char key[128],sql[300];market_hex(CHAR_getChar(actor+2,CHAR_CDKEY),key);
    snprintf(sql,sizeof(sql),"SELECT VipPoint FROM csalogin WHERE BINARY Name=X'%s'",key);return (int)db_scalar(sql);
}
static void db_set_balance(int actor,int amount) {
    char key[128],sql[300];market_hex(CHAR_getChar(actor+2,CHAR_CDKEY),key);
    snprintf(sql,sizeof(sql),"UPDATE csalogin SET VipPoint=%d WHERE BINARY Name=X'%s'",amount,key);assert(market_query(sql));
}
static int db_state(int id) { MarketRow *row=malloc(sizeof(*row));assert(row && market_db_find(id,row)==1);int state=row->state;free(row);return state; }
static int latest_id(void) { return (int)db_scalar("SELECT MAX(id) FROM local_market_entries"); }
static void db_fixture_accounts(void) {
    assert(market_query("CREATE TABLE csalogin (Name VARBINARY(64) PRIMARY KEY,VipPoint INT NOT NULL) ENGINE=InnoDB"));
    assert(market_query("CREATE TABLE vippointlog (id INT AUTO_INCREMENT PRIMARY KEY,cdkey VARBINARY(64),point INT,oldpoint INT,newpoint INT,buff VARCHAR(255),time DATETIME) ENGINE=InnoDB"));
    strcpy(CHAR_getChar(2,CHAR_CDKEY),"fixture'a");strcpy(CHAR_getChar(3,CHAR_CDKEY),"fixture\\b");
    strcpy(CHAR_getChar(2,CHAR_NAME),"角色'甲\\");strcpy(CHAR_getChar(3,CHAR_NAME),"角色\\乙'");
    for(int i=0;i<2;++i) {
        char key[128],sql[300];market_hex(CHAR_getChar(i+2,CHAR_CDKEY),key);
        snprintf(sql,sizeof(sql),"INSERT INTO csalogin(Name,VipPoint) VALUES(X'%s',1000)",key);assert(market_query(sql));
        copy_snapshot(&disk[i],i+7);
    }
}
int main(void) {
    const char *database=getenv("STONE_MARKET_TEST_DATABASE");
    const char *user=getenv("STONE_MARKET_TEST_USER"),*password=getenv("STONE_MARKET_TEST_PASSWORD");
    const char *socket=getenv("STONE_MARKET_TEST_SOCKET"),*porttext=getenv("STONE_MARKET_TEST_PORT");
    int port=0;
    if(!database || strncmp(database,"stoneage_market_test_",21) || strlen(database)>64 || !database[21] || !user ||
        (porttext && (!market_number(porttext,0,&port) || port>65535))) {
        fprintf(stderr,"Set STONE_MARKET_TEST_DATABASE=stoneage_market_test_<unique>, STONE_MARKET_TEST_USER, optional PASSWORD/SOCKET/PORT; an EMPTY isolated test DB is required.\n");return 77;
    }
    for(const char *p=database;*p;++p) if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_')) return 77;
    mysql_init(&mysql);
    if(!mysql_real_connect(&mysql,socket?NULL:"127.0.0.1",user,password?password:"",database,(unsigned)port,socket,0)) {
        fprintf(stderr,"isolated test connection failed: %s\n",mysql_error(&mysql));return 1;
    }
    if(db_scalar("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE()")!=0) {
        fprintf(stderr,"Refusing non-empty database; create a new isolated test database.\n");mysql_close(&mysql);return 77;
    }
    assert(market_query("SET SESSION sql_mode='STRICT_TRANS_TABLES,NO_BACKSLASH_ESCAPES'"));
    m_reset();db_fixture_accounts();assert(market_db_ready());
    give_item(7,9,"market-real-item");copy_snapshot(&disk[0],7);stone_market_menu(7,130);m_request(7,"U|1|101|1|-1|9|");
    int id=latest_id();assert(db_state(id)==MARKET_DEPOSIT && db_balance(0)==984 && CHAR_getItemIndex(2,9)<0);
    m_ack(SUCCESSFUL);assert(db_state(id)==MARKET_AVAILABLE);
    MarketRow *row=malloc(sizeof(*row));assert(row && market_db_find(id,row)==1);
    assert(row->quantity==3 && row->total==303 && row->fee==16 && !strcmp(row->asset.owner_name,"角色'甲\\"));
    assert(market_db_stage(market_session(7),row,'D')==0 && db_balance(0)==984); /* duplicate UID rolls back */
    stone_market_menu(8,130);char request[100];snprintf(request,sizeof(request),"B|2|%d|",id);
    db_set_balance(1,302);m_request(8,request);assert(db_state(id)==MARKET_AVAILABLE && db_balance(1)==302);
    db_set_balance(1,303);m_request(8,request);assert(db_state(id)==MARKET_BUY && db_balance(1)==0 && db_balance(0)==984);
    assert(ITEM_getInt(CHAR_getItemIndex(3,9),ITEM_MODIFYATTACK)==37);m_ack(SUCCESSFUL);
    assert(db_state(id)==MARKET_SOLD && db_balance(0)==1287 && db_balance(1)==0);
    m_request(8,request);assert(db_balance(0)==1287 && stone_asset_uid_count(3,2,"market-real-item")==1);

    /* INSERT+fee COMMIT happened but the client did not receive its result. */
    give_item(7,9,"market-real-ambiguous-D");copy_snapshot(&disk[0],7);lost_commit_reply=1;
    m_request(7,"U|2|100|1|-1|9|");int uncertain=latest_id();assert(db_state(uncertain)==MARKET_DEPOSIT && db_balance(0)==1272 && CHAR_getItemIndex(2,9)>0);
    m_relog(7);assert(db_state(uncertain)==MARKET_CANCELLED && db_balance(0)==1287);
    stone_market_menu(7,130);
    /* Inject an SQL failure inside a real transaction: no fee and no row. */
    long long count=db_scalar("SELECT COUNT(*) FROM local_market_entries");fail_debit_query=1;m_request(7,"U|2|100|1|-1|9|");
    assert(db_scalar("SELECT COUNT(*) FROM local_market_entries")==count && db_balance(0)==1287 && CHAR_getItemIndex(2,9)>0);
    m_relog(7);stone_market_menu(7,130);m_request(7,"U|2|100|1|-1|9|");id=latest_id();m_ack(SUCCESSFUL);assert(db_state(id)==MARKET_AVAILABLE);
    db_set_balance(1,1000);copy_snapshot(&disk[1],8);lost_commit_reply=1;snprintf(request,sizeof(request),"B|2|%d|",id);m_request(8,request);
    assert(db_state(id)==MARKET_BUY && db_balance(1)==700 && stone_asset_uid_count(3,2,"market-real-ambiguous-D")==0);
    m_relog(8);assert(db_state(id)==MARKET_AVAILABLE && db_balance(1)==1000);stone_market_menu(8,130);
    m_request(8,request);lost_commit_reply=1;m_ack(SUCCESSFUL);assert(db_state(id)==MARKET_SOLD && stone_market_busy(8));
    int seller_before=db_balance(0);m_request(8,"L|2|");assert(!stone_market_busy(8) && db_balance(0)==seller_before+300);
    m_request(8,request);assert(db_balance(0)==seller_before+300);

    /* Real native pet + equipment, expiry/return, and durable overflow income. */
    int pet=give_pet(7,0,"market-real-pet",30),gear=give_item(7,9,"market-real-gear");CHAR_setItemIndex(2,9,-1);CHAR_setItemIndex(pet,0,gear);
    m_request(7,"U|3|100|3|0|0|");id=latest_id();m_ack(SUCCESSFUL);assert(market_db_find(id,row)==1 && row->duration==168);
    char sql[300];snprintf(sql,sizeof(sql),"UPDATE local_market_entries SET expires=UNIX_TIMESTAMP()-1 WHERE id=%d",id);assert(market_query(sql));
    snprintf(request,sizeof(request),"B|1|%d|",id);m_request(8,request);assert(db_state(id)==MARKET_AVAILABLE);
    snprintf(request,sizeof(request),"G|2|%d|",id);m_request(8,request);assert(db_state(id)==MARKET_AVAILABLE);
    m_request(7,request);m_ack(SUCCESSFUL);assert(db_state(id)==MARKET_RETURNED && CHAR_getItemIndex(CHAR_getCharPet(2,0),0)>0);
    db_set_balance(0,INT_MAX-10);m_request(7,"U|1|100|2|0|0|");id=latest_id();m_ack(SUCCESSFUL);
    snprintf(request,sizeof(request),"B|1|%d|",id);m_request(8,request);m_ack(SUCCESSFUL);
    assert(db_state(id)==MARKET_SOLD && db_balance(0)==INT_MAX && db_scalar("SELECT SUM(amount) FROM local_market_income")==85);
    db_set_balance(0,INT_MAX-50);m_request(7,"M|1|");assert(db_balance(0)==INT_MAX && db_scalar("SELECT SUM(amount) FROM local_market_income")==35);
    pet=CHAR_getCharPet(3,0);assert(!strcmp(CHAR_getChar(pet,CHAR_OWNERCDKEY),"fixture\\b") && CHAR_getItemIndex(pet,0)>0);
    assert(db_scalar("SELECT COUNT(*) FROM csalogin")==2 && db_scalar("SELECT COUNT(*) FROM vippointlog")>=10);
    free(row);mysql_close(&mysql);m_reset();
    puts("PASS market real SQL: isolated InnoDB, native assets + ACKs, exact-balance purchase, quoted identity, duplicate UID, rollback/lost COMMIT, expiry/return, seller income overflow");return 0;
}
#endif
