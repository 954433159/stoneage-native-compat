"""Run prepared native family creation/ACK functions against in-memory roles.

No services, accounts or database connections are used by this harness.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class FamilyCreationTests(unittest.TestCase):
    def test_validation_and_rejected_creation_preserve_fee(self):
        relative='vendor/gmsv/char/family.c' if os.getenv('STONE_FAMILY_ORIGINAL')=='1' else 'native/overrides/gmsv/char/family.c'
        raw=(ROOT/relative).read_bytes()
        try: source=raw.decode('utf-8')
        except UnicodeDecodeError: source=raw.decode('gb18030')
        functions=source[source.index('int CheckLeaderQ('):source.index('void FAMILY_Join(')]
        harness=r'''
#define main storage_fixture_main
#include "tests/app_storage.c"
#undef main
#include "family.h"
static int family_requests,adult=1;
int FamilyBadgeData[]={1,2,3};
int FamilyBadgeDataNum=3;
static void reset_family(int index) {
    CHAR_setInt(index,CHAR_FMINDEX,-1);
    CHAR_setChar(index,CHAR_FMNAME,"");
    CHAR_setInt(index,CHAR_FMLEADERFLAG,0);
}
#define NPC_EventCheckFlg(...) adult
#define CHAR_send_P_StatusString(...) ((void)0)
#define CHAR_makeCAOPT1String(...) ((void)0)
#define lssproto_CA_send(...) ((void)0)
#define lssproto_WN_send(...) ((void)0)
#define saacproto_ACAddFM_send(...) (++family_requests)
#define saacproto_ACShowFMList_send(...) ((void)0)
#define saacproto_ACShowMemberList_send(...) ((void)0)
#define saacproto_ACShowTopFMList_send(...) ((void)0)
#define JoinMemberIndex(...) ((void)0)
#define CHAR_UpdateEquitMagic(...) ((void)0)
#define CHAR_newMessageToCli(...) ((void)0)
#define LogFamily(...) ((void)0)
#define SetFMVarInit(index) reset_family(index)
'''+functions+r'''
static void ready(void) {
    reset();family_requests=0;adult=1;reset_family(2);
    CHAR_setInt(2,CHAR_LV,50);CHAR_setInt(2,CHAR_GOLD,20000);
    give_pet(7,0,"family-pet",1);
}
int main(int argc,char **argv) {
    assert(argc==2);ready();
    if(!strcmp(argv[1],"invalid-name")) {
        FAMILY_Add(7,2,"A|bad name|0|0|local|0|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
    } else if(!strcmp(argv[1],"missing-pet")) {
        FAMILY_Add(7,2,"A|LocalQA|4|0|local|0|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
    } else if(!strcmp(argv[1],"invalid-badge")) {
        FAMILY_Add(7,2,"A|LocalQA|0|0|local|4|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
    } else if(!strcmp(argv[1],"invalid-qq")) {
        FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|123456789012|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
    } else if(!strcmp(argv[1],"duplicate-name")) {
        FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|1|");
        assert(family_requests==1 && CHAR_getInt(2,CHAR_GOLD)==10000);
        ACAddFM(7,0,-2,0);
        assert(CHAR_getInt(2,CHAR_GOLD)==20000 && CHAR_getInt(2,CHAR_FMINDEX)==-1);
    } else if(!strcmp(argv[1],"accepted")) {
        FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|1|");
        assert(family_requests==1 && CHAR_getInt(2,CHAR_GOLD)==10000);
        ACAddFM(7,1,1,0);
        assert(CHAR_getInt(2,CHAR_GOLD)==10000 && CHAR_getInt(2,CHAR_FMINDEX)==1);
    } else if(!strcmp(argv[1],"qualification")) {
        CHAR_setInt(2,CHAR_LV,29);FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
        CHAR_setInt(2,CHAR_LV,50);adult=0;FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==20000);
        adult=1;CHAR_setInt(2,CHAR_GOLD,9999);FAMILY_Add(7,2,"A|LocalQA|0|0|local|0|1|");
        assert(!family_requests && CHAR_getInt(2,CHAR_GOLD)==9999);
    } else { assert(0); }
    reset();return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-family-') as directory:
            path=Path(directory);(path/'family.c').write_text(harness,encoding='utf-8')
            command=['cc','-D_GNU_SOURCE','-DLUA_COMPAT_MODULE','-Wno-int-conversion',
                     '-Wno-incompatible-pointer-types','-Wno-multichar','-ffunction-sections','-fdata-sections',
                     '-I.','-Inative','-Inative/include','-Inative/overrides/gmsv',
                     '-Inative/overrides/gmsv/include','-Inative/overrides/gmsv/lua','-I/usr/include/mariadb',
                     str(path/'family.c'),'-Wl,--gc-sections','-o',str(path/'family')]
            build=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            for branch in ('invalid-name','missing-pet','invalid-badge','invalid-qq','duplicate-name','accepted','qualification'):
                with self.subTest(branch=branch):
                    result=subprocess.run([str(path/'family'),branch],cwd=ROOT,capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__': unittest.main()
