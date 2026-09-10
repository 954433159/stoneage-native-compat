/* Stateful boundaries: SQL rows, native inventory, saved character snapshots,
 * and FIFO SAAC acknowledgements. The mail implementation itself is included. */
#include <assert.h>
int stone_storage_busy(int fd) { return 0; }
int stone_attributes_busy(int fd) { return 0; }
int stone_market_busy(int fd) { return 0; }
#include <sys/wait.h>
#include <unistd.h>
#include "../native/app_mail.c"

MYSQL mysql;
typedef struct {
    int id, item, claimed, created, expires;
    char account[64], name[192], title[384], body[6144];
} Row;
static Row rows[110];
static int row_count, db_down, update_down, queries, updates;
static int connected = 1, epoch = 10, logged_in = 1, client = 1, char_fd = 7, profile = 4;
static int trade_mode, battle_mode;
static char account[64] = "fixture", name[192] = "RoleA", receipt[192];
static char disk_receipt[192], last_message[512];
static int bag[CHAR_MAXITEMHAVE], disk_bag[CHAR_MAXITEMHAVE];
static int items[64], next_item = 1, grants, item_messages;
static int notices, fail_create, fail_place, fail_receipt, fail_save;
static int view_count, open_count, update_count, last_sequence, last_function, popup_count;
static int red_count, red_flag, int_values[2], int_count;
static char last_payload[MAIL_PAYLOAD_SIZE];
char PersonalKey[4096];
typedef struct { int epoch; char receipt[192]; int bag[CHAR_MAXITEMHAVE]; } Snapshot;
static Snapshot snapshots[64];
static int snapshot_count, snapshot_ack;
typedef struct {
    Row row[110]; int count, cursor;
    char id[24], item[24], claimed[24], created[24], expires[24];
    char *columns[7]; unsigned long lengths[7];
} Result;
static Result result;

