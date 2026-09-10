/* Stateful native/DB boundaries; all routing, identity checks, object movement,
 * journal recovery and FIFO save handling execute the actual implementation.
 * The default executable never connects to MySQL, a game process or a real
 * account. storage_database.c reuses only these native-memory boundaries and
 * selects the actual production SQL against its newly created fixture DB. */
#include <assert.h>
#ifndef STONE_STORAGE_REAL_DB
#define STONE_STORAGE_TEST_DB
#endif
#include "../native/app_storage.c"

#define TEST_CHARS 20
#define TEST_ITEMS 128
#define TEST_ROWS 128
static Char characters[TEST_CHARS];
static ITEM_Item objects[TEST_ITEMS];
static int object_used[TEST_ITEMS];
static int connected[2],epochs[2],profile[2],events[2],masters[2];
static StorageRow rows[TEST_ROWS];
static int row_count,db_down,finish_down,ambiguous_stage,fail_allocate,fail_allocate_after,fail_place,fail_receipt,fail_save,roundtrip_damage;
static int legacy[3],charges,gold[2],last_channel[2],last_sequence[2],last_window[2],views[2],notices[2];
static int other_mail_busy,other_attributes_busy,other_market_busy;
static char last_payload[2][STORAGE_PAYLOAD_SIZE],last_notice[2][512];
typedef struct { char scope[512];int kind,capacity; } Capacity;
static Capacity capacities[12];static int capacity_count;
typedef struct {
    Char chars[TEST_CHARS]; ITEM_Item items[TEST_ITEMS];int used[TEST_ITEMS];
    int fd,epoch;
} Snapshot;
static Snapshot snapshots[32],disk[2];static int sent_count,ack_count;
static ITEM_Item item_blobs[128];static int item_blob_count;
typedef struct { Char pet;ITEM_Item gear[CHAR_MAXPETITEMHAVE];int has[CHAR_MAXPETITEMHAVE]; } PetBlob;
static PetBlob pet_blobs[64];static int pet_blob_count;
MY_Lua MYLua;

static int conn(int fd) { assert(fd==7 || fd==8);return fd-7; }
static int player(int fd) { return fd-5; }
static void copy_snapshot(Snapshot *s,int fd) {
    memcpy(s->chars,characters,sizeof(characters));memcpy(s->items,objects,sizeof(objects));memcpy(s->used,object_used,sizeof(object_used));
    s->fd=fd;s->epoch=epochs[conn(fd)];
}
static void blank_char(Char *ch) {
    memset(ch,0,sizeof(*ch));
    for (int i=0;i<CHAR_MAXITEMHAVE;++i) ch->indexOfExistItems[i]=-1;
    for (int i=0;i<CHAR_MAXPOOLITEMHAVE;++i) ch->indexOfExistPoolItems[i]=-1;
    for (int i=0;i<CHAR_MAXPETHAVE;++i) ch->unionTable.indexOfPet[i]=-1;
    for (int i=0;i<CHAR_MAXPETSKILLHAVE;++i) ch->unionTable.indexOfPetskill[i]=-1;
    for (int i=0;i<CHAR_MAXPOOLPETHAVE;++i) ch->indexOfPoolPet[i]=-1;
    ch->workint[CHAR_WORKOBJINDEX]=-1;ch->workint[CHAR_WORKFD]=-1;
}
static void reset(void) {
    while (storage_sessions) { StorageSession *s=storage_sessions;storage_sessions=s->next;free(s); }
    for (int i=0;i<TEST_CHARS;++i) blank_char(&characters[i]);
    memset(objects,0,sizeof(objects));memset(object_used,0,sizeof(object_used));memset(rows,0,sizeof(rows));
    memset(capacities,0,sizeof(capacities));memset(legacy,0,sizeof(legacy));memset(&MYLua,0,sizeof(MYLua));
    row_count=capacity_count=charges=sent_count=ack_count=item_blob_count=pet_blob_count=0;
    db_down=finish_down=ambiguous_stage=fail_allocate=fail_place=fail_receipt=fail_save=roundtrip_damage=0;
    fail_allocate_after=-1;
    other_mail_busy=other_attributes_busy=other_market_busy=0;
    for (int i=0;i<2;++i) {
        int index=i+2;Char *ch=&characters[index];ch->use=TRUE;
        ch->data[CHAR_WHICHTYPE]=CHAR_TYPEPLAYER;ch->data[CHAR_LV]=20;ch->data[CHAR_FLOOR]=5543;
        ch->data[CHAR_RIDEPET]=-1;ch->data[CHAR_DEFAULTPET]=-1;ch->workint[CHAR_WORKFD]=i+7;
        snprintf(ch->string[CHAR_CDKEY].string,128,"fixture-%c",'a'+i);
        snprintf(ch->string[CHAR_NAME].string,128,"角色%c",'A'+i);
        connected[i]=1;epochs[i]=10+i;profile[i]=4;masters[i]=42;events[i]=1;gold[i]=1000;
        views[i]=notices[i]=0;last_payload[i][0]=last_notice[i][0]=0;
        copy_snapshot(&disk[i],i+7);
    }
}
static int item_create(int id,const char *uid) {
    for (int i=1;i<TEST_ITEMS;++i) if (!object_used[i]) {
        ITEM_Item *it=&objects[i];memset(it,0,sizeof(*it));object_used[i]=1;
        it->data[ITEM_ID]=id;it->data[ITEM_BASEIMAGENUMBER]=20033;it->data[ITEM_USEPILENUMS]=3;
        it->data[ITEM_TYPE]=ITEM_AXE;it->data[ITEM_MAGICID]=17;it->data[ITEM_COLOER]=2;
        it->data[ITEM_MODIFYATTACK]=37;it->data[ITEM_DAMAGECRUSHE]=4500;it->data[ITEM_MAXDAMAGECRUSHE]=9000;
        strcpy(it->string[ITEM_NAME].string,"原生'测试斧");strcpy(it->string[ITEM_SECRETNAME].string,"原生'测试斧");
        strcpy(it->string[ITEM_EFFECTSTRING].string,"攻 +37");strcpy(it->string[ITEM_TYPECODE].string,"2,4");
        strcpy(it->string[ITEM_INLAYCODE].string,"gem-17");snprintf(it->string[ITEM_UNIQUECODE].string,64,"%s",uid);
        return i;
    }
    assert(0);return -1;
}
static int give_item(int fd,int slot,const char *uid) {
    int object=item_create(0,uid);characters[player(fd)].indexOfExistItems[slot]=object;return object;
}
static int give_pet(int fd,int slot,const char *uid,int level) {
    for (int i=5;i<TEST_CHARS;++i) if (!characters[i].use) {
        Char *p=&characters[i];blank_char(p);p->use=TRUE;p->data[CHAR_WHICHTYPE]=CHAR_TYPEPET;
        p->data[CHAR_LV]=level;p->data[CHAR_PETID]=717;p->data[CHAR_BASEIMAGENUMBER]=100001;
        p->data[CHAR_VITAL]=10000;p->data[CHAR_STR]=8000;p->data[CHAR_TOUGH]=6000;p->data[CHAR_DEX]=5000;
        p->data[CHAR_SLOT]=2;p->data[CHAR_EARTHAT]=80;p->data[CHAR_WATERAT]=20;
        p->unionTable.indexOfPetskill[0]=11;p->unionTable.indexOfPetskill[1]=12;
        strcpy(p->string[CHAR_NAME].string,"原生'测试宠");strcpy(p->string[CHAR_PET_4V].string,"200|40|30|20|1");
        strcpy(p->string[CHAR_NEWNAME].string,"1|2|3|4");strcpy(p->string[CHAR_CAPTURE_DATA].string,"capture-preserved");
        strcpy(p->string[CHAR_PETTRN_4V].string,"trn-preserved");strcpy(p->string[CHAR_UNIQUECODE].string,uid);
        characters[player(fd)].unionTable.indexOfPet[slot]=i;p->workint[CHAR_WORKPLAYERINDEX]=player(fd);
        return i;
    }
    assert(0);return -1;
}
static int active_rows(int state) { int n=0;for(int i=1;i<=row_count;++i) if(rows[i].state==state) ++n;return n; }
static void ack(const char *status) {
    if (ack_count>=sent_count) fprintf(stderr,"missing save: rows=%d, noticeA=%s, noticeB=%s\n",row_count,last_notice[0],last_notice[1]);
    assert(ack_count<sent_count);Snapshot *s=&snapshots[ack_count++];
    if (!strcmp(status,SUCCESSFUL)) disk[conn(s->fd)]=*s;
    if (epochs[conn(s->fd)]==s->epoch) stone_storage_saved(s->fd,status);
}
static void relog(int fd) {
    Snapshot *s=&disk[conn(fd)];memcpy(characters,s->chars,sizeof(characters));memcpy(objects,s->items,sizeof(objects));memcpy(object_used,s->used,sizeof(object_used));
    ++epochs[conn(fd)];stone_storage_login(fd);
}
static void request(int fd,const char *data) {
    StorageSession *s=storage_session(fd);assert(s && s->kind);
    if (s->shared) assert(stone_storage_window(fd,s->sequence,-1,0,data)==1);
    else assert(stone_storage_personal(fd,s->kind,data)==1);
}

