#ifndef STONE_APP_MAIL_H
#define STONE_APP_MAIL_H

/* Call login only after a native character has been loaded and authenticated.
 * The target opens Lua window 1026 with menu 202/index 118; 67 carries its
 * session seqno, object=-1, select=0 and R|id|1, D|id|1 or T|1. Return 1 from
 * menu/window means consumed. Both identity fields come from the native fd.
 * saving must run once before EVERY actual ACCharSave send, including autosaves
 * and logout saves; saved must run once for EVERY matching FIFO SAAC reply,
 * before other reply handlers return or destroy the connection/character.
 * The caller resolves the reply's fdid with getfdFromFdid before calling saved.
 */
void stone_mail_login(int fd);
int stone_mail_menu(int fd, int menu);
int stone_mail_window(int fd, int sequence, int object, int select, const char *data);
void stone_mail_request(int fd, int id);
void stone_mail_saving(int fd);
void stone_mail_saved(int fd, const char *result);
int stone_mail_busy(int fd);

/* Schema is created lazily, with no automatic mail/rewards. Local administrator
 * seeding example (use the exact authenticated account and CHAR_NAME bytes):
 * SET @recipient_account = ...; SET @recipient_name = ...;
 * INSERT INTO local_system_mail(cdkey,charname,title,body,item_template)
 * VALUES (BINARY @recipient_account,BINARY @recipient_name,
 *         '邮件验收：无附件','这是一封无附件系统邮件。',NULL),
 *        (BINARY @recipient_account,BINARY @recipient_name,
 *         '邮件验收：小斧头','附件为一把小斧头。',0);
 * Login updates red dot 831; menu 118 loads the current SQL rows with 66/L.
 * Claim confirmation uses 808/S only after the inventory snapshot and SQL
 * acknowledgement; no-attachment claims/deletes update 808/L directly.
 * D refuses any unclaimed attachment. Template 0 is a native small axe;
 * it is not picture 20033. These statements are documentation, never executed
 * by this adapter. CHAR_ACTIVE_ACT16/caa16 is reserved for mail:<positive id>.
 * Existing non-mail data there causes a refusal instead of being overwritten.
 * created_at records actual insertion (or old-row migration) time. NULL
 * expires_at keeps existing non-expiring behavior; the fixed target date UI
 * displays INT_MAX in that case. No expiry deletion policy is introduced.
 */
#endif
