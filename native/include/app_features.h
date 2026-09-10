#ifndef STONE_APP_FEATURES_H
#define STONE_APP_FEATURES_H
/* Read-only barrier for native inventory/point/trade/session mutations while
 * a feature owns an unacknowledged character snapshot or durable receipt. */
int stone_features_busy(int fd);
#endif
