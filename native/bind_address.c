#include "bind_address.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int stone_bind_address(void)
{
    const char *value = getenv("STONEAGE_BIND_ADDR");
    struct in_addr parsed;
    unsigned int host;

    if (value == NULL || value[0] == '\0')
        return htonl(0x7f000001u);
    if (inet_pton(AF_INET, value, &parsed) != 1)
        goto refuse;
    host = ntohl(parsed.s_addr);
    /* 0.0.0.0, broadcast and multicast would expose every interface. */
    if (host == 0 || host == 0xffffffffu || host >= 0xe0000000u)
        goto refuse;
    return parsed.s_addr;
refuse:
    fprintf(stderr, "[local] STONEAGE_BIND_ADDR refused; listening on loopback\n");
    return htonl(0x7f000001u);
}
