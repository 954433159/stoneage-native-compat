"""Exercise native free-slot allocation and both inventory senders at 3 pages.

The target client has 9 equipment slots and 3 x 15 bag slots (verified machine
code anchors in attributes_packets.py). No player database or client execution.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'native/overrides/gmsv'


class InventoryPagesTests(unittest.TestCase):
    def test_three_pages_allocation_and_untruncated_native_updates(self):
        header = (SOURCE / 'include/char_base.h').read_text(encoding='utf-8')
        item = (SOURCE / 'char/char_item.c').read_text(encoding='utf-8')
        char = (SOURCE / 'char/char.c').read_text(encoding='utf-8')
        macros = '\n'.join(re.search(r'^#define ' + key + r'\b[^\n]*', header, re.M)[0]
                           for key in ('CHAR_MAXITEMNUM', 'CHAR_STARTITEMARRAY', 'CHAR_MAXITEMHAVE'))
        allocation = item[item.index('static int CHAR_findEmptyItemBoxFromChar('):
                          item.index('static int CHAR_findEmptyPoolItemBoxFromChar(')]
        sender = item[item.index('BOOL CHAR_sendItemData('):
                      item.index('static void CHAR_sendItemDetachEvent(')]
        full = char[char.index("        case 'i':", char.index('static char CHAR_statusSendBuffer')):
                    char.index("        case 's':", char.index('static char CHAR_statusSendBuffer'))]
        buffers = '\n'.join(re.search(r'static char ' + key + r'\[[^;]+;', text)[0]
                            for key, text in [('CHAR_sendItemBuffer', item), ('CHAR_statusSendBuffer', char)])
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define STRINGBUFSIZ 4096
#define CHAR_EQUIPPLACENUM 9
#define CHAR_WHICHTYPE 0
#define CHAR_TYPEPLAYER 1
#define arraysizeof(a) (sizeof(a)/sizeof((a)[0]))
'''+macros+r'''
typedef struct { int indexOfExistItems[CHAR_MAXITEMHAVE]; } Char;
static char row[502], incremental[40000];
int CHAR_getInt(int index, int field) { return CHAR_TYPEPLAYER; }
int CHAR_CHECKITEMINDEX(int index, int slot) { return slot>=0 && slot<CHAR_MAXITEMHAVE; }
int CHAR_getItemIndex(int index, int slot) { return slot; }
int getfdFromCharaIndex(int index) { return 7; }
char *ITEM_makeItemStatusString(int a,int b,int c) { return row; }
char *ITEM_makeItemFalseString(int index) { return row; }
char *ITEM_makeItemFalseStringWithNum(int index,int slot) { return row; }
void strcpysafe(char *out,size_t size,const char *in) { if(size)snprintf(out,size,"%s",in); }
void dchop(char *s,char *delim) { size_t n=strlen(s);if(n && s[n-1]==*delim)s[n-1]=0; }
void lssproto_I_send(int fd,char *data) { assert(fd==7);snprintf(incremental,sizeof(incremental),"%s",data); }
'''+buffers+'\n'+allocation+'\n'+sender+r'''
char *full_inventory(int index) {
    int strlength=0;
    switch('i') {
'''+full+r'''
    } return NULL;
}
int main(void) {
    Char ch; int slots[54];
    for(int i=0;i<CHAR_MAXITEMHAVE;i++)ch.indexOfExistItems[i]=-1;
    for(int i=9;i<24;i++)ch.indexOfExistItems[i]=i;
    assert(CHAR_findEmptyItemBoxFromChar(&ch)==24 && "page 2 must be usable after 15 bag items");
    for(int i=24;i<53;i++)ch.indexOfExistItems[i]=i;
    assert(CHAR_findEmptyItemBoxFromChar(&ch)==53 && "last slot of page 3 must remain usable");
    ch.indexOfExistItems[53]=53;
    assert(CHAR_findEmptyItemBoxFromChar(&ch)==-1);
    assert(CHAR_MAXITEMHAVE==54 && CHAR_MAXITEMNUM==45);
    memset(row,'x',480);memset(row+480,'|',20);row[500]=0;
    for(int i=0;i<54;i++)slots[i]=i;
    assert(CHAR_sendItemData(1,slots,54));
    int bars=0;for(char *p=incremental;*p;p++)bars+=*p=='|';
    assert(bars==54*22-1 && "incremental updates must contain complete rows through slot 53");
    assert(strstr(incremental,"|53|")!=NULL);
    char *all=full_inventory(1);assert(*all=='I');
    assert(strlen(all)==54*501 && "login/full refresh must retain all 54 rows without truncation");
    bars=0;for(char *p=all+1;*p;p++)bars+=*p=='|';assert(bars==54*21-1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-inventory-pages-') as directory:
            path = Path(directory)
            (path / 'inventory.c').write_text(harness)
            built = subprocess.run(['cc', '-O1', '-g', '-fsanitize=address,undefined',
                                    '-fno-omit-frame-pointer', str(path / 'inventory.c'),
                                    '-o', str(path / 'inventory')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(path / 'inventory')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
