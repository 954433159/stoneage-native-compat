#include <assert.h>
#include "../native/app_control.c"
#include "lauxlib.h"
#include "lualib.h"
#include "../build/app_logout_function.inc"

/* Compile the real adapter and execute the original offline.lua. Only its
 * native I/O boundaries use stateful fixtures; no gameplay algorithm is copied. */
MYSQL mysql;
int acfd = 3, ConnectLen = 128;
char PersonalKey[4096];
static int connected[128], epochs[128], states[128], indexes[128], close_requests[128];
static char accounts[128][CDKEYLEN];
static int alive[32], workfds[32], modes[32], battles[32], parties[32], leaders[32];
static int floors[32], xs[32], ys[32], offline[32], vitality[32], offline_time[32];
static int login_time[32], last_leave[32], npc_time[32], timers[32], loop_time[32], encounters[32];
static char loop_name[32][16];
static int loads, lists, saves, closes, commands, command_fd, exits, wars, joins, auto_calls;
static int packet_fd, packet_type, values[4], value_count, rosters;
static int sql_calls, sql_row, sql_failed, next_fd = 20, sql_offline, lua_players, stopped, notices;
static int new_battles, blocked_line, busy_account;
static int feature_busy_fd = -1, logout_ack;
static int snapshot_counts[32];
static char character_accounts[32][CDKEYLEN], character_names[32][CHARNAMELEN];
static int trades[32], street_busy[32], account1_online;
static int reconnect_calls, reconnect_target = -1, reconnect_reject;
static char payload[4096], roster[4096], sql_query_text[1024];
static char *sql_rows[5][5] = {
    {"account1", "fixture-secret", "0", "501", "10042"}, {"account2", "fixture-secret", "1", "502", "10042"},
    {"account3", "fixture-secret", "0", "503", "10042"}, {"account4", "fixture-secret", "0", "504", "10042"},
    {"account5", "fixture-secret", "0", "505", "10042"}
};
static lua_State *fixture_lua;
static time_t fixture_now = 100000;
time_t time(time_t *out) { if (out) *out = fixture_now; return fixture_now; }

int CONNECT_checkfd(int fd) { return fd >= 0 && fd < 128 && connected[fd]; }
int CONNECT_getFdid(int fd) { assert(fd >= 0 && fd < 128); return epochs[fd]; }
BOOL CONNECT_isLOGIN(int fd) { return states[fd] == LOGIN; }
BOOL CONNECT_isCLI(int fd) { return 1; }
int CONNECT_getloginmark(int fd) { return 4; }
int CONNECT_getCharaindex(int fd) { return indexes[fd]; }
void CONNECT_setState(int fd, int state) { states[fd] = state; }
void CONNECT_setCharaindex(int fd, int index) { indexes[fd] = index; }
void CONNECT_setCtype(int fd, int type) {}
void CONNECT_setCdkey(int fd, char *s) { snprintf(accounts[fd], CDKEYLEN, "%s", s); }
void CONNECT_setPasswd(int fd, char *s) {}
void CONNECT_setloginmark(int fd, int p) {}
void CONNECT_setCharname(int fd, char *s) {}
void CONNECT_setCloseRequest(int fd, int value) { close_requests[fd] = value; }
void CONNECT_getMAC(int fd, char *out, int size) { snprintf(out, size, "fixture-device"); }
void CONNECT_getMAC2(int fd, char *out, int size) { snprintf(out, size, "fixture-version"); }
void CONNECT_getMAC3(int fd, char *out, int size) { snprintf(out, size, "fixture-platform"); }
void CONNECT_setMAC(int fd, char *s) {}
void CONNECT_setMAC2(int fd, char *s) {}
void CONNECT_setMAC3(int fd, char *s) {}
int stone_features_busy(int fd) { return fd == feature_busy_fd; }
void stone_mail_login(int fd) {}
void stone_storage_login(int fd) {}
void stone_storage_logout(int fd) {}
void stone_market_logout(int fd) {}
void stone_attributes_logout(int fd) {}
void lssproto_CharLogout_send(int fd, char *result, char *data) { logout_ack += !strcmp(result, "successful") ? 1 : -1; }
BOOL CHAR_talkToCli(int index, int npc, char *s, CHAR_COLOR color) { ++notices; return TRUE; }
void CONNECT_getCdkey(int fd, char *s, int capacity) { snprintf(s, capacity, "%s", accounts[fd]); }
char *getRandDefaultKey(int fd) { return "fixture"; }
int getfdFromCdkey(char *s) {
    if (busy_account && !strcmp(s, "account1")) return 99;
    for (int fd = 0; fd < 128; ++fd) if (connected[fd] && !strcmp(s, accounts[fd])) return fd;
    return -1;
}
int eventfd(unsigned int value, int flags) { assert(value == 0); return next_fd++; }
BOOL initConnectOne(int fd, struct sockaddr_in *s, int len) {
    connected[fd] = 1; epochs[fd]++; indexes[fd] = -1; return 1;
}
BOOL _CONNECT_endOne(char *file, int line, int fd, int from) {
    assert(indexes[fd] < 0 || !alive[indexes[fd]]);
    connected[fd] = 0; ++closes; return 1;
}
unsigned long mysql_real_escape_string(MYSQL *db, char *out, const char *in, unsigned long size) {
    memcpy(out, in, size); out[size] = 0; return size;
}
int mysql_query(MYSQL *db, const char *query) {
    ++sql_calls; snprintf(sql_query_text, sizeof(sql_query_text), "%s", query); return sql_failed;
}
MYSQL_RES *mysql_store_result(MYSQL *db) { sql_row = 0; return (MYSQL_RES *)sql_rows; }
MYSQL_ROW mysql_fetch_row(MYSQL_RES *rows) { return sql_row < 5 ? sql_rows[sql_row++] : NULL; }
void mysql_free_result(MYSQL_RES *rows) {}

BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return index >= 0 && index < 32 && alive[index]; }
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    if (field == CHAR_WORKFD) return workfds[index];
    if (field == CHAR_WORKBATTLEMODE) return modes[index];
    if (field == CHAR_WORKBATTLEINDEX) return battles[index];
    if (field == CHAR_WORKPARTYMODE) return parties[index];
    if (field == CHAR_WORKPARTYINDEX1) return leaders[index];
    if (field == CHAR_WORK_OFFLINE) return offline[index];
    if (field == CHAR_WORKTRADEMODE) return trades[index];
    if (field == CHAR_WORKSTREETVENDOR) return street_busy[index] ? 1 : -1;
    return 0;
}
BOOL _CHAR_setWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field, int value) {
    if (field == CHAR_WORKFD) workfds[index] = value;
    else if (field == CHAR_WORK_OFFLINE) offline[index] = value;
    else if (field == CHAR_WORK_LOGIN_TIME) login_time[index] = value;
    else assert(0);
    return TRUE;
}
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    if (field == CHAR_WHICHTYPE) return index == 20 ? CHAR_TYPEENEMY : CHAR_TYPEPLAYER;
    if (field == CHAR_LV) return index;
    if (field == CHAR_FACEIMAGENUMBER) return 23;
    if (field == CHAR_TRANSMIGRATION) return 2;
    if (field == CHAR_FLOOR) return floors[index];
    if (field == CHAR_X) return xs[index];
    if (field == CHAR_Y) return ys[index];
    return 0;
}
char *CHAR_getUseName(int index) { return index == 11 ? "Primary" : "Companion"; }
char *CHAR_makeStatusString(int index, char *category) { return "Pfixture"; }
char *_CHAR_getChar(char *file, int line, int index, CHAR_DATACHAR field) {
    if (field == CHAR_CDKEY && character_accounts[index][0]) return character_accounts[index];
    if (field == CHAR_NAME && character_names[index][0]) return character_names[index];
    return "uid";
}
int CHAR_getPlayerMaxNum(void) { return 32; }
BOOL _CHAR_sendStatusString(int index, char *category, char *file, int line) { ++snapshot_counts[index]; return 1; }
BOOL CHAR_charSaveFromConnect(int index, BOOL unlock) { ++saves; stone_app_saving(workfds[index]); return 1; }
BOOL _CHAR_logout(char *file, int line, int index, BOOL save) {
    assert(alive[index]);
    int fd = workfds[index];
    stone_app_disconnect(fd);
    CHAR_charSaveFromConnect(index, save);
    if (offline[index] == 1) --lua_players;
    alive[index] = 0;
    stone_app_logged_out(fd);
    return 1;
}
BOOL _CHAR_warpToSpecificPoint(char *file, int line, int index, int floor, int x, int y) {
    ++wars; floors[index] = floor; xs[index] = x; ys[index] = y; return 1;
}
BOOL CHAR_JoinParty_Main(int index, int owner) {
    ++joins; parties[index] = CHAR_PARTY_CLIENT; leaders[index] = owner; return 1;
}
BOOL BATTLE_CHECKINDEX(int index) { return index == 2; }
int BATTLE_No2Index(int battle, int position) { return position < 5 ? 10 + position : 20; }
void BattleCommandDispach(int fd, char *command) { ++commands; command_fd = fd; }
void BattleEncountOut(int index) { ++exits; modes[index] = BATTLE_CHARMODE_NONE; }
int sasql_queryonline(char *s) { return !strcmp(s, "account1") ? account1_online : 0; }
/* Native CharLogin is an I/O boundary here. The adapter must select and
 * authorize the existing object before entering it; this fixture never
 * searches for characters or performs the adapter's eligibility checks. */
void lssproto_CharLogin_recv(int fd, char *name) {
    ++reconnect_calls;
    assert(reconnect_target >= 0 && alive[reconnect_target]);
    assert(states[fd] == NOTLOGIN && stone_app_virtual(fd));
    assert(!strcmp(accounts[fd], character_accounts[reconnect_target]));
    assert(!strcmp(name, character_names[reconnect_target]));
    assert(workfds[reconnect_target] == -1 && offline[reconnect_target] == 2);
    if (reconnect_reject) return;
    indexes[fd] = reconnect_target;
    workfds[reconnect_target] = fd;
    states[fd] = LOGIN;
    offline[reconnect_target] = 0;
}
void saacproto_ACCharList_send(int fd, char *a, char *p, char *ip, char *m1, char *m2, char *m3, int id, int flag) {
    assert(flag == 1); ++lists;
}
void saacproto_ACCharLoad_send(int fd, char *a, char *p, char *name, int lock, char *opt, int id) {
    assert(lock == 1 && !strcmp(opt, "stone-control")); ++loads;
}
int stone_app_role_roster(int fd) { ++rosters; return stone_app_control_roster(fd, roster, sizeof(roster)); }
void stone_app_auto_send(int fd, int slot, int mode) { ++auto_calls; }
char *makeEscapeString(char *in, char *out, int cap) { snprintf(out, cap, "%s", in); return out; }
#include "../build/app_escape_function.inc"
BOOL getStringFromIndexWithDelim_body(char *in, char *delimiter, int index, char *out, int capacity, char *file, int line) {
    for (int n = 1; n < index; ++n) { in = strchr(in, '|'); if (!in) return 0; ++in; }
    size_t len = strcspn(in, "|"); if (len >= (size_t)capacity) return 0;
    memcpy(out, in, len); out[len] = 0; return 1;
}
int util_mkint(int fd, char *buffer, int value) { assert(value_count < 4); values[value_count++] = value; return value; }
int util_mkstring(int fd, char *buffer, char *value) { strcpy(payload, value); return strlen(value); }
void _util_SendMesg(char *file, int line, int fd, int protocol, char *buffer) {
    packet_fd = fd; packet_type = protocol; value_count = 0;
}

