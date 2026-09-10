/* Execute audited target Lua resources with the unchanged reference 6021 VM. */
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int writer(lua_State *L, const void *data, size_t size, void *file) {
    (void)L;
    return fwrite(data, 1, size, file) == size ? 0 : 1;
}

int main(int argc, char **argv) {
    int compile = argc == 4 && strcmp(argv[1], "--compile") == 0;
    if (argc < 2) return 2;
    lua_State *L = luaL_newstate();
    if (!L) return 3;
    luaL_openlibs(L);
    lua_newtable(L);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);
    }
    lua_setglobal(L, "arg");
    int status = luaL_loadfile(L, argv[compile ? 2 : 1]);
    if (!status && compile) {
        FILE *out = fopen(argv[3], "wb");
        if (!out) status = 4;
        else {
            status = lua_dump(L, writer, out);
            if (fclose(out) != 0) status = 5;
        }
    } else if (!status) status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status) fprintf(stderr, "Client Lua failed (%d): %s\n", status, lua_tostring(L, -1));
    lua_close(L);
    return status ? 1 : 0;
}
