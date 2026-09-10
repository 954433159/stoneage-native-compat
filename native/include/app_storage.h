#ifndef STONE_APP_STORAGE_H
#define STONE_APP_STORAGE_H

/* All calls execute on the native GMSV/SAAC event loop. The fd must resolve to
 * the current authenticated native character; no account or path is supplied
 * by the caller. Return 1 includes a refused storage request (never fall back
 * to the legacy Lua transfer after a refusal). */
void stone_storage_login(int fd);
int stone_storage_menu(int fd, int menu); /* 202: 35 items, 36 pets, shared */
int stone_storage_personal(int fd, int index, const char *data); /* 821: 1/2 */
int stone_storage_window(int fd, int sequence, int object, int select,
                         const char *data); /* 67, issued shared view only */

/* Count EVERY real ACCharSave send and corresponding ordered reply, including
 * autosave and final logout. Hook before other reply handlers may return or
 * dispose of the character. The caller must resolve mesgid via getfdFromFdid.
 * logout closes the view but deliberately retains pending/FIFO state. */
void stone_storage_saving(int fd);
void stone_storage_saved(int fd, const char *result);
void stone_storage_logout(int fd);
/* Read-only: true while any native save ACK, storage transaction or unresolved
 * receipt prevents rebinding this fd to another controlled character. */
int stone_storage_busy(int fd);

enum { STONE_ASSET_PET = 1, STONE_ASSET_ITEM = 2 };
typedef struct StoneNativeAsset {
    int kind, bound;
    char uid[128];
    char owner_account[64], owner_name[128];
    char blob[32768], summary[768], detail[4096];
} StoneNativeAsset;

/* Shared native asset codec for storage/market. Slots refer only to the
 * currently authenticated character (items 9..23, pets 0..4). No SQL, receipt,
 * save request or market/window response occurs here. The caller owns the
 * durable journal and must save its receipt together with the movement.
 * serialize returns 1 or 0; restore returns the registered object index or -1;
 * remove returns 1 or 0, and requires the exact previously serialized UID.
 * uid_count returns -1 for an invalid identity, otherwise native membership. */
int stone_asset_serialize(int charaindex, int kind, int slot, StoneNativeAsset *out);
int stone_asset_restore(int charaindex, int slot, const StoneNativeAsset *asset);
int stone_asset_remove(int charaindex, int kind, int slot, const char *uid);
int stone_asset_uid_count(int charaindex, int kind, const char *uid);

#endif
