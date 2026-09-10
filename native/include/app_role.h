#ifndef STONE_APP_ROLE_H
#define STONE_APP_ROLE_H
#include <stddef.h>

/* Slots follow sibling account Id order, including the authenticated player. */
int stone_app_primary_slot(int fd);
int stone_app_role_roster(int fd);
void stone_app_role_request(int fd, const char *request);
int stone_app_control_roster(int fd, char *out, size_t size);
int stone_app_control_request(int fd, const char *request);
int stone_app_control_owned(int fd, int slot);
/* Reuse normal native paid offline mode while retaining a virtual session. */
int stone_app_control_offline(int fd, int slot);
int stone_app_virtual(int fd);
void stone_app_tick(void);
void stone_app_disconnect(int fd);
int stone_app_close(int fd);
void stone_app_logged_out(int fd);
int stone_app_list_reply(int fd, const char *result, const char *data);
void stone_app_load_reply(int fd, const char *result);
int stone_app_save_reply(int fd, const char *result);
void stone_app_saving(int fd);
int stone_app_status(int fd, int protocol, const char *data);
int stone_app_pet_state(int fd, int protocol, int first, int result);
int stone_app_battle_command(int fd, int position, char *command);
void stone_app_battle_out(int fd);
void stone_app_auto_send(int fd, int slot, int mode);
/* Q uses the original client's logout/authentication/offline reconnect flow. */
int stone_app_switch_logout(int fd);
int stone_app_reconnect_ready(int fd, int index);
void stone_app_refresh_companions(int fd);

#endif
