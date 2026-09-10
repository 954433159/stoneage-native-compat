/* Actual MariaDB SQL + actual app_storage.c; only native character/item/pet
 * memory and SAAC acknowledgments use the existing stateful boundaries.
 *
 * Build from the project in WSL (same prepared native headers as the runner):
 * cc -D_GNU_SOURCE -DLUA_COMPAT_MODULE -ffunction-sections -fdata-sections \
 *   -Inative -Inative/include -Inative/overrides/gmsv \
 *   -Inative/overrides/gmsv/include -Inative/overrides/gmsv/lua \
 *   -I/usr/include/mariadb -Wno-int-conversion -Wno-incompatible-pointer-types \
 *   -fsanitize=address,undefined -fno-sanitize-recover=all \
 *   tests/storage_database.c -Wl,--gc-sections -lmariadb -o evidence/storage-db-test
 *
 * tools/run_feature_database_checks.py owns fixture DB/user creation and
 * cleanup. The temporary user must have access only to the newly created DB.
 * Credentials arrive only through STONE_STORAGE_TEST_DATABASE/USER/PASSWORD
 * and STONE_STORAGE_TEST_SOCKET or PORT. TCP always uses 127.0.0.1. This test
 * requires an empty stoneage_storage_test_* database; it never reads settings,
 * creates/drops databases, accepts an existing game DB, or prints passwords.
 *   python tools/run_feature_database_checks.py storage
 */
#define STONE_STORAGE_REAL_DB 1
#include "app_storage.c"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef NDEBUG
#error "storage_database requires assertions enabled"
#endif
MYSQL mysql;
static char fixture_database[65];
static const char *fixture_user, *fixture_password, *fixture_socket;
static unsigned fixture_port;

/* Only the production idempotent reconnect probe is replaced. Every DDL,
 * transaction, mutation, row decoder and affected-row check remains original. */
int sasql_mysql_query(char *query) { return mysql_query(&mysql,query); }

static void require_sql(const char *query)
{
    if (mysql_query(&mysql,query)) {
        fprintf(stderr,"storage SQL fixture failed, MariaDB error %u\n",mysql_errno(&mysql));
        assert(0);
    }
}

static void connect_fixture(int select_database)
{
    assert(mysql_init(&mysql));
    unsigned timeout=8,protocol=fixture_socket ? MYSQL_PROTOCOL_SOCKET : MYSQL_PROTOCOL_TCP;
    assert(!mysql_options(&mysql,MYSQL_OPT_CONNECT_TIMEOUT,&timeout));
    assert(!mysql_options(&mysql,MYSQL_OPT_READ_TIMEOUT,&timeout));
    assert(!mysql_options(&mysql,MYSQL_OPT_WRITE_TIMEOUT,&timeout));
    assert(!mysql_options(&mysql,MYSQL_OPT_PROTOCOL,&protocol));
    if (!mysql_real_connect(&mysql,fixture_socket ? "localhost" : "127.0.0.1",
                            fixture_user,fixture_password,NULL,fixture_port,fixture_socket,0)) {
        fprintf(stderr,"local fixture connection failed, MariaDB error %u\n",mysql_errno(&mysql));
        exit(2);
    }
    assert(!mysql_set_character_set(&mysql,"utf8mb4"));
    require_sql("SET SESSION sql_mode='STRICT_ALL_TABLES,NO_ENGINE_SUBSTITUTION'");
    require_sql("SET SESSION innodb_lock_wait_timeout=5");
    if (select_database) {
        assert(!strncmp(fixture_database,"stoneage_storage_test_",22));
        assert(!mysql_select_db(&mysql,fixture_database));
        require_sql("SELECT DATABASE()");
        MYSQL_RES *res=mysql_store_result(&mysql);assert(res);
        MYSQL_ROW row=mysql_fetch_row(res);
        assert(row && row[0] && !strcmp(row[0],fixture_database));
        mysql_free_result(res);
    }
}

static int sql_number(const char *query)
{
    int value=-1;assert(storage_scalar(query,&value)==1);return value;
}

