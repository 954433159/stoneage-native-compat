#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "version.h"
#include "char.h"
#include "battle.h"
#include "net.h"

static int profile = 4, team_channel = 1;
static int modes[3] = {2, 1, 0}, leaders[3] = {1, 1, -1};
int CONNECT_getloginmark(int fd) { assert(fd == 5); return profile; }
BOOL _CHAR_getFlg(char *file, int line, int index, CHAR_DATAFLG field) {
    assert(field == CHAR_ISPARTYCHAT); return team_channel;
}
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    assert(field == CHAR_WHICHTYPE); return CHAR_TYPEPLAYER;
}
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    if (field == CHAR_WORKPARTYMODE) return modes[index];
    if (field == CHAR_WORKPARTYINDEX1) return leaders[index];
    if (field == CHAR_WORKBATTLEMODE) return BATTLE_CHARMODE_NONE;
    assert(0); return 0;
}
#include "../build/team_chat_function.inc"

static void check(const char *input, const char *expected, int accepted) {
    char text[512]; strcpy(text, input);
    assert(stone_app_team_body(5, 0, text) == accepted);
    assert(strcmp(text, expected) == 0);
}
static int observed_channel;
static void route(char *messageeraseescape, int channel, int quickchannel) {
    int fd = 5, index = 0;
#include "../build/team_chat_route.inc"
    observed_channel = channel;
}
int main(void) {
    check("/TM 队伍石头测试0909", "队伍石头测试0909", TRUE);
    check("/TM 含有 空格 /TM 原文", "含有 空格 /TM 原文", TRUE);
    check("/TM ", "", FALSE);
    check("/TM /yd", "/yd", TRUE);
    check("/TM .command", ".command", TRUE);
    char colon[] = "/TM :队伍消息";
    route(colon, -1, 0);
    assert(observed_channel == -1 && !strcmp(colon, ":队伍消息"));
    check("/TMX 原文", "/TMX 原文", TRUE);
    check("/tm 原文", "/tm 原文", TRUE);
    check("/T", "/T", TRUE);
    check("普通中文", "普通中文", TRUE);
    assert(CHAR_Talk_check(0, 1, 0));
    assert(!CHAR_Talk_check(0, 2, 0));
    modes[2] = 2; leaders[2] = 2;
    assert(!CHAR_Talk_check(0, 2, 0));
    team_channel = 0;
    char ordinary_colon[] = ":原生家族快捷频道";
    route(ordinary_colon, -1, 0);
    assert(observed_channel == 0 && !strcmp(ordinary_colon, ":原生家族快捷频道"));
    check("/TM 原文", "/TM 原文", TRUE);
    assert(CHAR_Talk_check(0, 2, 0));
    team_channel = 1; profile = 1;
    check("/TM 原文", "/TM 原文", TRUE);
    profile = 4; modes[0] = CHAR_PARTY_NONE;
    check("/TM 原文", "/TM 原文", TRUE);
    puts("PASS: App team prefix, literal body, empty guard and native teammate/outsider routing");
}
