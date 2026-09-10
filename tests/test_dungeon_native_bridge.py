"""Run the prepared daily entry with the real native Lua CallFunction bridge.

No services or player databases are used. This catches same-state stack erasure
that a Python/Lua callback mock cannot reproduce.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DungeonNativeBridgeTests(unittest.TestCase):
    def test_daily_entry_preserves_native_lua_arguments(self):
        source = (ROOT / 'native/overrides/gmsv/mylua/otherbase.c').read_text()
        bridge = source[source.index('static int CallFunction(lua_State *L)'):
                        source.index('static int c10to62(lua_State *L)')]
        harness = r'''
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
typedef struct MY_Lua { struct MY_Lua *next; const char *luapath; lua_State *lua; } MY_Lua;
static MY_Lua MYLua, tail;
static int docall(lua_State *L, int n, int results) { return lua_pcall(L,n,results,0); }
''' + bridge + r'''
int main(int argc, char **argv) {
    lua_State *L=luaL_newstate();luaL_openlibs(L);
    MYLua.next=&tail;MYLua.luapath="data/ablua/npc/newFB/NewFB.lua";MYLua.lua=L;
    lua_pushcfunction(L,CallFunction);lua_setglobal(L,"native_call");
    for(int i=1;i<argc;i++) {
        if(luaL_dofile(L,argv[i])!=LUA_OK) { fprintf(stderr,"%s\n",lua_tostring(L,-1));lua_close(L);return 1; }
    }
    lua_close(L);return 0;
}
'''
        checks = r'''
local function field(s,sep,n)
    local from=1
    for i=1,n do local finish=s:find(sep,from,true)
        if i==n then return s:sub(from,finish and finish-1 or #s) end
        if not finish then return '' end
        from=finish+#sep
    end
end
local warps,queries,completed,failed=0,0,0,false
other={CallFunction=native_call,getString=field,atoi=function(v)return tonumber(v) or 0 end}
char={check=function(i)return i==42 and 1 or 0 end,getWorkInt=function()return 0 end,
    getInt=function()return 100 end,getChar=function(_,key)return key=='账号' and 'isolated-account' or 'isolated-role' end,
    TalkToCli=function()end,
    AllWarpToSpecificPoint=function(i,f,x,y) assert(i==42 and f==3 and x==12 and y==13);warps=warps+1 end}
sasql={query=function()queries=queries+1;return failed and 0 or 1 end,
    free_result=function()end,store_result=function()end,num_rows=function()return 1 end,
    fetch_row=function()end,data=function()return os.date('%m%d')..'/'..completed end}
-- A real WN directory request targets this same Lua state, not another module.
WindowTalked(1,42,1131,0,'G|1|1')
assert(warps==1 and queries==1,'daily request must reach original allowance and native warp')
completed=2;WindowTalked(1,42,1131,0,'G|1|1');assert(warps==1 and queries==2)
completed=0;failed=true;WindowTalked(1,42,1131,0,'G|1|1');assert(warps==1 and queries==3)
for _,request in ipairs({'G|1|0','G|1|1.5','G|4|1'}) do WindowTalked(1,42,1131,0,request) end
assert(warps==1 and queries==3)
print('PASS daily dungeon: production Lua and native bridge, allowance, DB failure, invalid request')
'''
        objects = sorted((ROOT / 'build/CMakeFiles/lua_reference.dir/overrides/gmsv/lua').glob('*.c.o'))
        self.assertTrue(objects, 'Build lua_reference before this check')
        with tempfile.TemporaryDirectory(prefix='stone-dungeon-bridge-') as directory:
            path = Path(directory)
            (path / 'bridge.c').write_text(harness)
            (path / 'checks.lua').write_text(checks)
            build = subprocess.run(['cc', '-O1', '-g', '-Inative/overrides/gmsv/lua',
                                    str(path / 'bridge.c'),
                                    *(str(p) for p in objects if p.name != 'lua.c.o'),
                                    '-lm', '-ldl', '-o', str(path / 'bridge')],
                                   cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(path / 'bridge'),
                                     'runtime/gmsv/data/ablua/npc/newFB/NewFB.lua', str(path / 'checks.lua')],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