static int row_state(int id)
{
    StorageRow row;assert(storage_db_find(id,&row)==1);return row.state;
}

static void create_fixture_tables(void)
{
    /* Minimal required legacy columns, all newly created and empty. These are
     * fixtures, never clones/readbacks of a user's csalogin or character data. */
    require_sql("CREATE TABLE csalogin (Name VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin PRIMARY KEY,"
                "MasterId INT NOT NULL,VipPoint INT NOT NULL) ENGINE=InnoDB");
    require_sql("CREATE TABLE pooldata (cdkey VARCHAR(16) PRIMARY KEY,petnum INT NOT NULL,itemnum INT NOT NULL) ENGINE=InnoDB");
    require_sql("CREATE TABLE poolitem (cdkey VARCHAR(16) NOT NULL) ENGINE=InnoDB");
    require_sql("CREATE TABLE poolpet (cdkey VARCHAR(16) NOT NULL) ENGINE=InnoDB");
    /* Column order/types match runtime/schema.sql:2173, including no id. */
    require_sql("CREATE TABLE vippointlog (cdkey VARCHAR(16) NOT NULL,"
                "point INT NOT NULL,oldpoint INT NOT NULL,newpoint INT NOT NULL,buff VARCHAR(128) NOT NULL,"
                "time TIMESTAMP NULL DEFAULT NULL) ENGINE=InnoDB");
    require_sql("INSERT INTO csalogin VALUES('fixture-a',42,1000),('fixture-b',42,1000)");
    assert(storage_db_ready());
    storage_schema_ready=0;assert(storage_db_ready()); /* real idempotent DDL */
    assert(sql_number("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() "
                      "AND table_name IN('local_storage_entries','local_storage_capacity') AND ENGINE='InnoDB'")==2);
}

static void reset_database(void)
{
    require_sql("DELETE FROM local_storage_entries");
    require_sql("ALTER TABLE local_storage_entries AUTO_INCREMENT=1");
    require_sql("DELETE FROM local_storage_capacity");
    require_sql("DELETE FROM pooldata");require_sql("DELETE FROM poolitem");require_sql("DELETE FROM poolpet");
    require_sql("DELETE FROM vippointlog");
    require_sql("UPDATE csalogin SET MasterId=42,VipPoint=1000");
    reset();
}

static StorageSession session(int fd,int kind,int shared)
{
    if (shared) stone_storage_menu(fd,kind==STORAGE_ITEM ? 35 : 36);
    else stone_storage_personal(fd,kind,"O|");
    StorageSession *s=storage_session(fd);
    assert(s && !s->blocked && s->kind==kind && s->shared==shared);
    return *s;
}

static StorageRow asset_row(StorageSession *s,const char *uid)
{
    StorageRow r={0};r.kind=s->kind;r.state=STORE_DEPOSIT;
    snprintf(r.scope,sizeof(r.scope),"%s",s->scope);
    snprintf(r.depositor,sizeof(r.depositor),"%s",s->account);
    snprintf(r.depname,sizeof(r.depname),"%s",s->name);
    snprintf(r.uid,sizeof(r.uid),"%s",uid);
    /* Intentionally includes quotes, backslashes, newlines and UTF-8 bytes.
     * SQL-only journal tests do not try to deserialize this synthetic blob. */
    strcpy(r.blob,"native=fixture\nname=仓库'对象\\line|装备=原值\n");
    snprintf(r.summary,sizeof(r.summary),"仓库'对象|1|%s|",uid);
    strcpy(r.detail,"字段'|中文\\x|完整\n");
    return r;
}