int stone_mail_busy(int fd) { return other_mail_busy; }
int stone_attributes_busy(int fd) { return other_attributes_busy; }
int stone_market_busy(int fd) { return other_market_busy; }

#ifndef STONE_STORAGE_REAL_DB
/* Durable database fixture with atomic publication/reservation/consumption. */
static int storage_db_ready(void) { return !db_down; }
static int storage_db_master(StorageSession *s) { return db_down ? -1 : masters[conn(s->fd)]; }
static Capacity *capacity_record(StorageSession *s) {
    for (int i=0;i<capacity_count;++i) if(capacities[i].kind==s->kind && !strcmp(capacities[i].scope,s->scope)) return &capacities[i];
    assert(capacity_count<12);Capacity *r=&capacities[capacity_count++];strcpy(r->scope,s->scope);r->kind=s->kind;
    r->capacity=s->shared ? (s->kind==STORAGE_ITEM?100:80) : 20;return r;
}
static int storage_db_capacity(StorageSession *s,int *capacity) {
    if(db_down) return -1;if(!s->shared && legacy[s->kind]) return 0;
    *capacity=capacity_record(s)->capacity;return 1;
}
static int storage_db_find(int id,StorageRow *row) {
    if(db_down) return -1;if(id<=0 || id>row_count) return 0;*row=rows[id];return 1;
}
static int storage_db_pending(StorageSession *s,StorageRow *row) {
    if(db_down) return -1;
    for(int i=1;i<=row_count;++i) {
        if((rows[i].state==STORE_DEPOSIT && storage_owns(s,&rows[i],'D')) ||
           (rows[i].state==STORE_WITHDRAW && storage_owns(s,&rows[i],'W'))) { *row=rows[i];return 1; }
    }
    return 0;
}
static int storage_db_uid(StorageSession *s,const char *uid,StorageRow *row) {
    if(db_down) return -1;
    for(int i=1;i<=row_count;++i) if(rows[i].state==STORE_AVAILABLE && rows[i].kind==s->kind &&
        !strcmp(rows[i].scope,s->scope) && !strcmp(rows[i].uid,uid)) { *row=rows[i];return 1; }
    return 0;
}
static int storage_db_list(StorageSession *s,char *out,size_t capacity,int *count) {
    if(db_down) return -1;*out=0;*count=0;
    for(int i=1;i<=row_count;++i) if(rows[i].state==STORE_AVAILABLE && rows[i].kind==s->kind && !strcmp(rows[i].scope,s->scope)) {
        ++*count;assert(storage_append(out,capacity,"%s",rows[i].summary));
    }
    return 1;
}
static int storage_db_deposit(StorageSession *s,StorageRow *r) {
    if(db_down) return -1;int count=0;
    for(int i=1;i<=row_count;++i) {
        if(rows[i].state<=STORE_WITHDRAW && rows[i].kind==s->kind && !strcmp(rows[i].uid,r->uid)) return -1;
        if(rows[i].state<=STORE_WITHDRAW && rows[i].kind==s->kind && !strcmp(rows[i].scope,s->scope)) ++count;
    }
    StorageRow pending;if(storage_db_pending(s,&pending)==1) return -1;
    if(count>=capacity_record(s)->capacity) return 0;
    assert(row_count+1<TEST_ROWS);r->id=++row_count;rows[r->id]=*r;
    return ambiguous_stage ? -1 : 1;
}
static int storage_db_withdraw(StorageSession *s,StorageRow *r) {
    if(db_down) return -1;StorageRow *stored=&rows[r->id],pending;
    if(stored->state!=STORE_AVAILABLE || strcmp(stored->scope,s->scope) || stored->kind!=s->kind || storage_db_pending(s,&pending)==1) return 0;
    if(stored->bound && !storage_owns(s,stored,'D')) return 0;
    stored->state=STORE_WITHDRAW;strcpy(stored->withdrawer,s->account);strcpy(stored->withname,s->name);
    return ambiguous_stage ? -1 : 1;
}
static int storage_db_transition(StorageSession *s,StorageRow *r,int target) {
    if(db_down || finish_down) return -1;StorageRow *stored=&rows[r->id];
    char op=stored->state==STORE_DEPOSIT?'D':'W';if(!storage_owns(s,stored,op)) return 0;
    assert(stored->state==STORE_DEPOSIT || stored->state==STORE_WITHDRAW);
    stored->state=target;
    if(op=='W' && target==STORE_AVAILABLE) stored->withdrawer[0]=stored->withname[0]=0;
    return 1;
}
static int storage_db_expand(StorageSession *s,int old_capacity,int cost) {
    if(db_down) return -1;Capacity *c=capacity_record(s);int *balance=&gold[conn(s->fd)];
    if(c->capacity!=old_capacity || *balance<cost) return 0;
    ++c->capacity;*balance-=cost;++charges;return 1;
}
#endif

