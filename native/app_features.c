#include "app_features.h"
#include "app_mail.h"
#include "app_storage.h"
#include "app_attributes.h"
extern int stone_market_busy(int fd);

int stone_features_busy(int fd)
{
    return stone_mail_busy(fd) || stone_storage_busy(fd) ||
        stone_attributes_busy(fd) || stone_market_busy(fd);
}
