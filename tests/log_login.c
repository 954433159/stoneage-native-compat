#include <assert.h>
#include "../native/overrides/gmsv/log.c"

struct timeval NowTime;

char *getGameservername(void) {
    return "本地石器";
}

int main(void) {
    FILE *output = tmpfile();
    char line[512];
    assert(output);
    NowTime.tv_sec = 1788837895;
    LogConf[LOG_LOGIN].f = output;
    LogConf[LOG_LOGIN].append = TRUE;
    LogLogin("local-test", "Shitou", 0, "127.0.0.1"
#ifdef _NEWCLISETMAC
        , "test-machine"
#endif
    );
    rewind(output);
    assert(fgets(line, sizeof(line), output));
    assert(strstr(line, "local-test\tShitou\ti=0\t本地石器\t127.0.0.1"));
    fclose(output);
    puts("PASS: native login log retains 64-bit string pointers");
    return 0;
}
