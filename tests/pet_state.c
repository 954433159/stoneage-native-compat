#include "version.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "util.h"
#include "lssproto_serv.h"
#include "net.h"
#include "char.h"
#include "char_talk.h"
#include "battle.h"
#include "family.h"
#include "configfile.h"

#include "../build/pet_state_function.inc"

static int profile = 4, connected = 1, battle_pet = 0, ride_pet = 1;
static int mode, status[5], replies, ks_replies;
int CONNECT_getloginmark(int fd) { return profile; }
BOOL CONNECT_isCLI(int fd) { return connected && fd == 7; }
BOOL CONNECT_isLOGIN(int fd) { return connected && fd == 7; }
int CONNECT_getCharaindex(int fd) { return connected && fd == 7 ? 2 : -1; }
BOOL _CHAR_CHECKINDEX(char *file, int line, int index) { return index >= 2 && index <= 7; }
int _CHAR_getInt(char *file, int line, int index, CHAR_DATAINT field) {
    if (field == CHAR_DEFAULTPET) return battle_pet;
    if (field == CHAR_RIDEPET) return ride_pet;
    return 0;
}
int _CHAR_setInt(char *file, int line, int index, CHAR_DATAINT field, int value) {
    if (field == CHAR_DEFAULTPET) battle_pet = value;
    if (field == CHAR_RIDEPET) ride_pet = value;
    return value;
}
int _CHAR_getWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field) {
    if (field == CHAR_WORKBATTLEMODE) return mode;
    if (field >= CHAR_WORK_PET0_STAT && field <= CHAR_WORK_PET4_STAT)
        return status[field - CHAR_WORK_PET0_STAT];
    return 0;
}
int _CHAR_setWorkInt(char *file, int line, int index, CHAR_WORKDATAINT field, int value) {
    assert(field >= CHAR_WORK_PET0_STAT && field <= CHAR_WORK_PET4_STAT);
    status[field - CHAR_WORK_PET0_STAT] = value;
    return value;
}
int CHAR_getCharPet(int index, int slot) { return slot + 3; }
int getfdFromCharaIndex(int index) { return 7; }
void lssproto_KS_send(int fd, int pet, int result) { ++ks_replies; }
void lssproto_PETS_send(int fd, int pet, int result) { ++replies; }
void lssproto_KS_recv(int fd, int pet) { battle_pet = pet; }
int _CHAR_complianceParameter(int index, char *file, int line) { return 0; }
void CHAR_sendCToArroundCharacter(int index) {}
BOOL CHAR_send_P_StatusString(int index, unsigned int field) { return TRUE; }
int FAMILY_RidePet(int fd, int index, char *message) { ride_pet = 1; return 0; }
int getPlayerPetLv(void) { return 5; }
BOOL CHAR_newMessageToCli(int index, int source, char *message, int color) { return TRUE; }
void lssproto_NewSaMenu2_send(int fd, int menu, char *text, char *callback) {}

int main(void) {
    /* SDL setUserSetting sends one 0/1 PETST per slot after receiving KS.
     * These are standby preferences; they must not cancel battle or riding. */
    for (int slot = 0; slot < 5; ++slot) lssproto_PETST_recv(7, slot, slot < 2);
    assert(battle_pet == 0 && ride_pet == 1 && ks_replies == 0);
    assert(status[0] == 1 && status[1] == 1);
    assert(replies == 4); /* Do not overwrite the active pet's KS display. */
    lssproto_PETST_recv(7, 2, 1);
    lssproto_PETST_recv(7, 2, 0);
    assert(status[2] == 0 && battle_pet == 0 && ride_pet == 1);
    mode = 1; lssproto_PETST_recv(7, 2, 1); assert(status[2] == 0);
    mode = 0; connected = 0; lssproto_PETST_recv(7, 2, 1); assert(status[2] == 0);
    connected = 1;
    /* Preserve the original unified/Unity protocol for other profiles. */
    profile = 3; lssproto_PETST_recv(7, 0, 1);
    assert(battle_pet == -1 && ks_replies == 1);
    puts("PASS: SDL login standby initialization preserves native battle/ride selection; unified profile unchanged");
    return 0;
}