static int *lua_integer(lua_State *lua, int work) {
    int index = luaL_checkinteger(lua, 1);
    const char *field = luaL_checkstring(lua, 2);
    assert(index >= 0 && index < 32);
    if (work) {
        if (!strcmp(field, "离线")) return &offline[index];
        if (!strcmp(field, "组队")) return &parties[index];
        if (!strcmp(field, "战斗")) return &modes[index];
        if (!strcmp(field, "登陆时间")) return &login_time[index];
        if (!strcmp(field, "NPC临时1")) return &npc_time[index];
        if (!strcmp(field, "计时器")) return &timers[index];
    } else {
        if (!strcmp(field, "活力")) return &vitality[index];
        if (!strcmp(field, "地图号")) return &floors[index];
        if (!strcmp(field, "下线时间")) return &last_leave[index];
        if (!strcmp(field, "离线时间")) return &offline_time[index];
        if (!strcmp(field, "循环事件时间")) return &loop_time[index];
    }
    assert(!"unexpected original Lua field"); return NULL;
}
static int lua_get_int(lua_State *lua) { lua_pushinteger(lua, *lua_integer(lua, 0)); return 1; }
static int lua_get_work(lua_State *lua) { lua_pushinteger(lua, *lua_integer(lua, 1)); return 1; }
static int lua_set_int(lua_State *lua) { *lua_integer(lua, 0) = luaL_checkinteger(lua, 3); return 0; }
static int lua_set_work(lua_State *lua) { *lua_integer(lua, 1) = luaL_checkinteger(lua, 3); return 0; }
static int lua_get_char(lua_State *lua) { lua_pushstring(lua, accounts[workfds[luaL_checkinteger(lua, 1)]]); return 1; }
static int lua_work_char(lua_State *lua) { lua_pushliteral(lua, "fixture-mac"); return 1; }
static int lua_message(lua_State *lua) { ++notices; return 0; }
static int lua_player_max(lua_State *lua) { lua_pushinteger(lua, 32); return 1; }
static int lua_check(lua_State *lua) { lua_pushinteger(lua, CHAR_CHECKINDEX(luaL_checkinteger(lua, 1))); return 1; }
static int lua_stop(lua_State *lua) { encounters[luaL_checkinteger(lua, 1)] = 0; ++stopped; return 0; }
static int lua_fd(lua_State *lua) { lua_pushinteger(lua, workfds[luaL_checkinteger(lua, 1)]); return 1; }
static int lua_function(lua_State *lua) {
    int index = luaL_checkinteger(lua, 1);
    snprintf(loop_name[index], sizeof(loop_name[index]), "%s", luaL_checkstring(lua, 3)); return 0;
}
static int lua_del_function(lua_State *lua) { loop_name[luaL_checkinteger(lua, 1)][0] = 0; return 0; }
static int lua_logout(lua_State *lua) { CHAR_logout(luaL_checkinteger(lua, 1), TRUE); return 0; }
static int lua_server(lua_State *lua) { lua_pushstring(lua, blocked_line ? "娱乐线" : "fixture"); return 1; }
static int lua_time(lua_State *lua) { lua_pushinteger(lua, 123456); return 1; }
static int lua_get_players(lua_State *lua) { lua_pushinteger(lua, lua_players); return 1; }
static int lua_set_players(lua_State *lua) { lua_players = luaL_checkinteger(lua, 1); return 0; }
static int lua_sql(lua_State *lua) {
    assert(strstr(luaL_checkstring(lua, 1), "`Offline`=1")); ++sql_offline; lua_pushinteger(lua, 1); return 1;
}
static int lua_return(lua_State *lua) { assert(stone_app_virtual(luaL_checkinteger(lua, 1))); return 0; }
static int lua_close_request(lua_State *lua) {
    CONNECT_setCloseRequest(luaL_checkinteger(lua, 1), luaL_checkinteger(lua, 2)); return 0;
}
static int lua_battle(lua_State *lua) { ++new_battles; lua_pushinteger(lua, 2); return 1; }
lua_State *FindLua(char *path) { assert(!strcmp(path, "data/ablua/item/offline.lua")); return fixture_lua; }
static void lua_table(const char *name, const luaL_Reg *functions) {
    lua_newtable(fixture_lua); luaL_setfuncs(fixture_lua, functions, 0); lua_setglobal(fixture_lua, name);
}
static void init_lua(void) {
    fixture_lua = luaL_newstate(); assert(fixture_lua); luaL_openlibs(fixture_lua);
    const luaL_Reg chars[] = {{"getInt", lua_get_int}, {"getWorkInt", lua_get_work},
        {"setInt", lua_set_int}, {"setWorkInt", lua_set_work}, {"getChar", lua_get_char},
        {"getWorkChar", lua_work_char}, {"newMessageToCli", lua_message}, {"getPlayerMaxNum", lua_player_max},
        {"check", lua_check}, {"StopEncounter", lua_stop}, {"getFd", lua_fd},
        {"setFunctionPointer", lua_function}, {"delFunctionPointer", lua_del_function}, {"logou", lua_logout}, {NULL, NULL}};
    const luaL_Reg others[] = {{"time", lua_time}, {"getLuaPLayerNum", lua_get_players}, {"setLuaPLayerNum", lua_set_players}, {NULL, NULL}};
    const luaL_Reg config[] = {{"getGameservername", lua_server}, {NULL, NULL}};
    const luaL_Reg sql[] = {{"query", lua_sql}, {NULL, NULL}};
    const luaL_Reg net[] = {{"setCloseRequest", lua_close_request}, {NULL, NULL}};
    const luaL_Reg wire[] = {{"sendOfflineReturn", lua_return}, {NULL, NULL}};
    const luaL_Reg battle[] = {{"CreateVsEnemy", lua_battle}, {NULL, NULL}};
    lua_table("char", chars); lua_table("other", others); lua_table("config", config); lua_table("sasql", sql);
    lua_table("net", net); lua_table("lssproto", wire); lua_table("battle", battle);
    assert(luaL_dofile(fixture_lua, "runtime/gmsv/data/ablua/item/offline.lua") == LUA_OK);
    lua_getglobal(fixture_lua, "data"); assert(lua_pcall(fixture_lua, 0, 0, 0) == LUA_OK);
}
static void run_loop(int index) {
    lua_getglobal(fixture_lua, loop_name[index]); lua_pushinteger(fixture_lua, index);
    assert(lua_pcall(fixture_lua, 1, 0, 0) == LUA_OK);
}

