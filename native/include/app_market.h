#ifndef STONE_APP_MARKET_H
#define STONE_APP_MARKET_H
/* Original target window 1123, menu 202/index 130. Each opened view has a
 * native sequence bound to the authenticated fd and character. caa13 holds
 * market:<row id>:D/B/W and must be saved with its native asset movement.
 * Hook EVERY actual ACCharSave send and FIFO reply, before early returns.
 * Native asset-changing entrypoints must honor the shared features busy gate.
 * All functions execute on the single native GMSV/SAAC event loop. */
void stone_market_login(int fd);
int stone_market_menu(int fd, int menu);
int stone_market_window(int fd, int sequence, int object, int select, const char *data);
void stone_market_saving(int fd);
void stone_market_saved(int fd, const char *result);
void stone_market_logout(int fd);
int stone_market_busy(int fd);
#endif
