#include <assert.h>
#include "../native/overrides/gmsv/battle/battle_command.c"

BATTLE fixture;
BATTLE *BattleArray = &fixture;
int BATTLE_battlenum = 1;
static int relife[3] = {1, 0, 0};
static int changes;
static const char *titles[] = {"Champion123ABCDEF", "09AFHero", "FF00"};
BOOL BATTLE_CHECKINDEX(int index) { return index == 0; }
BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return index >= 0 && index < 3; }
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    if (field == CHAR_WHICHTYPE) return index == 0 ? CHAR_TYPEPLAYER : CHAR_TYPEPET;
    if (field == CHAR_BASEIMAGENUMBER) return 100010 + index;
    if (field == CHAR_LV) return 1;
    if (field == CHAR_HP) return 50 - index;
    if (field == CHAR_MP) return 17 + index;
    return 0;
}
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    if (field == CHAR_WORKMAXHP) return 50;
    if (field == CHAR_WORKMAXMP) return 100 + index;
    if (field == CHAR_WORKBATTLEMODE) return BATTLE_CHARMODE_INIT;
    if (field == CHAR_WORK_RELIFE) return relife[index];
    return 0;
}
int _CHAR_setWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field, int value) {
    assert(field == CHAR_WORK_RELIFE);
    relife[index] = value;
    ++changes;
    return value;
}
BOOL _CHAR_getFlg(char *file, int line, int index, CHAR_DATAFLG field) { return FALSE; }
char *CHAR_getUseName(int index) { return index == 0 ? "Xinshou" : index == 1 ? "Pet" : "Enemy"; }
char *_CHAR_getChar(char *file, int line, int index, CHAR_DATACHAR field) { return CHAR_getUseName(index); }
char *BATTLE_CharTitle(int index) { return (char *)titles[index]; }
char *BATTLE_CharUid(int index) { return ""; }
int BATTLE_getRidePet(int index) { return -1; }
char *makeEscapeString(char *source, char *destination, int length) {
    snprintf(destination, length, "%s", source);
    return destination;
}

static int split(char *text, char **fields) {
    int n = 0;
    char *next;
    while ((next = strchr(text, '|'))) {
        *next = 0;
        fields[n++] = text;
        text = next + 1;
    }
    assert(!*text);
    return n;
}

int main(void) {
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < BATTLE_ENTRY_MAX; ++slot)
            fixture.Side[side].Entry[slot].charaindex = -1;
    fixture.Side[0].Entry[0] = (BATTLE_ENTRY){.charaindex=0, .bid=0};
    fixture.Side[0].Entry[5] = (BATTLE_ENTRY){.charaindex=1, .bid=5};
    fixture.Side[1].Entry[5] = (BATTLE_ENTRY){.charaindex=2, .bid=15};
    char legacy[4096] = "", app[4096] = "", *old[100], *current[100];
#ifdef BEFORE_APP_BC
    assert(BATTLE_MakeCharaString(0, legacy, sizeof(legacy)));
    strcpy(app, legacy);
#else
    assert(BATTLE_MakeCharaString(0, legacy, app, sizeof(legacy)));
#endif
    assert(split(legacy, old) == 2 + 15 * 3);
    int count = split(app, current);
    printf("Target BC: %d fields; expected 53 for three actors\n", count);
    fflush(stdout);
    assert(count == 2 + 17 * 3);
    int bids[] = {0, 5, 15};
    for (int actor = 0; actor < 3; ++actor) {
        int base = 2 + actor * 17;
        assert(strtol(current[base], 0, 16) == bids[actor]);
        /* Fast target 0x861b50 does not consume its title; 0x861e60 then
         * mistakes title digits/A-F for the actor image and shifts all fields. */
        assert(!*current[base + 2]);
        assert(!strcmp(old[2 + actor * 15 + 2], titles[actor]));
        for (int field = 0; field < 15; ++field)
            if (field != 2)
                assert(!strcmp(current[base + field], old[2 + actor * 15 + field]));
        assert(strtol(current[base + 15], 0, 16) == 17 + actor);
        assert(strtol(current[base + 16], 0, 16) == 100 + actor);
    }
    assert(changes == 1 && relife[0] == 0);
#ifndef BEFORE_APP_BC
    strcpy(legacy, "stale"); strcpy(app, "stale");
    assert(!BATTLE_MakeCharaString(0, legacy, app, 60));
    assert(!*legacy && !*app);
#endif
    puts("PASS: App blank-title compatibility, unchanged legacy titles/actor fields, actual mana, single state transition and bounds");
    return 0;
}