int CONNECT_checkfd(int fd) { return fd == 7 && connected; }
int CONNECT_getloginmark(int fd) { return profile; }
int CONNECT_getFdid(int fd) { assert(fd == 7); return epoch; }
BOOL CONNECT_isCLI(int fd) { return client; }
BOOL CONNECT_isLOGIN(int fd) { return logged_in; }
int CONNECT_getCharaindex(int fd) { return connected ? 2 : -1; }
void CONNECT_getCdkey(int fd, char *out, int size) { snprintf(out, size, "%s", account); }
void CONNECT_getCharname(int fd, char *out, int size) { snprintf(out, size, "%s", name); }
BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return index == 2 && connected; }
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    assert(index == 2); return field == CHAR_WHICHTYPE ? CHAR_TYPEPLAYER : 0;
}
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    assert(index == 2);
    if (field == CHAR_WORKTRADEMODE) return trade_mode;
    if (field == CHAR_WORKBATTLEMODE) return battle_mode;
    assert(field == CHAR_WORKFD); return char_fd;
}
char *_CHAR_getChar(char *file, int line, int index, CHAR_DATACHAR field) {
    assert(index == 2);
    if (field == CHAR_CDKEY) return account;
    if (field == CHAR_NAME) return name;
    assert(field == CHAR_ACTIVE_ACT16); return receipt;
}
BOOL _CHAR_setChar(char *file, int line, int index, CHAR_DATACHAR field, const char *value) {
    assert(index == 2 && field == CHAR_ACTIVE_ACT16);
    if (fail_receipt) return FALSE;
    snprintf(receipt, sizeof(receipt), "%s", value); return TRUE;
}
int CHAR_findEmptyItemBox(int index) {
    assert(index == 2);
    for (int n = CHAR_EQUIPPLACENUM; n < CHAR_MAXITEMHAVE; ++n) if (bag[n] == -1) return n;
    return -1;
}
int _CHAR_getItemIndex(char *file, int line, int index, int slot) {
    assert(index == 2 && slot >= 0 && slot < CHAR_MAXITEMHAVE); return bag[slot];
}
int _CHAR_setItemIndex(char *file, int line, int index, int slot, int item) {
    int previous = bag[slot];
    if (!fail_place || item == -1) bag[slot] = item;
    return previous;
}
BOOL ITEM_CHECKITEMTABLE(int id) { return id == 0; }
int ITEM_makeItemAndRegist(int id) {
    assert(id == 0); if (fail_create) return -1;
    assert(next_item < 64); items[next_item] = 1; ++grants; return next_item++;
}
int ITEM_setWorkInt(int index, ITEM_WORKDATAINT field, int value) {
    assert(index > 0 && items[index]);
    assert((field == ITEM_WORKCHARAINDEX && value == 2) || (field == ITEM_WORKOBJINDEX && value == -1));
    return 0;
}
void _ITEM_endExistItemsOne(int index, char *file, int line) { assert(items[index]); items[index] = 0; }
int ITEMTBL_getInt(int id, ITEM_DATAINT field) { assert(id == 0 && field == ITEM_BASEIMAGENUMBER); return 20033; }
char *ITEMTBL_getChar(int id, ITEM_DATACHAR field) {
    assert(id == 0);
    if (field == ITEM_NAME || field == ITEM_SECRETNAME) return "小斧头";
    assert(field == ITEM_EFFECTSTRING); return "攻 +9 防 -3 敏 -3";
}
BOOL CHAR_sendItemDataOne(int index, int slot) { assert(index == 2); ++item_messages; return TRUE; }
BOOL CHAR_talkToCli(int index, int from, char *message, CHAR_COLOR color) {
    assert(index == 2); ++notices; snprintf(last_message, sizeof(last_message), "%s", message); return TRUE;
}
static void view(int fd, int type, int buttons, int sequence, int object, char *data, int function) {
    assert(fd == 7 && type == 1026 && buttons == 0 && object == -1);
    assert(sequence > 0 && strlen(data) < sizeof(last_payload));
    ++view_count; last_sequence = sequence; last_function = function;
    snprintf(last_payload, sizeof(last_payload), "%s", data);
    if (!strcmp(data,"S")) { assert(function == 808); ++popup_count; }
}
void lssproto_WN_send(int fd, int type, int buttons, int sequence, int object, char *data) {
    ++open_count; view(fd,type,buttons,sequence,object,data,66);
}
void lssproto_WNUpdate_send(int fd, int type, int buttons, int sequence, int object, char *data) {
    ++update_count; view(fd,type,buttons,sequence,object,data,808);
}
char *getRandDefaultKey(int fd) { return "fixture-key"; }
int util_mkint(int fd, char *buffer, int value) {
    assert(fd == 7 && int_count < 2); int_values[int_count++] = value; return value;
}
void _util_SendMesg(char *file, int line, int fd, int function, char *buffer) {
    assert(fd == 7 && function == 831 && int_count == 2);
    assert((int_values[0] == 0 || int_values[0] == 1) && int_values[1] == int_values[0]);
    ++red_count; red_flag = int_values[0]; int_count = 0;
}
BOOL CHAR_charSaveFromConnect(int index, BOOL unlock) {
    assert(index == 2 && !unlock);
    if (fail_save) return FALSE;
    Snapshot *s = &snapshots[snapshot_count++];
    s->epoch = epoch; strcpy(s->receipt, receipt); memcpy(s->bag, bag, sizeof(bag));
    stone_mail_saving(7); return TRUE;
}
static void ack(const char *status, int deliver) {
    assert(snapshot_ack < snapshot_count);
    Snapshot *s = &snapshots[snapshot_ack++];
    if (!strcmp(status, SUCCESSFUL)) { strcpy(disk_receipt, s->receipt); memcpy(disk_bag, s->bag, sizeof(bag)); }
    if (deliver && s->epoch == epoch) stone_mail_saved(7, status);
}

static void hex_value(const char *query, const char *key, char *out, size_t size) {
    const char *p = strstr(query, key); assert(p); p += strlen(key);
    size_t n = 0;
    while (*p != '\'') {
        unsigned value; assert(sscanf(p, "%2x", &value) == 1 && n + 1 < size);
        out[n++] = (char)value; p += 2;
    }
    out[n] = 0;
}
/* Execute the subset used by this adapter against persisted fixture rows;
 * ownership predicates are evaluated, not replaced with canned success. */