static AppGroup *init_owner(int primary) {
    int index = 9 + primary;
    connected[7] = alive[index] = 1; ++epochs[7];
    states[7] = LOGIN; indexes[7] = index; workfds[index] = 7;
    floors[index] = 100; battles[index] = 2; vitality[index] = 100;
    snprintf(accounts[7], CDKEYLEN, "account%d", primary);
    for (int s = 0; s < 5; ++s) sql_rows[s][2] = s + 1 == primary ? "1" : "0";
    assert(stone_app_primary_slot(7) == primary);
    return groups;
}
static void load_members(AppGroup *g) {
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        AppMember *m = &g->member[s];
        /* SAAC returns name|escaped-option, not a flat character record. */
        assert(stone_app_list_reply(m->fd, "successful", "Companion|0\\z23\\z4\\z100\\z11\\z2\\z0\\z60\\z0\\z100\\z0\\z0\\z0\\z1\\zCompanion\\zVillage"));
        assert(m->face == 23 && m->level == 4);
    }
    assert(stone_app_control_request(7, "AL"));
    for (int s = 0; s < 5; ++s) {
        if (s + 1 == g->primary_slot) continue;
        AppMember *m = &g->member[s];
        assert(m->phase == ROLE_LOAD && stone_app_primary_slot(m->fd) == g->primary_slot);
        int index = 10 + s;
        alive[index] = 1; workfds[index] = m->fd; indexes[m->fd] = index; states[m->fd] = LOGIN;
        battles[index] = 2; floors[index] = 100; vitality[index] = 100;
        stone_app_load_reply(m->fd, "successful"); assert(m->phase == ROLE_ONLINE);
    }
}
static int roster_field(int slot, int field) {
    char text[128];
    assert(getStringFromIndexWithDelim(roster, "|", 3 + (slot - 1) * 13 + field, text, sizeof(text)));
    return atoi(text);
}

static void reset_reconnect_fixture(void) {
    while (groups) { AppGroup *next = groups->next; free(groups); groups = next; }
    memset(connected, 0, sizeof(connected)); memset(alive, 0, sizeof(alive));
    memset(accounts, 0, sizeof(accounts)); memset(states, 0, sizeof(states));
    memset(modes, 0, sizeof(modes)); memset(offline, 0, sizeof(offline));
    memset(parties, 0, sizeof(parties)); memset(trades, 0, sizeof(trades));
    memset(street_busy, 0, sizeof(street_busy));
    memset(character_accounts, 0, sizeof(character_accounts));
    memset(character_names, 0, sizeof(character_names));
    next_fd = 20; feature_busy_fd = -1; reconnect_calls = reconnect_reject = 0;
    reconnect_target = 10; account1_online = 1;
}

