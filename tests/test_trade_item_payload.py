"""Actual native direct-trade item formatter, production optimizer and bounds."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DirectTradeItemTests(unittest.TestCase):
    def test_item_identity_survives_custom_fields(self):
        source = (ROOT / 'native/overrides/gmsv/char/trade.c').read_text(encoding='utf8')
        start = source.index('BOOL TRADE_HandleItem(', source.index('BOOL TRADE_HandleItem(')+1)
        function = source[start:source.index('\nBOOL TRADE_HandleGold(', start)]
        sized = 'size_t outsize' in function.split('{', 1)[0]
        prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define _ITEM_PILEFORTRADE
#define _ITEM_PILENUMS
#define _ITEM_SHOWCUSTEM
#define CHAR_STARTITEMARRAY 9
#define CHAR_MAXITEMHAVE 54
enum { CHAR_WORKTRADEMODE, CHAR_TRADE_TRADING, CHAR_TRADE_LOCK, TRADE_ITEM,
       ITEM_TYPE, ITEM_VANISHATDROP, ITEM_MAGICID, ITEM_BASEIMAGENUMBER,
       ITEM_NEEDTRANS, ITEM_LEVEL, ITEM_NAME, ITEM_SECRETNAME, ITEM_USEFUNC,
       ITEM_TYPECODE, ITEM_INLAYCODE, MAGIC_NAME };
static int fixture_judge;
static int getfdFromCharaIndex(int i){return 7;}
static int CONNECT_get_confirm(int fd){return 0;}
static int CHAR_getWorkInt(int i,int k){return CHAR_TRADE_TRADING;}
static int CHAR_getItemIndex(int i,int slot){return slot==14 ? 4 : -1;}
static int ITEM_CHECKINDEX(int i){return i==4;}
static int getStringFromIndexWithDelim(char *s,char *d,int f,char *out,int n){
    snprintf(out,n,"%s",f==6 ? "14" : "1");return f==6 || f==7;}
static int TRADE_addSomeTradeList(int i,int s,int *n,int kind){return 1;}
static int ITEM_getInt(int i,int k){switch(k){case ITEM_TYPE:return 16;
    case ITEM_MAGICID:return 1;case ITEM_BASEIMAGENUMBER:return 20001;
    case ITEM_NEEDTRANS:return 2;case ITEM_LEVEL:return 50;default:return 0;}}
static char *ITEM_getChar(int i,int k){switch(k){case ITEM_NAME:return "fish";
    case ITEM_SECRETNAME:return "fish-display";case ITEM_TYPECODE:return "blue";
    case ITEM_INLAYCODE:return "rune";default:return "";}}
static char *makeEscapeString(char *s,char *b,int n){snprintf(b,n,"%s",s);return b;}
static char *FreeShowItemEffect(int i){return "memo";}
static int MAGIC_getMagicArray(int i){return 0;}
static char *MAGIC_getChar(int i,int k){return "Heal";}
static int FreeJudgePlayer(int i){return fixture_judge;}
static int FreeGetItemStar(int i){return 7;}
'''
        call = 'TRADE_HandleItem(1,0,"request",out' + (',capacity)' if sized else ')')
        harness = prefix + function + '\nstatic int invoke(char *out,size_t capacity){return ' + call + ';}\n' + r'''
int main(void){
    char out[2048];
    const char *base="20001|fish|fish-display|memo|14|耐久度：无限|1|0|Heal|blue|rune|2|50";
    for(fixture_judge=0;fixture_judge<2;fixture_judge++){
        char expected[256];snprintf(expected,sizeof(expected),"%s",base);
#ifdef _ITEM_SYSTEM
        if(fixture_judge)strcat(expected,"|7");
#endif
        memset(out,0,sizeof(out));assert(invoke(out,sizeof(out))==TRUE);
        assert(!strcmp(out,expected));
''' + (r'''
        char shortbuf[2048];memset(shortbuf,0x5a,sizeof(shortbuf));
        assert(invoke(shortbuf,32)==FALSE && shortbuf[32]==0x5a);
        memset(shortbuf,0x5a,sizeof(shortbuf));size_t n=strlen(expected);
        assert(invoke(shortbuf,n)==FALSE && shortbuf[n]==0x5a);
''' if sized else '') + '    }\n    return 0;\n}\n'
        with tempfile.TemporaryDirectory(prefix='stone-direct-trade-item-') as temp:
            path = Path(temp)
            (path/'test.c').write_text(harness, encoding='utf8')
            for define in ([], ['-D_ITEM_SYSTEM']):
                with self.subTest(defines=define):
                    built = subprocess.run(['cc','-O2','-D_FORTIFY_SOURCE=2',*define,
                        str(path/'test.c'),'-o',str(path/'test')], capture_output=True, text=True)
                    self.assertEqual(built.returncode,0,built.stderr)
                    ran = subprocess.run([str(path/'test')],capture_output=True,text=True)
                    self.assertEqual(ran.returncode,0,ran.stderr)

if __name__ == '__main__':
    unittest.main()