static void test_sql_capacity(void)
{
    reset_database();StorageSession pet=session(7,STORAGE_PET,0);int capacity=0;
    assert(storage_db_capacity(&pet,&capacity)==1 && capacity==20);
    require_sql("INSERT INTO pooldata(cdkey,itemnum,petnum) VALUES('fixture-a',15,10)");
    require_sql("UPDATE local_storage_capacity SET capacity=10 WHERE kind=1");
    assert(storage_db_capacity(&pet,&capacity)==1 && capacity==20);
    assert(sql_number("SELECT petnum FROM pooldata WHERE cdkey='fixture-a'")==10);
    require_sql("UPDATE local_storage_capacity SET capacity=25 WHERE kind=1");
    assert(storage_db_capacity(&pet,&capacity)==1 && capacity==25);
    require_sql("UPDATE pooldata SET petnum=27 WHERE cdkey='fixture-a'");
    assert(storage_db_capacity(&pet,&capacity)==1 && capacity==27);
    require_sql("UPDATE local_storage_capacity SET capacity=9 WHERE kind=1");
    assert(storage_db_capacity(&pet,&capacity)==-1); /* corrupt capacity is not silently accepted */
    require_sql("UPDATE local_storage_capacity SET capacity=27 WHERE kind=1");
    require_sql("INSERT INTO poolpet VALUES('fixture-a')");
    assert(storage_db_capacity(&pet,&capacity)==0); /* old assets require explicit migration */
    assert(sql_number("SELECT COUNT(*) FROM poolpet")==1);
    StorageSession item=session(8,STORAGE_ITEM,0);
    assert(storage_db_capacity(&item,&capacity)==1 && capacity==20);
    require_sql("INSERT INTO pooldata(cdkey,itemnum,petnum) VALUES('fixture-b',15,10)");
    require_sql("UPDATE local_storage_capacity SET capacity=15 WHERE kind=2");
    assert(storage_db_capacity(&item,&capacity)==1 && capacity==20);
    assert(sql_number("SELECT itemnum FROM pooldata WHERE cdkey='fixture-b'")==15);
    require_sql("UPDATE local_storage_capacity SET capacity=25 WHERE kind=2");
    assert(storage_db_capacity(&item,&capacity)==1 && capacity==25);
    require_sql("UPDATE pooldata SET itemnum=29 WHERE cdkey='fixture-b'");
    assert(storage_db_capacity(&item,&capacity)==1 && capacity==29);
    StorageSession shared_item=session(8,STORAGE_ITEM,1);
    require_sql("UPDATE local_storage_capacity SET capacity=50 WHERE scope='S:42' AND kind=2");
    assert(storage_db_capacity(&shared_item,&capacity)==1 && capacity==100);
    StorageSession shared=session(8,STORAGE_PET,1);
    assert(storage_db_capacity(&shared,&capacity)==1 && capacity==80);
    require_sql("UPDATE local_storage_capacity SET capacity=30 WHERE scope='S:42' AND kind=1");
    assert(storage_db_capacity(&shared,&capacity)==1 && capacity==80);
    for (int n=0;n<80;++n) {
        char uid[32];snprintf(uid,sizeof(uid),"sql-shared-pet-%d",n);
        StorageRow row=asset_row(&shared,uid);assert(storage_db_deposit(&shared,&row)==1);
        assert(storage_db_transition(&shared,&row,STORE_AVAILABLE)==1);
    }
    char list[STORAGE_PAYLOAD_SIZE];int count=0;
    assert(storage_db_list(&shared,list,sizeof(list),&count)==1 && count==80);
    StorageRow excess=asset_row(&shared,"sql-pet-overflow");assert(storage_db_deposit(&shared,&excess)==0);
    assert(storage_db_master(&shared)==42);
    strcpy(shared.account,"absent-fixture");assert(storage_db_master(&shared)==-1);
    assert(sql_number("SELECT 2147483647")==INT_MAX);
    puts("PASS storage SQL: schema, authenticated capacity inheritance, base 20 and legacy preservation");
}