/* Native boundaries, using real native data structures and complete snapshots. */
int CONNECT_checkfd(int fd) { return (fd==7 || fd==8) && connected[fd-7]; }
int CONNECT_getloginmark(int fd) { return profile[conn(fd)]; }
int CONNECT_getFdid(int fd) { return epochs[conn(fd)]; }
BOOL CONNECT_isCLI(int fd) { return connected[conn(fd)]; }
BOOL CONNECT_isLOGIN(int fd) { return connected[conn(fd)]; }
int CONNECT_getCharaindex(int fd) { return player(fd); }
void CONNECT_getCdkey(int fd,char *out,int size) { snprintf(out,size,"%s",characters[player(fd)].string[CHAR_CDKEY].string); }
void CONNECT_getCharname(int fd,char *out,int size) { snprintf(out,size,"%s",characters[player(fd)].string[CHAR_NAME].string); }
BOOL _CHAR_CHECKINDEX(char *file,int line,int index) { return index>=0 && index<TEST_CHARS && characters[index].use; }
int _CHAR_getInt(char *file,int line,int index,CHAR_DATAINT field) { assert(CHAR_CHECKINDEX(index));return characters[index].data[field]; }
int _CHAR_setInt(char *file,int line,int index,CHAR_DATAINT field,int value) { int old=characters[index].data[field];characters[index].data[field]=value;return old; }
int _CHAR_getWorkInt(char *file,int line,int index,CHAR_WORKDATAINT field) { assert(CHAR_CHECKINDEX(index));return characters[index].workint[field]; }
int _CHAR_setWorkInt(char *file,int line,int index,CHAR_WORKDATAINT field,int value) { int old=characters[index].workint[field];characters[index].workint[field]=value;return old; }
char *_CHAR_getChar(char *file,int line,int index,CHAR_DATACHAR field) { return characters[index].string[field].string; }
BOOL _CHAR_setChar(char *file,int line,int index,CHAR_DATACHAR field,const char *value) {
    if(field==CHAR_ACTIVE_ACT15 && fail_receipt) return FALSE;
    snprintf(characters[index].string[field].string,128,"%s",value);return TRUE;
}
Char *CHAR_getCharPointer(int index) { return &characters[index]; }
int _CHAR_getItemIndex(char *file,int line,int index,int slot) { return characters[index].indexOfExistItems[slot]; }
int _CHAR_setItemIndex(char *file,int line,int index,int slot,int item) {
    int old=characters[index].indexOfExistItems[slot];if(!fail_place || item==-1) characters[index].indexOfExistItems[slot]=item;return old;
}
int CHAR_getCharPet(int index,int slot) { return characters[index].unionTable.indexOfPet[slot]; }
int CHAR_setCharPet(int index,int slot,int pet) {
    int old=CHAR_getCharPet(index,slot);if(!fail_place || pet==-1) characters[index].unionTable.indexOfPet[slot]=pet;return old;
}
int CHAR_findEmptyItemBox(int index) { for(int i=CHAR_EQUIPPLACENUM;i<CHAR_MAXITEMHAVE;++i) if(characters[index].indexOfExistItems[i]<0) return i;return -1; }
int CHAR_getCharPetElement(int index) { for(int i=0;i<CHAR_MAXPETHAVE;++i) if(CHAR_getCharPet(index,i)<0) return i;return -1; }
BOOL _ITEM_CHECKINDEX(char *file,int line,int index) { return index>0 && index<TEST_ITEMS && object_used[index]; }
int _ITEM_getInt(char *file,int line,int index,ITEM_DATAINT field) { return objects[index].data[field]; }
char *ITEM_getChar(int index,ITEM_DATACHAR field) { return objects[index].string[field].string; }
ITEM_Item *ITEM_getItemPointer(int index) { return &objects[index]; }
int ITEM_setWorkInt(int index,ITEM_WORKDATAINT field,int value) { int old=objects[index].workint[field];objects[index].workint[field]=value;return old; }
void ITEM_setItemUniCode(int index) { if(!*ITEM_getChar(index,ITEM_UNIQUECODE)) snprintf(objects[index].string[ITEM_UNIQUECODE].string,64,"fixture-i%d",index); }
void CHAR_setPetUniCode(int index) { if(!*CHAR_getChar(index,CHAR_UNIQUECODE)) snprintf(characters[index].string[CHAR_UNIQUECODE].string,128,"fixturep%d",index); }
void _ITEM_endExistItemsOne(int index,char *file,int line) { assert(ITEM_CHECKINDEX(index));object_used[index]=0; }
void CHAR_endCharData(Char *ch) {
    for(int i=0;i<CHAR_MAXITEMHAVE;++i) { int object=ch->indexOfExistItems[i];if(ITEM_CHECKINDEX(object)) object_used[object]=0;ch->indexOfExistItems[i]=-1; }
    ch->use=FALSE;
}
void _CHAR_endCharOneArray(int index,char *file,int line) { CHAR_endCharData(&characters[index]); }
char *ITEM_makeStringFromItemIndex(int index,int mode) {
    static char blob[64];assert((mode==0 || mode==1) && item_blob_count<128);item_blobs[item_blob_count]=objects[index];
    snprintf(blob,sizeof(blob),"fixture-item:%d",item_blob_count++);return blob;
}
BOOL ITEM_makeExistItemsFromStringToArg(char *src,ITEM_Item *out,int mode) {
    int id=-1;if(sscanf(src,"fixture-item:%d",&id)!=1 || id<0 || id>=item_blob_count) return FALSE;
    *out=item_blobs[id];if(roundtrip_damage) ++out->data[ITEM_MODIFYATTACK];return TRUE;
}
int _ITEM_initExistItemsOne(char *file,int line,ITEM_Item *item) {
    if(fail_allocate || fail_allocate_after==0) return -1;
    if(fail_allocate_after>0) --fail_allocate_after;
    for(int i=1;i<TEST_ITEMS;++i) if(!object_used[i]) { objects[i]=*item;object_used[i]=1;return i; }
    return -1;
}
char *CHAR_makePetStringFromPetIndex(int index) {
    static char blob[1024];assert(pet_blob_count<64);PetBlob *p=&pet_blobs[pet_blob_count];p->pet=characters[index];
    for(int i=0;i<CHAR_MAXPETITEMHAVE;++i) { int object=p->pet.indexOfExistItems[i];p->has[i]=ITEM_CHECKINDEX(object);if(p->has[i]) p->gear[i]=objects[object]; }
    snprintf(blob,sizeof(blob),"fixture-pet:%d|",pet_blob_count++);
    for(int i=0;i<CHAR_MAXPETITEMHAVE;++i) if(p->has[i]) {
        char *itemblob=ITEM_makeStringFromItemIndex(p->pet.indexOfExistItems[i],1);
        assert(storage_append(blob,sizeof(blob),"pitem%d:%s|",i,itemblob));
    }
    return blob;
}
int CHAR_makePetFromStringToArg(char *src,Char *out,int slot) {
    int id=-1;blank_char(out);if(sscanf(src,"fixture-pet:%d",&id)!=1 || id<0 || id>=pet_blob_count) return -1;
    PetBlob *p=&pet_blobs[id];*out=p->pet;
    for(int i=0;i<CHAR_MAXPETITEMHAVE;++i) out->indexOfExistItems[i]=p->has[i] ? ITEM_initExistItemsOne(&p->gear[i]) : -1;
    return TRUE;
}
int PET_initCharOneArray(Char *pet) {
    if(fail_allocate) return -1;
    for(int i=5;i<TEST_CHARS;++i) if(!characters[i].use) { characters[i]=*pet;characters[i].use=TRUE;return i; }
    return -1;
}
BOOL _CHAR_complianceParameter(int index,char *file,int line) { return TRUE; }
BOOL CHAR_sendItemDataOne(int index,int slot) { return TRUE; }
BOOL _CHAR_sendStatusString(int index,char *category,char *file,int line) { return TRUE; }
BOOL CHAR_send_P_StatusString(int index,unsigned int fields) { return TRUE; }
void CHAR_sendCToArroundCharacter(int index) { }
void lssproto_KS_send(int fd,int slot,int mode) { }
BOOL NPC_EventCheckFlg(int index,int event) { assert(event==304);return events[index-2]; }
int ITEMTBL_getInt(int index,ITEM_DATAINT field) { assert(field==ITEM_BASEIMAGENUMBER);return 20033; }
int MAGIC_getMagicArray(int id) { return id==17?0:-1; }
char *MAGIC_getChar(int index,MAGIC_DATACHAR field) { return "恢复精灵"; }
int PETSKILL_getPetskillArray(int id) { return id==11 || id==12 ? id : -1; }
char *PETSKILL_getChar(int index,PETSKILL_DATACHAR field) { return index==11 ? "攻击" : "防御"; }
char *makeEscapeString(char *input,char *out,int size) { snprintf(out,size,"%s",input);return out; }
BOOL getStringFromIndexWithDelim_body(char *src,char *delim,int index,char *out,int size,char *file,int line) {
    const char *p=src;for(int i=1;i<index;++i) { p=strchr(p,*delim);if(!p) return FALSE;++p; }
    const char *end=strchr(p,*delim);size_t n=end ? (size_t)(end-p) : strlen(p);if(n>=(size_t)size) return FALSE;
    memcpy(out,p,n);out[n]=0;return TRUE;
}
BOOL CHAR_talkToCli(int index,int from,char *message,CHAR_COLOR color) {
    int c=index-2;assert(c>=0 && c<2);++notices[c];snprintf(last_notice[c],512,"%s",message);return TRUE;
}
static void display(int fd,int window,int seq,int channel,char *data) {
    int c=conn(fd);++views[c];last_window[c]=window;last_sequence[c]=seq;last_channel[c]=channel;
    snprintf(last_payload[c],sizeof(last_payload[c]),"%s",data);
}
void lssproto_WN_send(int fd,int type,int button,int seq,int object,char *data) { assert(button==0 && object==-1);display(fd,type,seq,66,data); }
void lssproto_WNUpdate_send(int fd,int type,int button,int seq,int object,char *data) { assert(button==0 && object==-1);display(fd,type,seq,808,data); }
void lssproto_NewSaMenu_send(int fd,int index,char *data) { display(fd,index,0,820,data); }
BOOL CHAR_charSaveFromConnect(int index,int unlock) {
    assert(index==2 || index==3);if(fail_save) return FALSE;assert(sent_count<32);
    int fd=index+5;copy_snapshot(&snapshots[sent_count++],fd);stone_storage_saving(fd);return TRUE;
}
/* No Lua activity resources are loaded in this isolated fixture. */
int lua_gettop(lua_State *L) { assert(0);return 0; }
int lua_getglobal(lua_State *L,const char *name) { assert(0);return 0; }
int lua_type(lua_State *L,int index) { assert(0);return 0; }
void lua_settop(lua_State *L,int index) { assert(0); }
void lua_pushinteger(lua_State *L,lua_Integer value) { assert(0); }
int lua_pcallk(lua_State *L,int nargs,int nresults,int errfunc,lua_KContext ctx,lua_KFunction continuation) { assert(0);return 0; }