int sasql_mysql_query(char *query) {
    ++queries; if (db_down) return 1;
    if (!strncmp(query, "CREATE TABLE IF NOT EXISTS local_system_mail", strlen("CREATE TABLE IF NOT EXISTS local_system_mail"))) return 0;
    if (!strncmp(query, "ALTER TABLE local_system_mail ADD COLUMN IF NOT EXISTS", strlen("ALTER TABLE local_system_mail ADD COLUMN IF NOT EXISTS"))) return 0;
    char who[64], character[192];
    hex_value(query, "cdkey=X'", who, sizeof(who));
    hex_value(query, "charname=X'", character, sizeof(character));
    int id = 0; const char *where = strstr(query, "WHERE id=");
    if (where) assert(sscanf(where, "WHERE id=%d", &id) == 1);
    int update = !strncmp(query, "UPDATE local_system_mail SET claimed=1 ", strlen("UPDATE local_system_mail SET claimed=1 "));
    assert(update || !strncmp(query, "SELECT id,title,body,item_template,claimed,UNIX_TIMESTAMP(created_at),expires_at FROM local_system_mail ", strlen("SELECT id,title,body,item_template,claimed,UNIX_TIMESTAMP(created_at),expires_at FROM local_system_mail ")));
    if (update && update_down) return 1;
    memset(&result, 0, sizeof(result));
    for (int n = 0; n < row_count; ++n) {
        Row *r = &rows[n];
        if (strcmp(who, r->account) || strcmp(character, r->name) || (id && id != r->id)) continue;
        if (strstr(query, "AND claimed=0") && r->claimed) continue;
        if (update) { r->claimed = 1; ++updates; }
        else if (result.count < 100) result.row[result.count++] = *r;
    }
    return 0;
}
MYSQL_RES *mysql_store_result(MYSQL *connection) { return (MYSQL_RES *)&result; }
unsigned int mysql_num_fields(MYSQL_RES *res) { return 7; }
MYSQL_ROW mysql_fetch_row(MYSQL_RES *res) {
    Result *r = (Result *)res; if (r->cursor == r->count) return NULL;
    Row *row = &r->row[r->cursor++];
    snprintf(r->id, sizeof(r->id), "%d", row->id);
    snprintf(r->item, sizeof(r->item), "%d", row->item);
    snprintf(r->claimed, sizeof(r->claimed), "%d", row->claimed);
    snprintf(r->created, sizeof(r->created), "%d", row->created);
    snprintf(r->expires, sizeof(r->expires), "%d", row->expires);
    r->columns[0] = r->id; r->columns[1] = row->title; r->columns[2] = row->body;
    r->columns[3] = row->item < 0 ? NULL : r->item; r->columns[4] = r->claimed;
    r->columns[5] = r->created; r->columns[6] = row->expires ? r->expires : NULL;
    for (int n = 0; n < 7; ++n) r->lengths[n] = r->columns[n] ? strlen(r->columns[n]) : 0;
    return r->columns;
}
unsigned long *mysql_fetch_lengths(MYSQL_RES *res) { return ((Result *)res)->lengths; }
void mysql_free_result(MYSQL_RES *res) {}