static void test_sql_journal(void)
{
    reset_database();StorageSession a=session(7,STORAGE_ITEM,1),b=session(8,STORAGE_ITEM,1);
    StorageRow original=asset_row(&a,"sql-quote-i"),read;
    assert(storage_db_deposit(&a,&original)==1 && original.id>0);
    assert(storage_db_find(original.id,&read)==1 && read.state==STORE_DEPOSIT);
    assert(!strcmp(read.blob,original.blob) && !strcmp(read.summary,original.summary) && !strcmp(read.detail,original.detail));
    assert(!strcmp(read.depname,"角色A") && !strcmp(read.depositor,"fixture-a"));
    assert(storage_db_pending(&a,&read)==1 && storage_db_pending(&b,&read)==0);
    assert(storage_db_uid(&b,original.uid,&read)==0); /* unacknowledged deposit is not published */
    StorageRow second=asset_row(&a,"sql-second-i");
    assert(storage_db_deposit(&a,&second)==-1); /* one pending role UNIQUE */
    assert(sql_number("SELECT COUNT(*) FROM local_storage_entries")==1);
    assert(storage_db_transition(&b,&original,STORE_AVAILABLE)==0);
    assert(storage_db_transition(&a,&original,STORE_AVAILABLE)==1);
    assert(storage_db_transition(&a,&original,STORE_AVAILABLE)==0);
    assert(storage_db_find(original.id,&read)==1 && read.state==STORE_AVAILABLE);
    StorageRow duplicate=asset_row(&b,"sql-quote-i");assert(storage_db_deposit(&b,&duplicate)==-1);
    StorageSession outsider=b;strcpy(outsider.scope,"S:99");
    assert(storage_db_uid(&outsider,original.uid,&duplicate)==0);
    assert(storage_db_withdraw(&outsider,&read)==0);
    char list[2048];int count=-1;assert(storage_db_list(&b,list,sizeof(list),&count)==1 && count==1);
    assert(!strcmp(list,original.summary));
    assert(storage_db_withdraw(&b,&read)==1);
    assert(storage_db_withdraw(&a,&read)==0); /* a stale available row cannot win twice */
    assert(storage_db_find(original.id,&read)==1 && read.state==STORE_WITHDRAW);
    assert(storage_db_transition(&a,&read,STORE_CONSUMED)==0);
    assert(storage_db_transition(&b,&read,STORE_AVAILABLE)==1); /* restore failure rollback */
    assert(storage_db_find(original.id,&read)==1 && read.state==STORE_AVAILABLE && !read.withdrawer[0]);
    assert(storage_db_withdraw(&b,&read)==1);
    assert(storage_db_find(original.id,&read)==1);
    assert(storage_db_transition(&b,&read,STORE_CONSUMED)==1);
    assert(sql_number("SELECT COUNT(*) FROM local_storage_entries WHERE active_uid IS NULL AND pending_owner IS NULL")==1);
    duplicate=asset_row(&b,"sql-quote-i");assert(storage_db_deposit(&b,&duplicate)==1); /* UID reusable after completion */
    assert(storage_db_transition(&b,&duplicate,STORE_CANCELLED)==1);
    StorageRow bound=asset_row(&a,"sql-bound-i");bound.bound=1;
    assert(storage_db_deposit(&a,&bound)==1 && storage_db_transition(&a,&bound,STORE_AVAILABLE)==1);
    assert(storage_db_find(bound.id,&bound)==1);
    assert(storage_db_withdraw(&b,&bound)==0 && storage_db_withdraw(&a,&bound)==1);
    puts("PASS storage SQL: all 13 binary columns, ownership, unique pending/UID, publication and atomic consumption");
}