static void test_integer_boundary(void) {
    int value=-1;
    /* UBSan catches an intermediate +ASCII overflow even if the final wrapped
     * value happens to equal INT_MAX on the current compiler. */
    assert(storage_number("2147483647",1,&value) && value==INT_MAX);
    assert(storage_number("2147483646",0,&value) && value==INT_MAX-1);
    value=73;assert(!storage_number("2147483648",0,&value) && value==73);
    assert(!storage_number("999999999999999999999",0,&value));
    assert(storage_number("0",0,&value) && value==0);
    assert(!storage_number("0",1,&value));
    char operation=0;
    assert(storage_receipt("storage:2147483647:D",&operation)==INT_MAX && operation=='D');
    assert(storage_receipt("storage:2147483648:D",&operation)==-1);
}

#ifndef STONE_STORAGE_REAL_DB
static void test_views_and_rules(void) {
    reset();assert(stone_storage_menu(7,999)==0);assert(stone_storage_menu(7,35)==1);
    assert(last_window[0]==1125 && last_channel[0]==66 && !strcmp(last_payload[0],"L|0|100|"));
    assert(stone_storage_menu(7,36)==1);assert(last_window[0]==1124 && !strcmp(last_payload[0],"L|0|80|"));
    stone_storage_personal(7,2,"O|");assert(last_channel[0]==820 && !strcmp(last_payload[0],"L|0|20|300|"));
    request(7,"H|");assert(charges==1 && gold[0]==700 && !strcmp(last_payload[0],"H|21|10"));
    stone_storage_personal(7,1,"O|");assert(!strcmp(last_payload[0],"L|0|20|500|"));
    request(7,"H|");assert(charges==2 && gold[0]==200 && !strcmp(last_payload[0],"H|21|100"));
    stone_storage_menu(7,35);request(7,"H|");assert(charges==2 && !strcmp(last_payload[0],"L|0|100|"));
    int before=views[0],seq=last_sequence[0];stone_storage_window(7,seq+1,-1,0,"H|");assert(views[0]==before);
    stone_storage_window(7,seq,44,0,"H|");assert(views[0]==before);request(7,"C|");assert(!storage_session(7)->kind);
    characters[2].data[CHAR_FLOOR]=41011;stone_storage_personal(7,2,"O|");assert(views[0]==before);
    characters[2].data[CHAR_FLOOR]=5543;characters[2].workint[CHAR_WORKTRADEMODE]=1;stone_storage_menu(7,35);assert(views[0]==before);
    reset();strcpy(characters[2].string[CHAR_ACTIVE_ACT15].string,"another-event");stone_storage_menu(7,35);assert(!views[0] && storage_session(7)->blocked);
    reset();legacy[STORAGE_ITEM]=1;stone_storage_personal(7,2,"O|");assert(!views[0]);
    reset();masters[0]=0;stone_storage_menu(7,35);assert(!views[0]);
    reset();profile[0]=0;stone_storage_menu(7,35);assert(!views[0]);
    reset();stone_storage_personal(7,2,"O|");capacity_record(storage_session(7))->capacity=45;
    request(7,"H|");assert(!charges && storage_fee(STORAGE_ITEM,45)==-1);
    stone_storage_personal(7,1,"O|");capacity_record(storage_session(7))->capacity=30;
    request(7,"H|");assert(!charges && storage_fee(STORAGE_PET,30)==-1);
    reset();stone_storage_personal(7,2,"O|");
    for(int n=0;n<20;++n) { char uid[32];snprintf(uid,sizeof(uid),"capacity-i-%d",n);give_item(7,9,uid);request(7,"C|9|");ack(SUCCESSFUL); }
    int retained=give_item(7,9,"capacity-i-full");request(7,"C|9|");assert(row_count==20 && CHAR_getItemIndex(2,9)==retained);
}

