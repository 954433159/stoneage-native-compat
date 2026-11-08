#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int stone_bind_address(void);

static void expect_loopback(const char *value)
{
    if (value == NULL)
        unsetenv("STONEAGE_BIND_ADDR");
    else
        setenv("STONEAGE_BIND_ADDR", value, 1);
    assert(stone_bind_address() == htonl(0x7f000001u));
}

static void expect_address(const char *value)
{
    struct in_addr parsed;
    setenv("STONEAGE_BIND_ADDR", value, 1);
    assert(inet_pton(AF_INET, value, &parsed) == 1);
    assert(stone_bind_address() == parsed.s_addr);
}

int main(void)
{
    expect_loopback(NULL);
    expect_loopback("");
    expect_loopback("0.0.0.0");
    expect_loopback("255.255.255.255");
    expect_loopback("224.0.0.1");
    expect_loopback("not-an-address");
    expect_loopback("http://10.0.0.2");
    expect_address("127.0.0.1");
    expect_address("10.0.0.2");
    expect_address("192.168.1.20");
    expect_address("203.0.113.10");
    puts("PASS: game bind defaults to loopback and accepts one IPv4 unicast");
    return 0;
}
