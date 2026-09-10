"""Actual native pet trade formatter against the target TD inspection contract."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DirectTradePetTests(unittest.TestCase):
    def test_target_inspection_elements_and_growth_fields(self):
        source = (ROOT/'native/overrides/gmsv/char/trade.c').read_text(encoding='utf8')
        marker = ', oldlv, oldhp, oldstr, oldvgh, olddex, CHAR_getInt(petindex,'
        point = source.index(marker)
        start = source.rfind('sprintf(outmess,', 0, point)
        end = source.index('\n                       );', point) + len('\n                       );')
        formatter = source[start:end]
        prefix = r'''
#include <stdio.h>
#include <string.h>
#include "version.h"
enum { CHAR_WORKMAXHP, CHAR_SLOT, CHAR_NAME, CHAR_FUSIONBEIT,
       CHAR_EARTHAT, CHAR_WATERAT, CHAR_FIREAT, CHAR_WINDAT, CHAR_NOFAME,
       CHAR_LASTTALKELDER, CHAR_ATTACK_EFFECT, CHAR_PETDATA2,
       CHAR_BATTLEEFFECTS, CHAR_BATTLEEFFECTS2 };
static int CHAR_getInt(int p,int k){switch(k){case CHAR_SLOT:return 7;
case CHAR_EARTHAT:return 10;case CHAR_WATERAT:return 20;
case CHAR_FIREAT:return 30;case CHAR_WINDAT:return 40;
case CHAR_NOFAME:return 1;case CHAR_LASTTALKELDER:return 2;
case CHAR_ATTACK_EFFECT:return 3;case CHAR_PETDATA2:return 4;default:return 0;}}
static int CHAR_getWorkInt(int p,int k){return 52;}
static char *CHAR_getChar(int p,int k){return "pet";}
static char *CHAR_getUseName(int p){return "pet-alias";}
static char *makeEscapeString(char *s,char *b,int n){snprintf(b,n,"%s",s);return b;}
static char *PET_get4v(int p){return "51|8|5|6";}
int main(void){
int petindex=1,oldlv=1,oldhp=52,oldstr=9,oldvgh=6,olddex=7;
char outmess[2048],buf1[256],buf2[256];
char *token="100251|pet-alias|1|9|6|7|2|0";
char *skillname[]={"Attack","Defend","","","","",""};
char *zhufudata="101,0|102,1|103,0|104,1";
char *petplessdata=zhufudata,*zhufubuff="";
'''
        with tempfile.TemporaryDirectory(prefix='stone-direct-trade-pet-') as temp:
            path=Path(temp)
            (path/'test.c').write_text(prefix+formatter+'\nputs(outmess);return 0;}\n', encoding='utf8')
            subprocess.run(['cc','-O2','-D_FORTIFY_SOURCE=2',
                '-I'+str(ROOT/'native/overrides/gmsv/include'),
                str(path/'test.c'),'-o',str(path/'test')],check=True,capture_output=True)
            wire=subprocess.run([str(path/'test')],check=True,capture_output=True,text=True).stdout.strip()
        # Target EXE 7c4048..7c41e2 parses 30..42; 7df31b..7df4b6
        # labels fields 35..38 WATER/FIRE/EARTH/WIND (not struct member names).
        fields=('T|7|peer|P|0|'+wire).split('|')
        at=lambda n:fields[n-1]
        self.assertEqual([at(n) for n in range(26,30)],['101,0','102,1','103,0','104,1'])
        self.assertEqual([int(at(n)) for n in range(30,35)],[1,52,9,6,7])
        self.assertEqual([int(at(n)) for n in range(35,39)],[20,30,10,40])
        self.assertEqual([int(at(n)) for n in range(39,43)],[51,8,5,6])

if __name__=='__main__':
    unittest.main()