static void test_save_order_and_failures(void) {
    reset();int item=give_item(7,9,"native-i-order");copy_snapshot(&disk[0],7);
    stone_storage_personal(7,2,"O|");assert(!stone_storage_busy(7));CHAR_charSaveFromConnect(2,FALSE);assert(stone_storage_busy(7));request(7,"C|9|");
    assert(row_count==1 && rows[1].state==STORE_DEPOSIT && CHAR_getItemIndex(2,9)==-1);
    assert(!strcmp(CHAR_getChar(2,CHAR_ACTIVE_ACT15),"storage:1:D"));assert(!strstr(last_payload[0],"native-i-order"));
    request(7,"C|10|");assert(row_count==1);ack(SUCCESSFUL);assert(rows[1].state==STORE_DEPOSIT);
    ack("failed");assert(rows[1].state==STORE_DEPOSIT);CHAR_charSaveFromConnect(2,FALSE);ack(SUCCESSFUL);
    assert(rows[1].state==STORE_AVAILABLE && strstr(last_payload[0],"native-i-order") && !stone_storage_busy(7));
    request(7,"X|native-i-order|");assert(strstr(last_payload[0],"恢复精灵|2,4|gem-17"));
    request(7,"B|native-i-order|");assert(rows[1].state==STORE_WITHDRAW);int restored=CHAR_getItemIndex(2,9);
    assert(restored>0 && !strcmp(ITEM_getChar(restored,ITEM_UNIQUECODE),"native-i-order"));assert(ITEM_getInt(restored,ITEM_MODIFYATTACK)==37);
    assert(ITEM_getInt(restored,ITEM_USEPILENUMS)==3);request(7,"B|native-i-order|");assert(storage_inventory_uid(storage_session(7),2,"native-i-order")==1);
    ack(SUCCESSFUL);assert(rows[1].state==STORE_CONSUMED);request(7,"B|native-i-order|");assert(storage_inventory_uid(storage_session(7),2,"native-i-order")==1);
    /* Depositing that same unique object again produces a new journal id. */
    request(7,"C|9|");assert(row_count==2);ack(SUCCESSFUL);assert(rows[2].state==STORE_AVAILABLE);
    fail_allocate=1;request(7,"B|native-i-order|");assert(rows[2].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);fail_allocate=0;
    fail_receipt=1;request(7,"B|native-i-order|");assert(rows[2].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);fail_receipt=0;
    fail_place=1;request(7,"B|native-i-order|");assert(rows[2].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);fail_place=0;
    for(int slot=9;slot<CHAR_MAXITEMHAVE;++slot) { char uid[30];snprintf(uid,sizeof(uid),"full-i-%d",slot);give_item(7,slot,uid); }
    request(7,"B|native-i-order|");assert(rows[2].state==STORE_AVAILABLE);
    (void)item;
}