static void test_sql_native_ack(void)
{
    reset_database();give_item(7,9,"sql-native-i");session(7,STORAGE_ITEM,1);session(8,STORAGE_ITEM,1);
    CHAR_charSaveFromConnect(2,FALSE);request(7,"I|9");
    assert(row_state(1)==STORE_DEPOSIT && CHAR_getItemIndex(2,9)==-1);
    ack(SUCCESSFUL);assert(row_state(1)==STORE_DEPOSIT); /* earlier autosave is not this receipt */
    ack("failed");assert(row_state(1)==STORE_DEPOSIT);
    CHAR_charSaveFromConnect(2,FALSE);ack(SUCCESSFUL);assert(row_state(1)==STORE_AVAILABLE);
    request(8,"B|sql-native-i");assert(row_state(1)==STORE_WITHDRAW && CHAR_getItemIndex(3,9)>0);
    request(7,"B|sql-native-i");assert(CHAR_getItemIndex(2,9)==-1);
    ack(SUCCESSFUL);assert(row_state(1)==STORE_CONSUMED && !stone_storage_busy(8));
    reset_database();characters[2].data[CHAR_LV]=characters[3].data[CHAR_LV]=4;
    events[0]=events[1]=0;give_pet(7,0,"sql-resting-pet",1);
    session(7,STORAGE_PET,0);request(7,"C|0|");assert(row_state(1)==STORE_DEPOSIT);
    ack(SUCCESSFUL);request(7,"B|sql-resting-pet|");ack(SUCCESSFUL);assert(row_state(1)==STORE_CONSUMED);
    session(7,STORAGE_PET,1);request(7,"I|0");ack(SUCCESSFUL);
    session(8,STORAGE_PET,1);request(8,"B|sql-resting-pet");ack(SUCCESSFUL);
    assert(row_state(2)==STORE_CONSUMED && CHAR_getCharPet(3,0)>=5);
    assert(!strcmp(CHAR_getChar(CHAR_getCharPet(3,0),CHAR_OWNERCDKEY),"fixture-b"));
    puts("PASS storage SQL: native fixture assets + real journal + ordered SAAC ACK, Lv4 pet storage");
}

enum { RACE_DEPOSIT, RACE_WITHDRAW, RACE_EXPAND };
typedef struct { int status,id; } RaceResult;

static void transfer_fd(int fd,void *data,size_t size,int writing)
{
    size_t used=0;
    while (used<size) {
        ssize_t n=writing ? write(fd,(char*)data+used,size-used) : read(fd,(char*)data+used,size-used);
        if (n<0 && errno==EINTR) continue;
        assert(n>0);used+=(size_t)n;
    }
}

static void race_two(int operation,StorageSession actors[2],StorageRow assets[2],RaceResult results[2])
{
    int ready[2][2],go[2][2],output[2][2];pid_t children[2];
    for (int n=0;n<2;++n) assert(!pipe(ready[n]) && !pipe(go[n]) && !pipe(output[n]));
    /* Fork with no live MariaDB connection, so no child closes/uses another
     * process's socket. Each actor opens a truly independent transaction. */
    mysql_close(&mysql);fflush(NULL);
    for (int n=0;n<2;++n) {
        children[n]=fork();assert(children[n]>=0);
        if (!children[n]) {
            for (int k=0;k<2;++k) {
                close(ready[k][0]);close(go[k][1]);close(output[k][0]);
                if (k!=n) { close(ready[k][1]);close(go[k][0]);close(output[k][1]); }
            }
            connect_fixture(1);
            if (operation==RACE_WITHDRAW) {
                assert(storage_db_find(assets[n].id,&assets[n])==1 && assets[n].state==STORE_AVAILABLE);
            }
            char byte=1;transfer_fd(ready[n][1],&byte,1,1);transfer_fd(go[n][0],&byte,1,0);
            int ret=operation==RACE_DEPOSIT ? storage_db_deposit(&actors[n],&assets[n]) :
                operation==RACE_WITHDRAW ? storage_db_withdraw(&actors[n],&assets[n]) :
                storage_db_expand(&actors[n],20,300);
            RaceResult result={ret,assets[n].id};transfer_fd(output[n][1],&result,sizeof(result),1);
            mysql_close(&mysql);_exit(0);
        }
    }
    for (int n=0;n<2;++n) { close(ready[n][1]);close(go[n][0]);close(output[n][1]); }
    char byte=0;for (int n=0;n<2;++n) transfer_fd(ready[n][0],&byte,1,0);
    for (int n=0;n<2;++n) transfer_fd(go[n][1],&byte,1,1);
    for (int n=0;n<2;++n) {
        transfer_fd(output[n][0],&results[n],sizeof(results[n]),0);
        int status;assert(waitpid(children[n],&status,0)==children[n]);assert(WIFEXITED(status) && !WEXITSTATUS(status));
        close(ready[n][0]);close(go[n][1]);close(output[n][0]);
        assert(results[n].status==0 || results[n].status==1);
    }
    connect_fixture(1);
    assert(results[0].status+results[1].status==1);
}

