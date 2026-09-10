"""Exercise native SAAC stop handlers without databases or player files."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / os.environ.get('SAAC_SHUTDOWN_SOURCE', 'native/overrides/saac')


class SaacShutdownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (SOURCE / 'main.c').read_text(encoding='utf-8')
        start = cls.source.index('void dump()')
        end = cls.source.index('void signalset(void)', start)
        cls.temporary = tempfile.TemporaryDirectory(prefix='stone-saac-stop-')
        cls.root = Path(cls.temporary.name)
        # File persistence is the boundary: these callbacks record that all
        # three existing native saves run, without opening any instance files.
        prefix = r'''
#include <assert.h>
#include <execinfo.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int saved, diagnostics;
char familydir[] = "fixture-family", fmpointdir[] = "fixture-point";
char fmsmemodir[] = "fixture-memo";
void writeFamily(char *path) { assert(!strcmp(path, familydir)); saved |= 1; }
void writeFMPoint(char *path) { assert(!strcmp(path, fmpointdir)); saved |= 2; }
void writeFMSMemo(char *path) { assert(!strcmp(path, fmsmemodir)); saved |= 4; }
void fixture_log(const char *format, ...) {
    va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap);
}
#define log fixture_log
void logerr(char *text) { assert(text); ++diagnostics; }
static void report(void) { printf("SAVED=%d DIAGNOSTICS=%d\n", saved, diagnostics); }
'''
        driver = r'''
int main(int argc, char **argv) {
    assert(argc == 2); atexit(report);
    int number = atoi(argv[1]);
    if (!number) sigshutdown(0);
    signal(number, sigshutdown);
    raise(number);
    return 99;
}
'''
        source = cls.root / 'stop.c'
        source.write_text(prefix + cls.source[start:end] + driver, encoding='utf-8')
        cls.executable = cls.root / 'stop'
        subprocess.run(['cc', '-O2', '-D_FORTIFY_SOURCE=2', '-fstack-protector-strong',
                        '-Werror=implicit-function-declaration', str(source),
                        '-o', str(cls.executable)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_native_backtrace_has_pointer_returning_declaration(self):
        self.assertTrue('#include <execinfo.h>' in self.source,
                        'SAAC backtrace lacks the pointer-returning libc declaration')

    def check_stop(self, number, normal):
        result = subprocess.run([str(self.executable), str(number)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0 if normal else 1)
        self.assertIn('SAVED=7 ', result.stdout)
        if normal:
            self.assertIn('DIAGNOSTICS=0', result.stdout)
        else:
            self.assertNotIn('DIAGNOSTICS=0', result.stdout)

    def test_project_sigterm_saves_and_exits_successfully(self):
        self.check_stop(15, True)

    def test_manual_interrupt_saves_and_exits_successfully(self):
        self.check_stop(2, True)

    def test_explicit_normal_stop(self):
        self.check_stop(0, True)

    def test_abnormal_stop_retains_diagnostics_and_failure_status(self):
        self.check_stop(6, False)


if __name__ == '__main__':
    unittest.main()