static void test_recovery(void) {
    reset();give_item(7,9,"recovery-i");copy_snapshot(&disk[0],7);stone_storage_personal(7,2,"O|");request(7,"C|9|");
    relog(7);assert(rows[1].state==STORE_CANCELLED && CHAR_getItemIndex(2,9)>0);
    reset();give_item(7,9,"recovery-i");copy_snapshot(&disk[0],7);stone_storage_personal(7,2,"O|");request(7,"C|9|");
    finish_down=1;ack(SUCCESSFUL);assert(rows[1].state==STORE_DEPOSIT);finish_down=0;relog(7);
    assert(rows[1].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);
    stone_storage_personal(7,2,"O|");request(7,"B|recovery-i|");relog(7);
    assert(rows[1].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);
    stone_storage_personal(7,2,"O|");request(7,"B|recovery-i|");
    /* Old epoch's queued ACK is ignored before this login's pending save. */
    ack(SUCCESSFUL);assert(rows[1].state==STORE_WITHDRAW);finish_down=1;ack(SUCCESSFUL);
    assert(rows[1].state==STORE_WITHDRAW);finish_down=0;relog(7);
    assert(rows[1].state==STORE_CONSUMED && storage_inventory_uid(storage_session(7),2,"recovery-i")==1);
    stone_storage_login(7);assert(storage_inventory_uid(storage_session(7),2,"recovery-i")==1);
    reset();give_item(7,9,"uncertain-i");copy_snapshot(&disk[0],7);stone_storage_personal(7,2,"O|");ambiguous_stage=1;request(7,"C|9|");
    assert(storage_session(7)->blocked && CHAR_getItemIndex(2,9)>0 && rows[1].state==STORE_DEPOSIT);
    ambiguous_stage=0;relog(7);assert(rows[1].state==STORE_CANCELLED && !storage_session(7)->blocked);
    reset();give_item(7,9,"logout-i");stone_storage_personal(7,2,"O|");request(7,"C|9|");stone_storage_logout(7);
    CHAR_charSaveFromConnect(2,FALSE);ack("failed");assert(rows[1].state==STORE_DEPOSIT);ack(SUCCESSFUL);assert(rows[1].state==STORE_AVAILABLE);
}