static void check_disconnected_primary(void) {
    enum { RECONNECT, RECONNECT_ALL, ACTIVE_FD, WRONG_ACCOUNT, WRONG_NAME,
        PAID_OFFLINE, ENCOUNTER_OFFLINE, IN_BATTLE, IN_TRADE, STREET_VENDOR,
        SAVE_PENDING, SAVE_FAILED, FEATURE_BUSY, NATIVE_REJECT, NON_PLAYER, DUPLICATE_MATCH, CASE_COUNT };
    for (int reason = 0; reason < CASE_COUNT; ++reason) {
        reset_reconnect_fixture();
        AppGroup *g = init_owner(2);
        AppMember *m = &g->member[0];
        assert(stone_app_list_reply(m->fd, "successful", "Companion|0\\z23\\z4"));
        assert(m->phase == ROLE_IDLE && !m->wanted);
        int index = reason == NON_PLAYER ? 20 : 10;
        reconnect_target = index;
        alive[index] = 1; workfds[index] = -1; offline[index] = 2;
        strcpy(character_accounts[index], "account1"); strcpy(character_names[index], "Companion");
        parties[index] = CHAR_PARTY_CLIENT; leaders[index] = g->index;
        switch (reason) {
            case ACTIVE_FD:
                connected[99] = 1; states[99] = LOGIN; indexes[99] = index;
                strcpy(accounts[99], "account1"); workfds[index] = 99; break;
            case WRONG_ACCOUNT: strcpy(character_accounts[index], "another-account"); break;
            case WRONG_NAME: strcpy(character_names[index], "another-character"); break;
            case PAID_OFFLINE: offline[index] = 1; break;
            case ENCOUNTER_OFFLINE: offline[index] = 3; break;
            case IN_BATTLE: modes[index] = BATTLE_CHARMODE_FINAL; break;
            case IN_TRADE: trades[index] = CHAR_TRADE_FREE + 1; break;
            case STREET_VENDOR: street_busy[index] = 1; break;
            case SAVE_PENDING: m->saves = 1; break;
            case SAVE_FAILED: m->save_failed = 1; break;
            case FEATURE_BUSY: feature_busy_fd = m->fd; break;
            case NATIVE_REJECT: reconnect_reject = 1; break;
            case DUPLICATE_MATCH:
                alive[15] = 1; workfds[15] = -1; offline[15] = 2;
                strcpy(character_accounts[15], "account1"); strcpy(character_names[15], "Companion"); break;
        }
        int before_loads = loads, before_saves = saves, before_offline = offline[index];
        int original_fd = workfds[index];
        fprintf(stderr, "[fixture] disconnected-primary boundary=%d\n", reason);
        assert(stone_app_control_request(7, reason == RECONNECT_ALL ? "AL" : "B|1"));
        assert(loads == before_loads && saves == before_saves);
        assert(alive[index] && parties[index] == CHAR_PARTY_CLIENT && leaders[index] == g->index);
        assert(indexes[7] == g->index && workfds[g->index] == 7);
        int players = 0;
        for (int i = 0; i < 32; ++i) players += !!alive[i];
        assert(players == (reason == DUPLICATE_MATCH ? 3 : 2)); /* No replacement object. */
        if (reason == RECONNECT || reason == RECONNECT_ALL) {
            assert(reconnect_calls == 1 && m->phase == ROLE_ONLINE);
            assert(indexes[m->fd] == index && workfds[index] == m->fd && offline[index] == 0);
            assert(stone_app_control_owned(7, 1));
            stone_app_role_roster(7);
            assert(roster_field(1, 5) == 2 && roster_field(1, 11) == 2);
            int snapshots = snapshot_counts[index];
            assert(stone_app_control_request(7, "B|1"));
            assert(reconnect_calls == 1 && loads == before_loads && snapshot_counts[index] == snapshots);
        } else {
            assert(reconnect_calls == (reason == NATIVE_REJECT));
            assert(m->phase == ROLE_IDLE && !stone_app_control_owned(7, 1));
            assert(indexes[m->fd] == -1 && workfds[index] == original_fd && offline[index] == before_offline);
            if (reason == NATIVE_REJECT) assert(!m->wanted);
            if (reason == ACTIVE_FD) assert(connected[99] && states[99] == LOGIN && indexes[99] == index);
        }
    }
    printf("PASS: disconnected primary reuses its native object; %d reconnect/ownership/save boundaries\n", CASE_COUNT);
}