static void test_sql_competition(void)
{
    reset_database();StorageSession actors[2]={session(7,STORAGE_ITEM,1),session(8,STORAGE_ITEM,1)};
    /* Leave one real shared slot: both actors must count all 99 live rows,
     * including reservations, under the production capacity-row lock. */
    for (int n=0;n<99;++n) {
        char uid[32];snprintf(uid,sizeof(uid),"sql-fill-%d",n);
        StorageRow r=asset_row(&actors[0],uid);assert(storage_db_deposit(&actors[0],&r)==1);
        assert(storage_db_transition(&actors[0],&r,STORE_AVAILABLE)==1);
    }
    StorageRow assets[2]={asset_row(&actors[0],"sql-race-a"),asset_row(&actors[1],"sql-race-b")};
    RaceResult results[2];race_two(RACE_DEPOSIT,actors,assets,results);
    assert(sql_number("SELECT COUNT(*) FROM local_storage_entries WHERE state IN(0,1,2)")==100);
    assert(sql_number("SELECT COUNT(*) FROM local_storage_entries WHERE state=0")==1);
    int deposited=results[0].status ? 0 : 1;StorageRow deposited_row;
    assert(storage_db_find(results[deposited].id,&deposited_row)==1);
    assert(storage_db_transition(&actors[deposited],&deposited_row,STORE_AVAILABLE)==1);
    char list[STORAGE_PAYLOAD_SIZE];int count=0;
    assert(storage_db_list(&actors[0],list,sizeof(list),&count)==1 && count==100);
    StorageRow excess=asset_row(&actors[0],"sql-item-overflow");assert(storage_db_deposit(&actors[0],&excess)==0);
    reset_database();actors[0]=session(7,STORAGE_ITEM,1);actors[1]=session(8,STORAGE_ITEM,1);
    StorageRow r=asset_row(&actors[0],"sql-race-take");assert(storage_db_deposit(&actors[0],&r)==1);
    assert(storage_db_transition(&actors[0],&r,STORE_AVAILABLE)==1);assets[0]=assets[1]=r;
    race_two(RACE_WITHDRAW,actors,assets,results);
    assert(storage_db_find(r.id,&r)==1 && r.state==STORE_WITHDRAW);
    int winner=results[0].status ? 0 : 1;
    assert(!strcmp(r.withdrawer,actors[winner].account));
    assert(storage_db_transition(&actors[1-winner],&r,STORE_CONSUMED)==0);
    assert(storage_db_transition(&actors[winner],&r,STORE_CONSUMED)==1);
    reset_database();actors[0]=session(7,STORAGE_ITEM,0);actors[1]=actors[0];
    race_two(RACE_EXPAND,actors,assets,results);
    assert(sql_number("SELECT capacity FROM local_storage_capacity WHERE kind=2")==21);
    assert(sql_number("SELECT VipPoint FROM csalogin WHERE Name='fixture-a'")==700);
    assert(sql_number("SELECT COUNT(*) FROM vippointlog WHERE point=-300 AND oldpoint=1000 AND newpoint=700")==1);
    puts("PASS storage SQL: two real connections competing for last slot, same UID and same expansion");
}