static void test_scopes_and_concurrency(void) {
    reset();give_item(7,9,"share-i");stone_storage_menu(7,35);stone_storage_menu(8,35);request(7,"I|9");
    request(8,"B|share-i");assert(CHAR_getItemIndex(3,9)==-1);ack(SUCCESSFUL);assert(strstr(last_payload[1],"share-i"));
    request(8,"B|share-i");assert(rows[1].state==STORE_WITHDRAW && CHAR_getItemIndex(3,9)>0);
    request(7,"B|share-i");assert(CHAR_getItemIndex(2,9)==-1);ack(SUCCESSFUL);assert(rows[1].state==STORE_CONSUMED);
    request(8,"I|9");ack(SUCCESSFUL);masters[0]=99;stone_storage_menu(7,35);request(7,"B|share-i");assert(CHAR_getItemIndex(2,9)==-1);
    masters[1]=77;request(8,"B|share-i");assert(storage_session(8)->blocked && rows[2].state==STORE_AVAILABLE);
    reset();give_item(7,9,"personal-i");stone_storage_personal(7,2,"O|");request(7,"C|9|");ack(SUCCESSFUL);
    stone_storage_personal(8,2,"O|");request(8,"B|personal-i|");assert(CHAR_getItemIndex(3,9)==-1 && rows[1].state==STORE_AVAILABLE);
    /* Same cdkey, different character remains a distinct personal scope. */
    strcpy(characters[3].string[CHAR_CDKEY].string,"fixture-a");++epochs[1];stone_storage_personal(8,2,"O|");
    request(8,"B|personal-i|");assert(CHAR_getItemIndex(3,9)==-1);
    reset();int i=give_item(7,9,"bound-i");objects[i].string[ITEM_NAME].string[0]='*';stone_storage_menu(7,35);request(7,"I|9");ack(SUCCESSFUL);
    stone_storage_menu(8,35);request(8,"B|bound-i");assert(rows[1].state==STORE_AVAILABLE && CHAR_getItemIndex(3,9)==-1);
    request(7,"B|bound-i");ack(SUCCESSFUL);assert(rows[1].state==STORE_CONSUMED);
}

static void test_pets_and_validation(void) {
    reset();int p=give_pet(7,0,"nativep-original",22);int gear=item_create(1,"gear-i");characters[p].indexOfExistItems[0]=gear;
    characters[p].indexOfExistItems[1]=item_create(1,"gear2-i");
    stone_storage_personal(7,1,"O|");events[0]=0;
    /* Native FAMILY_Add and trade.c use 1 for guardian pets, not the role leader flag 3. */
    characters[p].data[CHAR_PETFAMILY]=1;request(7,"C|0|");
    assert(!row_count && CHAR_getCharPet(2,0)==p && characters[p].data[CHAR_PETFAMILY]==1);
    assert(stone_storage_menu(7,36)==1 && storage_session(7)->shared);
    request(7,"I|0");
    assert(!row_count && CHAR_getCharPet(2,0)==p);
    characters[p].data[CHAR_PETFAMILY]=0;stone_storage_personal(7,1,"O|");
    characters[2].data[CHAR_RIDEPET]=0;characters[2].data[CHAR_DEFAULTPET]=0;request(7,"C|0|");
    assert(rows[1].state==STORE_DEPOSIT && CHAR_getCharPet(2,0)==-1 && characters[2].data[CHAR_RIDEPET]==-1 && characters[2].data[CHAR_DEFAULTPET]==-1);
    ack(SUCCESSFUL);request(7,"X|nativep-original|");assert(strstr(last_payload[0],"攻击|防御|1|2|3|4|"));
    characters[2].data[CHAR_LV]=10;request(7,"B|nativep-original|");assert(rows[1].state==STORE_AVAILABLE);
    characters[2].data[CHAR_TRANSMIGRATION]=1;fail_allocate_after=1;request(7,"B|nativep-original|");
    assert(rows[1].state==STORE_AVAILABLE && CHAR_getCharPet(2,0)==-1);
    for(int i=1;i<TEST_ITEMS;++i) assert(!object_used[i]);
    fail_allocate_after=-1;characters[2].data[CHAR_TRANSMIGRATION]=0;
    characters[2].data[CHAR_LV]=20;request(7,"B|nativep-original|");p=CHAR_getCharPet(2,0);assert(p>=5);
    assert(!strcmp(CHAR_getChar(p,CHAR_CAPTURE_DATA),"capture-preserved"));assert(!strcmp(CHAR_getChar(p,CHAR_PETTRN_4V),"trn-preserved"));
    assert(!strcmp(CHAR_getChar(p,CHAR_OWNERCDKEY),"fixture-a"));assert(CHAR_getInt(p,CHAR_LV)==22);
    gear=CHAR_getItemIndex(p,0);assert(ITEM_CHECKINDEX(gear) && !strcmp(ITEM_getChar(gear,ITEM_UNIQUECODE),"gear-i"));ack(SUCCESSFUL);
    stone_storage_menu(7,36);request(7,"I|0");ack(SUCCESSFUL);stone_storage_menu(8,36);request(8,"B|nativep-original");ack(SUCCESSFUL);
    p=CHAR_getCharPet(3,0);assert(p>=5 && !strcmp(CHAR_getChar(p,CHAR_OWNERCDKEY),"fixture-b"));
    /* A full pet party never reserves/deletes a bank entry. */
    request(8,"I|0");ack(SUCCESSFUL);for(int i=0;i<CHAR_MAXPETHAVE;++i) { char uid[30];snprintf(uid,sizeof(uid),"fullp%d",i);give_pet(7,i,uid,1); }
    request(7,"B|nativep-original");assert(rows[3].state==STORE_AVAILABLE);
    reset();int i=give_item(7,9,"bad-i");stone_storage_personal(7,2,"O|");objects[i].data[ITEM_VANISHATDROP]=1;request(7,"C|9|");assert(!row_count);
    objects[i].data[ITEM_VANISHATDROP]=0;strcpy(objects[i].string[ITEM_USEFUNC].string,"ITEM_useSkup");request(7,"C|9|");assert(!row_count);
    objects[i].string[ITEM_USEFUNC].string[0]=0;roundtrip_damage=1;request(7,"C|9|");assert(!row_count && CHAR_getItemIndex(2,9)==i);
    roundtrip_damage=0;request(7,"C|9|junk");request(7,"C|999999999999999999999|");assert(!row_count);
    fail_save=1;request(7,"C|9|");assert(rows[1].state==STORE_DEPOSIT && storage_session(7)->pending);
    fail_save=0;CHAR_charSaveFromConnect(2,FALSE);ack(SUCCESSFUL);assert(rows[1].state==STORE_AVAILABLE);
}

