"""Real family Lua and native metadata bindings; no database or services."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class FamilyMetadataTests(unittest.TestCase):
    def test_my_family_reads_native_metadata(self):
        source=(ROOT/'native/overrides/gmsv/mylua/family_mylua.c').read_text()
        functions=source[source.index('static int ShowFmBadge('):source.index('static int ShowFmAccept(')]
        start=source.index('#ifdef _FAMILY_QQ',source.index('static const luaL_Reg Familylib[]'))
        registration=source[start:source.index('#endif',start)+len('#endif')]
        c=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "char.h"
#include "family.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
static struct FMMEMBER_LIST memberlist[FAMILY_MAXNUM];
'''+functions+r'''
int main(int argc,char **argv) {
 assert(argc==2);
 memberlist[0].badge=59000;
#ifdef _FAMILY_QQ
 strcpy(memberlist[0].fmqq,"1");strcpy(memberlist[0].fmrule,"Native purpose");
 strcpy(memberlist[0].petname,"Guardian");strcpy(memberlist[0].petattr,"10145 2 3 4");
 memberlist[0].fmsprite=1;
#endif
 static const luaL_Reg methods[]={ {"ShowFmBadge",ShowFmBadge},
'''+registration+r'''
 {NULL,NULL}};
 lua_State *L=luaL_newstate();assert(L);luaL_openlibs(L);
 lua_newtable(L);luaL_setfuncs(L,methods,0);lua_setglobal(L,"family");
 int result=luaL_dofile(L,argv[1]);
 if(result!=LUA_OK)fprintf(stderr,"%s\n",lua_tostring(L,-1));
 lua_close(L);return result==LUA_OK?0:1;
}
'''
        lua=r'''
local function split(s,delimiter,n)
 local p=1;for i=1,n do local q=s:find(delimiter,p,true);if i==n then return s:sub(p,q and q-1 or #s) end;if not q then return '' end;p=q+#delimiter end
end
other={time=function()return 1000 end,getString=split,atoi=tonumber}
family.ACShowTopFMList=function()end;family.ACShowMemberList=function()end
family.ShowFmRank=function()return 0 end;family.ShowFmFame=function()return 77 end
family.ShowFmMomnum=function()return 300 end;family.ShowMemberListNum=function()return 1 end
family.ShowMemberListData=function()return '1|Leader|unused' end
char={getInt=function(_,k)return ({['家族索引']=1,['声望']=500,['家族地位']=3,['气势']=0})[k] end,
 getWorkInt=function(_,k)return k=='对象' and 30 or 0 end,
 getChar=function()return 'localqa' end,newMessageToCli=function()error('unexpected rejection')end}
local token
lssproto={windows=function(i,id,_,_,obj,data)assert(i==10 and id==1200 and obj==30);token=data end}
dofile('runtime/gmsv/data/ablua/npc/family/family.lua');data();npcindex=20;familytime={}
ShowMyFamily(10)
assert(token=='I|localqa|1|Leader|1|77|5|3|1|3|0|59000|1|1|Native purpose|10145',token)
for _,n in ipairs({'ShowFmQq','ShowFmRule','ShowFmPetName','ShowFmPetAttr'}) do
 assert(family[n](-1)=='' and family[n](4000)=='',n)
end
assert(family.ShowFmSprite(-1)==0 and family.ShowFmSprite(4000)==0)
print('Real ShowMyFamily 1200 reads registered native metadata and rejects out-of-range cache indices')
'''
        with tempfile.TemporaryDirectory(prefix='stone-family-metadata-') as temp:
            p=Path(temp);(p/'test.c').write_text(c);(p/'test.lua').write_text(lua,encoding='utf8')
            flags=['-D_GNU_SOURCE','-DLUA_COMPAT_MODULE','-Wno-int-conversion','-Wno-incompatible-pointer-types','-Wno-multichar',
                   '-ffunction-sections','-fdata-sections','-Inative','-Inative/include','-Inative/overrides/gmsv',
                   '-Inative/overrides/gmsv/include','-Inative/overrides/gmsv/lua','-I/usr/include/mariadb']
            objects=[str(x) for x in (ROOT/'build/CMakeFiles/gmsv.dir/overrides/gmsv/lua').glob('*.o') if x.name not in ('lua.c.o','luac.c.o')]
            self.assertTrue(objects,'Existing native Lua objects required')
            result=subprocess.run(['cc',*flags,str(p/'test.c'),*objects,'-Wl,--gc-sections','-lm','-ldl','-o',str(p/'test')],cwd=ROOT,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test'),str(p/'test.lua')],cwd=ROOT,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('Real ShowMyFamily',result.stdout)

if __name__=='__main__':unittest.main()
