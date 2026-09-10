#ifndef STONE_APP_ATTRIBUTES_H
#define STONE_APP_ATTRIBUTES_H

/* Authenticated current character only. A handled/refused request returns 1.
 * Hooks run on the native GMSV/SAAC event loop. Never route virtual role IDs. */
int stone_attributes_menu(int fd, int menu);
int stone_attributes_window(int fd, int sequence, int object, int select,
                            const char *data);
void stone_attributes_saving(int fd);
void stone_attributes_saved(int fd, const char *result);
void stone_attributes_logout(int fd);
/* Read-only: controller switching must wait for outstanding save/payment work. */
int stone_attributes_busy(int fd);

/* Small accessors defined beside SQLConfig in sasql_linux.c by the supplied
 * integration patch. They expose identifiers, never credentials. */
const char *stone_attributes_account_table(void);
const char *stone_attributes_account_key(void);

#endif
