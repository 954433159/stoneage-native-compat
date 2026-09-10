"""Run the native SAAC family-list serializer with production optimization."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FamilyListTests(unittest.TestCase):
    def test_existing_families_retain_rows(self):
        raw = (ROOT / 'native/overrides/saac/acfamily.c').read_bytes()
        try:
            source = raw.decode('utf8')
        except UnicodeDecodeError:
            source = raw.decode('gb18030')
        function = source[source.index('int ACShowFMList(char *data)'):source.index('int getFmmemberindexmaxnum(')]
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "version.h"
static int fmindexmaxnum, fmnownum;
static struct {
    char fmname[64], fmleadername[64], fmleaderid[64];
    int fmindex, fmtotalfame, fmjoinnum, fmsetupflag, fmacceptflag;
    int fmleadergrano, fmbadge, fmsprite, fmimage, fmpopular;
} family[3];
''' + function + r'''
int main(void) {
    char data[1024];
    family[0].fmindex=-1;
    assert(ACShowFMList(data)==0 && !strcmp(data,""));
    family[0].fmindex=1;strcpy(family[0].fmname,"localqa");
    strcpy(family[0].fmleadername,"Leader");strcpy(family[0].fmleaderid,"qa");
    family[0].fmjoinnum=1;family[0].fmacceptflag=1;
    family[0].fmleadergrano=100180;family[0].fmbadge=59000;
    fmnownum=1;
    assert(ACShowFMList(data)==1);
    assert(!strcmp(data,"1 localqa Leader 0 1 0 0 1 qa 100180 59000 0|"));
    family[2]=family[0];family[2].fmindex=3;strcpy(family[2].fmname,"another");
    family[1].fmindex=-1;fmindexmaxnum=2;fmnownum=2;
    assert(ACShowFMList(data)==2);
    assert(!strcmp(data,"1 localqa Leader 0 1 0 0 1 qa 100180 59000 0|3 another Leader 0 1 2 0 1 qa 100180 59000 0|"));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-family-list-') as temp:
            path = Path(temp)
            (path / 'test.c').write_text(harness, encoding='utf8')
            compile_result = subprocess.run(['cc', '-O2', '-D_FORTIFY_SOURCE=2',
                '-Inative/overrides/saac/include', str(path/'test.c'), '-o', str(path/'test')],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(path/'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