static void seed(void) {
    for (int n = 0; n < CHAR_MAXITEMHAVE; ++n) bag[n] = disk_bag[n] = -1;
    rows[0] = (Row){.id=1,.item=-1,.account="fixture",.name="RoleA",.title="通知",.body="无附件正文"};
    rows[1] = (Row){.id=2,.item=0,.account="fixture",.name="RoleA",.title="道具",.body="小斧头一把"};
    rows[2] = (Row){.id=3,.item=0,.account="fixture",.name="RoleB",.title="其他角色",.body="正文"};
    rows[3] = (Row){.id=4,.item=0,.account="other",.name="RoleA",.title="其他账号",.body="正文"};
    row_count = 4;
    for (int n=0;n<row_count;++n) rows[n].created = 1700000000;
}
static void relog(void) {
    ++epoch; snapshot_count = snapshot_ack = 0;
    strcpy(receipt, disk_receipt); memcpy(bag, disk_bag, sizeof(bag)); stone_mail_login(7);
}
static void open_mail(void) {
    assert(stone_mail_menu(7,118)==1);
    assert(last_function==66 && !strncmp(last_payload,"L|",2));
}
static void click(const char *data) {
    assert(stone_mail_window(7,last_sequence,-1,0,data)==1);
}
static void field(int position, const char *expected) {
    const char *p=last_payload;
    for (int n=1;n<position;++n) { p=strchr(p,'|'); assert(p); ++p; }
    const char *end=strchr(p,'|'); size_t size=end?(size_t)(end-p):strlen(p);
    assert(size==strlen(expected) && !strncmp(p,expected,size));
}
static void opens_exact_target_fields_and_refreshes_database(void) {
    stone_mail_login(7); assert(red_count==1 && red_flag==1 && !view_count && !grants);
    stone_mail_login(7); assert(red_count==1 && !view_count);
    assert(!stone_mail_menu(7,117) && !view_count);
    open_mail(); assert(open_count==1);
    field(1,"L"); field(2,"2"); field(3,"1"); field(4,"1");
    field(5,""); field(6,"0"); field(7,""); field(8,"通知"); field(9,"无附件正文");
    field(10,"1700000000"); field(11,"2147483647");
    field(12,"1"); field(13,"2"); field(14,"小斧头"); field(15,"20033");
    field(16,"攻 +9 防 -3 敏 -3"); field(17,"道具"); field(18,"小斧头一把");
    field(19,"1700000000"); field(20,"2147483647");
    strcpy(rows[0].title,"已更新"); click("T|1");
    assert(last_function==808 && open_count==1); field(8,"已更新");
}
static void empty_mailbox_opens_and_clears_red_dot(void) {
    rows[0].claimed=rows[1].claimed=1; stone_mail_login(7);
    assert(red_count==1 && !red_flag && !view_count);
    open_mail(); assert(!strcmp(last_payload,"L|0")); click("T|1");
    assert(last_function==808 && !strcmp(last_payload,"L|0") && !grants);
}
static void requires_authenticated_native_owner(void) {
    logged_in=0; stone_mail_login(7); stone_mail_menu(7,118); logged_in=1;
    client=0; stone_mail_login(7); client=1;
    char_fd=9; stone_mail_login(7); char_fd=7;
    profile=1; stone_mail_login(7); assert(!stone_mail_menu(7,118)); profile=4;
    stone_mail_login(8); assert(!queries && !view_count && !grants);
    stone_mail_request(7,2); assert(!grants); /* no authenticated view yet */
    open_mail(); click("R|3|1"); click("R|4|1"); click("D|3|1"); click("D|4|1");
    assert(!grants && !snapshot_count && !updates);
    /* Ownership is checked again against SQL even after a row was displayed. */
    strcpy(rows[1].name,"RoleB"); click("R|2|1");
    assert(!grants && !snapshot_count && !updates);
}
static void stale_sequence_wrong_object_and_malformed_actions_are_rejected(void) {
    open_mail(); int old=last_sequence; open_mail(); assert(last_sequence!=old);
    assert(stone_mail_window(7,old,-1,0,"R|2|1")==1);
    assert(stone_mail_window(7,last_sequence,2,0,"R|2|1")==1);
    assert(stone_mail_window(7,last_sequence,-1,1,"R|2|1")==1);
    assert(!stone_mail_window(7,123,-1,0,"R|2|1"));
    const char *bad[]={"R|0|1","R|-1|1","R|02|1","R|2147483648|1","R|2|2","R|2|1|",
                       "R|2|1|fixture","R|2","R|2|1x","D|2|0","T|2","T|1|","","R","D||1"};
    for(size_t n=0;n<sizeof(bad)/sizeof(bad[0]);++n) click(bad[n]);
    assert(!grants && !snapshot_count && !updates);
    ++epoch; assert(stone_mail_window(7,last_sequence,-1,0,"R|2|1")==1);
    assert(!grants && !snapshot_count);
}
static void full_bag_keeps_mail(void) {
    open_mail(); for(int n=CHAR_EQUIPPLACENUM;n<CHAR_MAXITEMHAVE;++n) bag[n]=50;
    click("R|2|1");
    assert(!grants && !receipt[0] && !rows[1].claimed && !snapshot_count && !popup_count && notices);
}
static void item_claim_waits_for_its_snapshot_and_popup_confirmation(void) {
    open_mail(); CHAR_charSaveFromConnect(2,FALSE); click("R|2|1");
    assert(grants==1 && bag[CHAR_EQUIPPLACENUM]==1 && item_messages==1);
    assert(!strcmp(receipt,"mail:2") && snapshot_count==2 && !rows[1].claimed && !popup_count);
    ack(SUCCESSFUL,1); assert(!rows[1].claimed && !popup_count);
    click("R|2|1"); assert(grants==1 && snapshot_count==2);
    stone_mail_login(7); assert(!rows[1].claimed); /* live receipt is not disk proof */
    ack(SUCCESSFUL,1); assert(rows[1].claimed && popup_count==1 && !strcmp(last_payload,"S"));
    click("T|1"); assert(last_function==808); field(2,"1"); field(4,"1");
    click("R|2|1"); assert(grants==1 && snapshot_count==2);
}
static void notice_also_has_durable_receipt(void) {
    open_mail(); click("R|1|1");
    assert(!grants && !strcmp(receipt,"mail:1") && !rows[0].claimed && snapshot_count==1);
    ack(SUCCESSFUL,1); assert(rows[0].claimed && !popup_count && last_function==808); field(2,"1"); field(4,"2");
}
static void delete_preserves_unclaimed_attachment_and_saves_empty_mail(void) {
    open_mail(); click("D|2|1");
    assert(!grants && !receipt[0] && !rows[1].claimed && !updates && !snapshot_count && notices);
    field(2,"2"); click("D|1|1");
    assert(!rows[0].claimed && !grants && snapshot_count==1);
    ack(SUCCESSFUL,1); assert(rows[0].claimed && !rows[1].claimed && !popup_count); field(2,"1");
    click("D|2|1"); assert(!rows[1].claimed && !grants && snapshot_count==1);
}
static void failed_save_needs_later_complete_snapshot(void) {
    open_mail(); click("R|2|1"); ack(FAILED,1);
    assert(!rows[1].claimed && !popup_count && !strcmp(receipt,"mail:2") && notices);
    click("R|1|1"); assert(snapshot_count==1 && !rows[0].claimed);
    CHAR_charSaveFromConnect(2,FALSE); ack(SUCCESSFUL,1);
    assert(rows[1].claimed && grants==1 && !popup_count); field(2,"1");
}
static void db_failure_blocks_new_claim_until_confirmed(void) {
    open_mail(); click("R|2|1"); update_down=1; ack(SUCCESSFUL,1);
    assert(!rows[1].claimed && !popup_count && !strcmp(receipt,"mail:2"));
    click("R|1|1"); assert(!rows[0].claimed && snapshot_count==1 && grants==1);
    update_down=0; click("R|1|1"); assert(rows[1].claimed && snapshot_count==1);
    /* A refresh caused by finishing the earlier claim cannot confirm a new selection. */
    click("R|1|1"); assert(snapshot_count==2 && !strcmp(receipt,"mail:1"));
    ack(SUCCESSFUL,1); assert(rows[0].claimed && grants==1 && !red_flag);
}
static void relog_recovers_saved_attachment_without_regrant(void) {
    open_mail(); click("R|2|1"); ack(SUCCESSFUL,0);
    assert(!rows[1].claimed && !strcmp(disk_receipt,"mail:2"));
    int before=view_count; relog(); assert(rows[1].claimed && grants==1 && bag[CHAR_EQUIPPLACENUM]==1 && view_count==before);
    open_mail(); field(2,"1"); click("R|2|1"); assert(grants==1 && !snapshot_count);
}
static void relog_without_saved_snapshot_retries_normally(void) {
    open_mail(); click("R|2|1"); ack(FAILED,1); relog();
    assert(!receipt[0] && !rows[1].claimed && bag[CHAR_EQUIPPLACENUM]==-1);
    open_mail(); click("R|2|1"); ack(SUCCESSFUL,1); assert(rows[1].claimed && grants==2);
}
static void relog_db_failure_preserves_receipt(void) {
    strcpy(disk_receipt,"mail:2"); update_down=1; relog();
    assert(!rows[1].claimed && !view_count && !strcmp(receipt,"mail:2"));
    open_mail(); click("R|1|1"); assert(!grants && !snapshot_count);
    update_down=0; stone_mail_login(7); assert(rows[1].claimed);
    click("T|1"); field(2,"1"); field(4,"1");
}
static void malformed_or_foreign_receipt_is_not_overwritten(void) {
    strcpy(receipt,"existing-activity"); stone_mail_menu(7,118); stone_mail_request(7,2);
    assert(!strcmp(receipt,"existing-activity") && !view_count && !grants && !updates);
    strcpy(disk_receipt,"mail:3"); relog(); open_mail(); click("R|2|1");
    assert(!strcmp(receipt,"mail:3") && !grants && !updates);
}
static void failed_creation_or_receipt_does_not_claim(void) {
    open_mail(); fail_create=1; click("R|2|1"); fail_create=0;
    assert(!receipt[0] && !rows[1].claimed && !snapshot_count);
    fail_place=1; click("R|2|1"); fail_place=0;
    assert(!receipt[0] && !items[1] && bag[CHAR_EQUIPPLACENUM]==-1 && !snapshot_count);
    fail_receipt=1; click("R|2|1");
    assert(!items[2] && bag[CHAR_EQUIPPLACENUM]==-1 && !rows[1].claimed && !popup_count);
}
static void earlier_failure_and_unsolicited_ack_cannot_confirm(void) {
    open_mail(); stone_mail_saved(7,SUCCESSFUL);
    CHAR_charSaveFromConnect(2,FALSE); click("R|2|1"); ack(FAILED,1);
    assert(!rows[1].claimed); ack(SUCCESSFUL,1); assert(rows[1].claimed);
}
static void save_start_failure_recovers_on_autosave(void) {
    open_mail(); fail_save=1; click("R|2|1"); fail_save=0;
    assert(grants==1 && !snapshot_count && !rows[1].claimed && !strcmp(receipt,"mail:2"));
    CHAR_charSaveFromConnect(2,FALSE); ack(SUCCESSFUL,1); assert(rows[1].claimed && grants==1);
}
static void account_and_name_are_byte_exact_sql_literals(void) {
    strcpy(name,"O'Reilly\\A"); strcpy(rows[0].name,name); rows[1].claimed=1;
    open_mail(); field(2,"1"); field(4,"1");
    click("R|1|1"); ack(SUCCESSFUL,1); assert(rows[0].claimed && !grants);
}
static void old_epoch_and_duplicate_ack_do_not_claim(void) {
    open_mail(); click("R|2|1");
    relog(); stone_mail_saved(7,SUCCESSFUL); assert(!rows[1].claimed && !updates);
    open_mail(); click("R|2|1"); ack(SUCCESSFUL,1); assert(rows[1].claimed);
    int before=updates; stone_mail_saved(7,SUCCESSFUL); assert(updates==before);
}
static void changed_selection_refresh_or_reopen_cannot_show_wrong_reward(void) {
    open_mail(); click("R|2|1"); click("R|1|1"); ack(SUCCESSFUL,1);
    assert(rows[1].claimed && !rows[0].claimed && !popup_count && grants==1); field(2,"1");
}
static void refresh_during_pending_claim_does_not_lose_confirmation(void) {
    open_mail(); click("R|2|1"); click("T|1"); ack(SUCCESSFUL,1);
    assert(rows[1].claimed && !popup_count && grants==1); field(2,"1");
}
static void reopen_during_pending_claim_does_not_show_wrong_popup(void) {
    open_mail(); int seq=last_sequence; click("R|2|1"); open_mail(); assert(seq!=last_sequence);
    ack(SUCCESSFUL,1); assert(rows[1].claimed && !popup_count && grants==1); field(2,"1");
}
static void trade_and_battle_block_inventory_actions(void) {
    open_mail(); trade_mode=1; click("R|2|1"); click("D|1|1");
    trade_mode=0; battle_mode=1; click("R|2|1"); click("D|1|1");
    assert(!grants && !snapshot_count && !updates); battle_mode=0;
    click("R|2|1"); ack(SUCCESSFUL,1); assert(grants==1 && rows[1].claimed);
}
static void invalid_delimiters_and_overlong_lists_do_not_shift_fields(void) {
    strcpy(rows[0].title,"bad|title"); open_mail(); field(2,"1"); field(4,"2");
    assert(notices && red_flag); click("R|1|1"); assert(!grants && !snapshot_count);
    for(int n=0;n<110;++n) {
        rows[n]=rows[1]; rows[n].id=100+n; rows[n].item=-1;
        memset(rows[n].body,'x',2047); rows[n].body[2047]=0;
    }
    row_count=110; open_mail(); assert(strlen(last_payload)<MAIL_PAYLOAD_SIZE && red_flag);
    int n=atoi(last_payload+2); assert(n>0 && n<100);
    int delimiters=0; for(const char *p=last_payload;*p;++p) if(*p=='|') ++delimiters;
    assert(delimiters==1+n*9 && !grants);
}
static void displayed_attachment_must_match_current_owned_record(void) {
    open_mail(); rows[0].item=0; click("R|1|1");
    assert(!grants && !snapshot_count && !rows[0].claimed && notices);
    field(5,"小斧头"); click("R|1|1"); ack(SUCCESSFUL,1);
    assert(rows[0].claimed && grants==1 && popup_count==1);
}
static void same_fd_changed_character_cannot_reuse_prior_window(void) {
    open_mail(); int seq=last_sequence;
    strcpy(name,"RoleB");
    assert(stone_mail_window(7,seq,-1,0,"R|2|1")==1);
    assert(stone_mail_window(7,seq,-1,0,"D|1|1")==1);
    assert(!grants && !snapshot_count && !updates);
    open_mail(); field(2,"1"); field(4,"3");
    assert(last_sequence!=seq); click("R|2|1"); assert(!grants);
    click("R|3|1"); ack(SUCCESSFUL,1); assert(rows[2].claimed && !rows[1].claimed && grants==1);
}
int main(void) {
    struct { const char *name; void (*run)(void); } tests[] = {
        {"1026 exact fields and SQL refresh",opens_exact_target_fields_and_refreshes_database},
        {"empty mailbox and red dot",empty_mailbox_opens_and_clears_red_dot},
        {"authenticated native owner",requires_authenticated_native_owner},
        {"stale sequence and strict actions",stale_sequence_wrong_object_and_malformed_actions_are_rejected},
        {"full bag keeps mail",full_bag_keeps_mail},
        {"own snapshot then 808/S then refresh",item_claim_waits_for_its_snapshot_and_popup_confirmation},
        {"notice durable receipt",notice_also_has_durable_receipt},
        {"D never discards unclaimed attachment",delete_preserves_unclaimed_attachment_and_saves_empty_mail},
        {"save failure and later snapshot",failed_save_needs_later_complete_snapshot},
        {"database failure blocks next claim",db_failure_blocks_new_claim_until_confirmed},
        {"relog saved attachment",relog_recovers_saved_attachment_without_regrant},
        {"relog unsaved attachment",relog_without_saved_snapshot_retries_normally},
        {"relog database failure",relog_db_failure_preserves_receipt},
        {"foreign or activity receipt",malformed_or_foreign_receipt_is_not_overwritten},
        {"create/place/receipt failures",failed_creation_or_receipt_does_not_claim},
        {"earlier failed or unsolicited save",earlier_failure_and_unsolicited_ack_cannot_confirm},
        {"save dispatch failure",save_start_failure_recovers_on_autosave},
        {"quoted character ownership",account_and_name_are_byte_exact_sql_literals},
        {"fd epoch and duplicate ACK",old_epoch_and_duplicate_ack_do_not_claim},
        {"another selection during save",changed_selection_refresh_or_reopen_cannot_show_wrong_reward},
        {"refresh during save",refresh_during_pending_claim_does_not_lose_confirmation},
        {"reopen during save",reopen_during_pending_claim_does_not_show_wrong_popup},
        {"trade and battle preserve inventory",trade_and_battle_block_inventory_actions},
        {"delimiter and packet capacity",invalid_delimiters_and_overlong_lists_do_not_shift_fields},
        {"SQL attachment changed after list",displayed_attachment_must_match_current_owned_record},
        {"same fd changes native character",same_fd_changed_character_cannot_reuse_prior_window}
    };
    for(size_t n=0;n<sizeof(tests)/sizeof(tests[0]);++n) {
        pid_t child=fork(); assert(child>=0);
        if(!child) { seed(); tests[n].run(); _exit(0); }
        int status; assert(waitpid(child,&status,0)==child);
        if(!WIFEXITED(status)||WEXITSTATUS(status)) { fprintf(stderr,"FAIL: %s\n",tests[n].name); return 1; }
        printf("PASS: %s\n",tests[n].name);
    }
    return 0;
}