static void test_sql_expansion_rollback(void)
{
    reset_database();StorageSession s=session(7,STORAGE_ITEM,0);
    require_sql("CREATE TRIGGER fixture_fail_log BEFORE INSERT ON vippointlog FOR EACH ROW "
                "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture forced rollback'");
    assert(storage_db_expand(&s,20,300)==-1);
    assert(sql_number("SELECT capacity FROM local_storage_capacity WHERE kind=2")==20);
    assert(sql_number("SELECT VipPoint FROM csalogin WHERE Name='fixture-a'")==1000);
    assert(sql_number("SELECT COUNT(*) FROM vippointlog")==0);
    require_sql("DROP TRIGGER fixture_fail_log");
    require_sql("UPDATE csalogin SET VipPoint=299 WHERE Name='fixture-a'");
    assert(storage_db_expand(&s,20,300)==0);
    assert(sql_number("SELECT capacity FROM local_storage_capacity WHERE kind=2")==20);
    require_sql("UPDATE csalogin SET VipPoint=300 WHERE Name='fixture-a'");
    assert(storage_db_expand(&s,20,300)==1 && storage_db_expand(&s,20,300)==0);
    assert(sql_number("SELECT VipPoint FROM csalogin WHERE Name='fixture-a'")==0);
    assert(sql_number("SELECT COUNT(*) FROM vippointlog WHERE point=-300 AND oldpoint=300 AND newpoint=0")==1);
    reset_database();session(7,STORAGE_PET,0);
    assert(!strcmp(last_payload[0],"L|0|20|500|"));request(7,"H|");
    assert(sql_number("SELECT capacity FROM local_storage_capacity WHERE kind=1")==21);
    assert(sql_number("SELECT VipPoint FROM csalogin WHERE Name='fixture-a'")==500);
    assert(sql_number("SELECT COUNT(*) FROM vippointlog WHERE point=-500 AND oldpoint=1000 AND newpoint=500")==1);
    puts("PASS storage SQL: fee/capacity/log atomic rollback and insufficient funds");
}

int main(int argc,char **argv)
{
    if (argc!=1) {
        fprintf(stderr,"Usage: storage_database\n"
                "Requires STONE_STORAGE_TEST_DATABASE/USER/PASSWORD and local SOCKET or PORT environment.\n"
                "Only accepts an empty stoneage_storage_test_* database with a restricted fixture user.\n");
        return argc==2 && !strcmp(argv[1],"--help") ? 0 : 2;
    }
    const char *database=getenv("STONE_STORAGE_TEST_DATABASE");
    fixture_user=getenv("STONE_STORAGE_TEST_USER");fixture_password=getenv("STONE_STORAGE_TEST_PASSWORD");
    fixture_socket=getenv("STONE_STORAGE_TEST_SOCKET");const char *port=getenv("STONE_STORAGE_TEST_PORT");
    if (!database || strncmp(database,"stoneage_storage_test_",22) || strlen(database)<=22 || strlen(database)>64 ||
        !fixture_user || !*fixture_user || !fixture_password ||
        (fixture_socket && fixture_socket[0]!='/') || (!fixture_socket && (!port || !*port))) {
        fprintf(stderr,"Missing or invalid isolated fixture database/local credentials/socket/port.\n");return 2;
    }
    for (const unsigned char *p=(const unsigned char*)database;*p;++p)
        if (!(*p>='a' && *p<='z') && !(*p>='0' && *p<='9') && *p!='_') {
            fprintf(stderr,"Invalid isolated fixture database name.\n");return 2;
        }
    snprintf(fixture_database,sizeof(fixture_database),"%s",database);
    if (!fixture_socket) { int number;assert(storage_number(port,1,&number) && number<=65535);fixture_port=(unsigned)number; }
    connect_fixture(1);
    if (sql_number("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE()") ||
        sql_number("SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE()") ||
        sql_number("SELECT COUNT(*) FROM information_schema.events WHERE event_schema=DATABASE()")) {
        fprintf(stderr,"Refusing a nonempty fixture database.\n");mysql_close(&mysql);return 2;
    }
    create_fixture_tables();test_integer_boundary();
    test_sql_capacity();test_sql_journal();test_sql_native_ack();test_sql_competition();test_sql_expansion_rollback();
    reset();mysql_close(&mysql);
    puts("PASS: production storage SQL on isolated MariaDB fixture; cleanup belongs to the root driver");return 0;
}