static void test_app_pet_without_npc_qualification(void) {
    reset();characters[2].data[CHAR_LV]=characters[3].data[CHAR_LV]=4;
    events[0]=events[1]=0;
    give_pet(7,0,"target-resting-pet",1);
    stone_storage_personal(7,1,"O|");request(7,"C|0|");
    assert(rows[1].state==STORE_DEPOSIT && CHAR_getCharPet(2,0)==-1);
    ack(SUCCESSFUL);request(7,"B|target-resting-pet|");ack(SUCCESSFUL);
    assert(rows[1].state==STORE_CONSUMED && CHAR_getCharPet(2,0)>=5);
    stone_storage_menu(7,36);request(7,"I|0");ack(SUCCESSFUL);
    stone_storage_menu(8,36);request(8,"B|target-resting-pet");ack(SUCCESSFUL);
    assert(rows[2].state==STORE_CONSUMED && CHAR_getCharPet(3,0)>=5);
}

static void test_shared_codec(void) {
    reset();StoneNativeAsset asset;
    int item=give_item(7,9,"codec-i");assert(stone_asset_serialize(2,STONE_ASSET_ITEM,9,&asset)==1);
    assert(!strcmp(asset.owner_account,"fixture-a") && !strcmp(asset.uid,"codec-i"));
    assert(stone_asset_restore(3,8,&asset)==-1);assert(stone_asset_remove(2,2,9,"different-i")==0);
    assert(stone_asset_remove(2,2,9,asset.uid)==1);assert(stone_asset_uid_count(2,2,asset.uid)==0);
    item=stone_asset_restore(3,9,&asset);assert(item>0 && ITEM_getInt(item,ITEM_MODIFYATTACK)==37);
    assert(stone_asset_restore(3,10,&asset)==-1 && stone_asset_uid_count(3,2,asset.uid)==1);
    profile[1]=0;assert(stone_asset_uid_count(3,2,asset.uid)==-1);assert(stone_asset_remove(3,2,9,asset.uid)==0);profile[1]=4;
    assert(stone_asset_serialize(3,2,9,&asset)==1);asset.bound=1;
    assert(stone_asset_remove(3,2,9,asset.uid)==1);assert(stone_asset_restore(2,9,&asset)==-1);
    assert(stone_asset_restore(3,9,&asset)>0);
    reset();int pet=give_pet(7,0,"codecp-with-gear",7);
    characters[pet].indexOfExistItems[0]=item_create(1,"codec-gear-i");characters[pet].indexOfExistItems[1]=item_create(1,"codec-gear2-i");
    assert(stone_asset_serialize(2,1,0,&asset)==1);assert(stone_asset_remove(2,1,0,asset.uid)==1);
    fail_allocate_after=1;assert(stone_asset_restore(3,0,&asset)==-1 && CHAR_getCharPet(3,0)==-1);
    for(int i=1;i<TEST_ITEMS;++i) assert(!object_used[i]);
    fail_allocate_after=-1;pet=stone_asset_restore(3,0,&asset);assert(pet>=5);
    assert(!strcmp(CHAR_getChar(pet,CHAR_UNIQUECODE),asset.uid) && !strcmp(CHAR_getChar(pet,CHAR_OWNERCHARANAME),"角色B"));
    assert(ITEM_CHECKINDEX(CHAR_getItemIndex(pet,0)) && ITEM_CHECKINDEX(CHAR_getItemIndex(pet,1)));
}

static void test_other_feature_barrier(void) {
    reset();give_item(7,9,"barrier-i");stone_storage_personal(7,2,"O|");
    int *flags[]={&other_mail_busy,&other_attributes_busy,&other_market_busy};
    for(int i=0;i<3;++i) { *flags[i]=1;request(7,"C|9|");request(7,"H|");assert(!row_count && !charges && CHAR_getItemIndex(2,9)>0);*flags[i]=0; }
    request(7,"C|9|");ack(SUCCESSFUL);assert(rows[1].state==STORE_AVAILABLE);
    for(int i=0;i<3;++i) { *flags[i]=1;request(7,"B|barrier-i|");assert(rows[1].state==STORE_AVAILABLE && CHAR_getItemIndex(2,9)==-1);*flags[i]=0; }
    request(7,"B|barrier-i|");ack(SUCCESSFUL);assert(rows[1].state==STORE_CONSUMED);
}

int main(void) {
    fprintf(stderr,"storage: integer boundary\n");test_integer_boundary();
    fprintf(stderr,"storage: views\n");test_views_and_rules();
    fprintf(stderr,"storage: save ordering\n");test_save_order_and_failures();
    fprintf(stderr,"storage: recovery\n");test_recovery();
    fprintf(stderr,"storage: scopes\n");test_scopes_and_concurrency();
    fprintf(stderr,"storage: pets\n");test_pets_and_validation();
    fprintf(stderr,"storage: App pet qualification\n");test_app_pet_without_npc_qualification();
    fprintf(stderr,"storage: shared native codec\n");test_shared_codec();
    fprintf(stderr,"storage: other feature barrier\n");test_other_feature_barrier();
    puts("PASS: four storage contracts, native assets, ownership, capacity, FIFO saves, failures and restart recovery");return 0;
}
#endif