static void check_individual_login(void) {
    reset_reconnect_fixture();
    account1_online = 0;
    AppGroup *g = init_owner(5);
    int before = loads;
    const char *invalid[] = {"I", "I|", "I|0", "I|6", "I|-1", "I|01", "I|1|extra", "I|1||"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        assert(!stone_app_control_request(7, invalid[i]));
    assert(loads == before);
    /* Actual target 0x7c9b32 formats I|<fixed roster slot>, without trailing |. */
    assert(stone_app_control_request(7, "I|1"));
    assert(g->member[0].wanted && g->member[0].phase == ROLE_LIST);
    for (int s = 1; s < 5; ++s) assert(!g->member[s].wanted);
    assert(stone_app_list_reply(g->member[0].fd, "successful", "Companion|0\\z23\\z4\\z100"));
    assert(loads == before + 1 && g->member[0].phase == ROLE_LOAD);
    assert(stone_app_control_request(7, "I|1|") && loads == before + 1);
    assert(stone_app_control_request(7, "I|5") && !g->member[4].wanted);
    g->member[2].account[0] = 0;
    assert(stone_app_control_request(7, "I|3") && !g->member[2].wanted);
    assert(!g->switching && loads == before + 1);
    puts("PASS: target I single login loads only the authenticated sibling, ignores duplicate/self/absent slots");
}

int main(void) {
    init_lua();
    AppGroup *g = init_owner(2);
    assert(sql_calls == 1 && lists == 4 && loads == 0 && auto_calls == 0);
    assert(strstr(sql_query_text, "ORDER BY child.Id LIMIT 5") && !strstr(sql_query_text, "child.Id<>current.Id"));
    assert(g->primary_slot == 2 && g->member[1].fd == -1 && g->member[1].password[0]);
    assert(!stone_app_virtual(7) && stone_app_virtual(g->member[0].fd));
    assert(stone_app_primary_slot(7) == 2 && sql_calls == 1); /* first P is stable */
    load_members(g);
    assert(loads == 4 && sql_calls == 1);
    for (int slot = 1; slot <= 5; ++slot) {
        assert(stone_app_control_owned(7, slot));
        assert(roster_field(slot, 0) == slot && roster_field(slot, 5) == 1);
        assert(roster_field(slot, 8) == 1 && roster_field(slot, 11) == 2);
        /* Target L field 9 marks the physical controller. Otherwise it is
         * rendered as a fifth subordinate, covering the one-key buttons. */
        assert(roster_field(slot, 6) == (slot == 2));
    }
    assert(!stone_app_control_owned(7, 0) && !stone_app_control_owned(7, 6));
    int first_fd = g->member[0].fd, third_fd = g->member[2].fd;
    stone_app_battle_command(7, 0, "H|F"); assert(commands == 1 && command_fd == first_fd);
    stone_app_battle_command(7, 1, "H|F"); assert(commands == 2 && command_fd == 7);
    stone_app_battle_command(7, 3, "W|0|F"); assert(commands == 3 && command_fd == g->member[3].fd);
    stone_app_battle_command(7, 15, "H|0"); assert(commands == 3);
    battles[13] = 3; stone_app_battle_command(7, 3, "H|F"); assert(commands == 3); battles[13] = 2;
    epochs[first_fd]++; stone_app_battle_command(7, 0, "H|F"); assert(commands == 3); epochs[first_fd]--;
    assert(stone_app_status(third_fd, 2026, "K0|native pet"));
    assert(packet_fd == 7 && packet_type == 2026 && values[0] == 3 && !strcmp(payload, "K0|native pet"));
    assert(stone_app_status(third_fd, 0, "BP|2|0|0") && values[0] == 3);
    packet_type = 0; assert(stone_app_status(third_fd, 0, "BC|duplicate") && packet_type == 0);
    assert(stone_app_pet_state(third_fd, 2019, 5, 1));
    assert(values[0] == 3 && values[1] == 5 && values[2] == 1 && values[3] == 9);
    assert(stone_app_control_request(7, "Z|1") && wars == 1);
    assert(stone_app_control_request(7, "Z|2") && wars == 1);
    assert(stone_app_control_request(7, "AZ") && wars == 5);
    assert(stone_app_control_request(7, "AT") && joins == 4);
    assert(roster_field(1, 5) == 2 && roster_field(2, 5) == 1 && roster_field(5, 5) == 2);
    stone_app_control_request(7, "AT"); assert(joins == 4);
    stone_app_control_request(7, "AL"); assert(loads == 4);
    assert(!stone_app_control_request(7, "A") && !stone_app_control_request(7, "AZextra"));
    assert(!stone_app_control_request(7, "Z|0") && !stone_app_control_request(7, "Z|6"));

    /* AG executes the original qualification checks, paid mode and callbacks. */
    vitality[10] = 0;
    assert(stone_app_control_request(7, "G|1") && !offline[10] && notices == 1);
    vitality[10] = 100; floors[10] = 1042;
    assert(!stone_app_control_offline(7, 1) && !offline[10]); floors[10] = 100;
    blocked_line = 1; assert(!stone_app_control_offline(7, 1)); blocked_line = 0;
    assert(!stone_app_control_offline(7, 2) && !stone_app_control_offline(7, 6));
    lua_pushliteral(fixture_lua, "existing stack value");
    parties[10] = CHAR_PARTY_NONE;
    encounters[11] = 1; /* Main encounter state is independent of AG. */
    assert(stone_app_control_request(7, "AG"));
    assert(lua_gettop(fixture_lua) == 1 && !strcmp(lua_tostring(fixture_lua, -1), "existing stack value"));
    lua_pop(fixture_lua, 1);
    assert(lua_players == 4 && sql_offline == 4 && stopped == 4 && auto_calls == 0);
    assert(offline[11] == 0 && encounters[11] == 1 && stopped == 4 && saves == 0 && closes == 0);
    for (int s = 0; s < 5; ++s) {
        if (s == 1) continue;
        int index = 10 + s, fd = g->member[s].fd;
        assert(offline[index] == 1 && loop_time[index] == 4000 && login_time[index] == 123456);
        assert(last_leave[index] == 123456 && npc_time[index] == 123456 && timers[index] == 0);
        assert(!strcmp(loop_name[index], s == 0 ? "Loop" : "pLoop"));
        assert(!close_requests[fd] && connected[fd] && states[fd] == LOGIN && indexes[fd] == index);
        assert(g->member[s].phase == ROLE_ONLINE && workfds[index] == fd && roster_field(s + 1, 5) == 3);
    }
    stone_app_control_request(7, "AG"); assert(lua_players == 4 && sql_offline == 4 && stopped == 4);
    run_loop(10); assert(new_battles == 1 && offline_time[10] == 4);
    run_loop(12); assert(new_battles == 1 && offline_time[12] == 4);
    modes[10] = modes[12] = BATTLE_CHARMODE_FINAL; battles[12] = 3;
    stone_app_battle_out(7); assert(exits == 1 && modes[12] == BATTLE_CHARMODE_FINAL);
    modes[10] = modes[12] = BATTLE_CHARMODE_NONE; battles[12] = 2;

    /* Original Lua expiry must save/logout without releasing the fd before ACK. */
    stone_app_saving(first_fd);
    vitality[10] = 0; run_loop(10);
    assert(!alive[10] && indexes[first_fd] == -1 && g->member[0].phase == ROLE_SAVE);
    assert(lua_players == 3 && saves == 1 && closes == 0 && g->member[0].saves == 2);
    assert(stone_app_save_reply(first_fd, "successful") && closes == 0);
    assert(stone_app_save_reply(first_fd, "successful") && closes == 1 && !connected[first_fd]);
    stone_app_saving(third_fd);
    states[7] = WHILELOGOUTSAVE;
    stone_app_disconnect(7);
    assert(g->closing && saves == 1 && closes == 1 && lua_players == 3);
    for (int s = 2; s < 5; ++s) {
        assert(g->member[s].phase == ROLE_ONLINE && alive[10 + s]);
        parties[10 + s] = CHAR_PARTY_NONE; /* Native primary logout removes its party. */
    }
    connected[7] = alive[11] = 0;
    run_loop(12); assert(offline_time[12] == 8 && lua_players == 3);
    packet_type = 0; assert(stone_app_status(third_fd, 2026, "Pdetached") && !packet_type);

    /* A different MasterId cannot adopt even when a stale name matches. */
    AppGroup *detached = g;
    for (int s = 0; s < 5; ++s) sql_rows[s][4] = "10043";
    g = init_owner(1);
    assert(g->master_id == 10043 && !stone_app_control_owned(7, 3));
    assert(detached->member[2].fd == third_fd && detached->member[2].saves == 1);
    assert(stone_app_control_request(7, "O|3") && alive[12] && detached->member[2].phase == ROLE_ONLINE);
    stone_app_disconnect(7);
    groups = g->next; free(g);

    /* Same Master sees detached offline roles, but AL/AZ/AT and individual
     * Z/T never take over their fdids. The reference server leaves owner=-1. */
    int previous_loads = loads, previous_lists = lists;
    for (int s = 0; s < 5; ++s) sql_rows[s][4] = "10042";
    g = init_owner(1);
    assert(g->primary_slot == 1 && g->member[2].fd == -1 && detached->member[2].fd == third_fd);
    assert(loads == previous_loads && lists == previous_lists + 1);
    for (int s = 2; s < 5; ++s) assert(detached->member[s].fd >= 0 && !stone_app_control_owned(7, s + 1));
    stone_app_role_roster(7);
    assert(roster_field(3, 5) == 3 && roster_field(3, 11) == -1 && roster_field(3, 8) == 1);
    int previous_wars = wars, previous_joins = joins;
    stone_app_control_request(7, "AL"); stone_app_control_request(7, "AZ"); stone_app_control_request(7, "AT");
    stone_app_control_request(7, "Z|3"); stone_app_control_request(7, "T|3");
    assert(loads == previous_loads && wars == previous_wars && joins == previous_joins);
    assert(detached->member[2].fd == third_fd && !stone_app_control_owned(7, 3));
    stone_app_save_reply(third_fd, "successful"); assert(detached->member[2].saves == 0);
    for (int s = 2; s < 5; ++s) {
        int fd = detached->member[s].fd;
        if (s == 4) {
            vitality[14] = 0; run_loop(14);
            assert(roster_field(5, 5) == 3);
            ++fixture_now; stone_app_tick(); /* Offline expiry refreshes observers. */
        } else {
            char request[8]; snprintf(request, sizeof(request), "O|%d", s + 1);
            assert(stone_app_control_request(7, request));
        }
        assert(detached->member[s].phase == ROLE_SAVE);
        assert(!alive[10 + s] && connected[fd] && roster_field(s + 1, 5) == 0 && roster_field(s + 1, 11) == -1);
        stone_app_save_reply(fd, "successful");
    }
    assert(lua_players == 0 && saves == 4);
    g->next = detached->next; free(detached);
    /* Explicit O followed by B performs a fresh native load only after unlock. */
    assert(stone_app_control_request(7, "B|3") && g->member[2].phase == ROLE_LIST);
    assert(stone_app_list_reply(g->member[2].fd, "successful", "Companion|native-file|23|4|100|"));
    assert(g->member[2].phase == ROLE_LOAD && loads == previous_loads + 1);
    int reloaded_fd = g->member[2].fd;
    alive[12] = 1; indexes[reloaded_fd] = 12; workfds[12] = reloaded_fd; states[reloaded_fd] = LOGIN; offline[12] = 0;
    stone_app_load_reply(reloaded_fd, "successful");
    assert(stone_app_control_owned(7, 3) && roster_field(3, 5) == 1 && roster_field(3, 11) == 1);
    stone_app_disconnect(7);
    assert(g->closing && g->member[2].phase == ROLE_SAVE && !alive[12] && connected[reloaded_fd]);
    stone_app_save_reply(reloaded_fd, "successful");
    groups = NULL; free(g);

    /* A missing owner in the fixed five or a SQL failure never grants siblings. */
    for (int s = 0; s < 5; ++s) sql_rows[s][2] = "0";
    connected[7] = alive[10] = 1; states[7] = LOGIN;
    g = create_group(7); assert(g && g->primary_slot == 1);
    for (int s = 0; s < 5; ++s) assert(g->member[s].fd == -1 && !g->member[s].account[0]);
    groups = NULL; free(g);
    sql_failed = 1; g = create_group(7); assert(g && g->member[0].fd == -1);
    groups = NULL; free(g); sql_failed = 0;

    /* Empty/unusable sibling records preserve later fixed slots. */
    char *saved_name = sql_rows[0][0]; sql_rows[0][0] = NULL;
    g = init_owner(5); assert(g->primary_slot == 5 && !g->member[0].account[0]);
    assert(!strcmp(g->member[1].account, "account2") && g->member[4].fd == -1);
    stone_app_role_roster(7);
    assert(roster_field(5, 6) == 1 && roster_field(2, 6) != 1);
    sql_rows[0][0] = saved_name;
    groups = NULL; free(g);

    /* Q uses normal authentication before the original native reconnect.
     * All five character objects and party membership survive, with no new
     * SAAC load/lock. A pending save or another MasterId cannot take over. */
    memset(connected, 0, sizeof(connected)); memset(alive, 0, sizeof(alive));
    memset(modes, 0, sizeof(modes)); memset(offline, 0, sizeof(offline));
    memset(parties, 0, sizeof(parties)); next_fd = 20;
    g = init_owner(1); load_members(g);
    stone_app_control_request(7, "AZ"); stone_app_control_request(7, "AT");
    int old_primary_index = indexes[7], target_fd = g->member[1].fd;
    int target_index = indexes[target_fd], before_loads = loads;
    feature_busy_fd = target_fd;
    stone_app_control_request(7, "Q|2"); assert(!g->switching);
    feature_busy_fd = -1;
    stone_app_control_request(7, "Q|2"); assert(g->switching == 2);
    fixture_now += 61; stone_app_tick(); assert(!g->switching);
    /* Drain ordinary autosaves triggered during the timeout tick. */
    for (int s = 1; s < 5; ++s) stone_app_save_reply(g->member[s].fd, "successful");
    stone_app_saving(7); stone_app_control_request(7, "Q|2"); assert(!g->switching);
    stone_app_save_reply(7, "successful");
    assert(stone_app_control_request(7, "Q|2") && g->switching == 2 && packet_type == 2024);
    assert(!strcmp(payload, "Q|2"));
    lssproto_NEWLOGOUT2_recv(7, 0); /* Only the real 207/213 branch may hand off. */
    assert(!g->closing && g->switching == 2 && logout_ack == 0);
    lssproto_NEWLOGOUT2_recv(7, 213);
    assert(logout_ack == 1);
    assert(g->closing && g->primary_slot == 0 && indexes[7] == -1 && states[7] == NOTLOGIN && !accounts[7][0]);
    int parked_fd = workfds[old_primary_index];
    assert(parked_fd != 7 && stone_app_virtual(parked_fd) && indexes[parked_fd] == old_primary_index);
    assert(offline[old_primary_index] == 2 && offline[target_index] == 2 && loads == before_loads);
    assert(parties[target_index] == CHAR_PARTY_CLIENT && leaders[target_index] == old_primary_index);
    strcpy(accounts[7], "account3"); assert(!stone_app_reconnect_ready(7, target_index));
    strcpy(accounts[7], "account2");
    feature_busy_fd = target_fd; assert(!stone_app_reconnect_ready(7, target_index)); feature_busy_fd = -1;
    assert(stone_app_reconnect_ready(7, target_index));
    /* These are the existing CharLogin offline-reconnect assignments. */
    indexes[target_fd] = -1; indexes[7] = target_index; workfds[target_index] = 7;
    states[7] = LOGIN; offline[target_index] = 0;
    for (int s = 0; s < 5; ++s) sql_rows[s][2] = s == 1 ? "1" : "0";
    AppGroup *switched = create_group(7);
    assert(switched != g && switched->primary_slot == 2 && switched->member[0].fd == parked_fd);
    assert(stone_app_control_owned(7, 1) && stone_app_control_owned(7, 5) && loads == before_loads);
    memset(snapshot_counts, 0, sizeof(snapshot_counts));
    stone_app_refresh_companions(7);
    for (int s = 0; s < 5; ++s)
        assert(snapshot_counts[10+s] == (s == 1 ? 0 : 12));
    assert(stone_app_close(target_fd) && !accounts[target_fd][0]);
    stone_app_control_request(7, "AL");
    assert(offline[old_primary_index] == 0 && loads == before_loads);
    stone_app_control_request(7, "Q|1"); assert(switched->switching == 1);
    assert(stone_app_switch_logout(7) && logout_ack == 2);
    /* An abandoned handoff eventually performs real save/unlock cleanup. */
    fixture_now += 61; stone_app_tick();
    for (int s = 0; s < 5; ++s) assert(!alive[10+s]);
    check_disconnected_primary();
    check_individual_login();
    lua_close(fixture_lua);
    puts("PASS: fixed slots/first P, native offline Lua, detached lifetime/display without takeover, and ordered saves");
    return 0;
}
