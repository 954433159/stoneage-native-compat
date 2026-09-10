"""Run the real GMSV signal handler with isolated persistence boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GmsvShutdownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'native/overrides/gmsv/msignal.c').read_text(encoding='utf-8')
        start = source.index('void sigshutdown(int number)')
        end = source.index('void signalset(void)', start)
        cls.temporary = tempfile.TemporaryDirectory(prefix='stone-gmsv-stop-')
        directory = Path(cls.temporary.name)
        prefix = r'''
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _KEEP_UP_NO_LOGIN
#define SERVER_VERSION "isolated native handler"
char keepupnologin[8] = "";
char saacretfunc[] = "fixture", saacsendfunc[] = "fixture";
int player_online, player_maxonline, cliretfunc, clisendfunc;
static int online, diagnostics, persisted, shutdowns;
void sasql_online(void *a, void *b, void *c, void *d, void *e, void *f, int mode) {
    assert(!a && !b && !c && !d && !e && !f && mode == 3); ++online;
}
void logerr(char *text) { assert(text); ++diagnostics; }
void dump(void) { ++diagnostics; }
int fixture_remove(const char *name) { assert(name); return 0; }
int fixture_rename(const char *a, const char *b) { assert(a && b); return 0; }
#define remove fixture_remove
#define rename fixture_rename
void allDataDump(void) { ++persisted; }
void shutdownProgram(void) { assert(persisted == 1); ++shutdowns; }
static void report(void) {
    printf("ONLINE=%d PERSISTED=%d SHUTDOWN=%d DIAGNOSTICS=%d\n",
           online, persisted, shutdowns, diagnostics);
}
'''
        driver = r'''
int main(int argc, char **argv) {
    assert(argc == 2); atexit(report);
    int number = atoi(argv[1]);
    if (number == 100) {
        strcpy(keepupnologin, "yes");
        sigshutdown(SIGINT);
        assert(!keepupnologin[0] && !persisted && !shutdowns);
        return 0;
    }
    if (!number) sigshutdown(0);
    signal(number, sigshutdown);
    raise(number);
    return 99;
}
'''
        path = directory / 'stop.c'
        path.write_text(prefix + source[start:end] + driver, encoding='utf-8')
        cls.executable = directory / 'stop'
        subprocess.run(['cc', '-O2', '-D_FORTIFY_SOURCE=2', '-fstack-protector-strong',
                        '-Werror=implicit-function-declaration', str(path), '-o', str(cls.executable)],
                       capture_output=True, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check_stop(self, number, normal):
        result = subprocess.run([str(self.executable), str(number)], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0 if normal else number)
        self.assertIn('ONLINE=1 PERSISTED=1 SHUTDOWN=1 ', result.stdout)
        if normal:
            self.assertIn('DIAGNOSTICS=0', result.stdout)
            self.assertIn('GMSV正常停止，原生数据保存和连接关闭已完成', result.stdout)
        else:
            self.assertNotIn('DIAGNOSTICS=0', result.stdout)

    def test_project_sigterm_is_success_after_native_persistence(self):
        self.check_stop(15, True)

    def test_manual_sigint_is_success_after_native_persistence(self):
        self.check_stop(2, True)

    def test_explicit_normal_stop(self):
        self.check_stop(0, True)

    def test_abnormal_signal_keeps_diagnostics_and_status(self):
        self.check_stop(6, False)

    def test_manual_login_pause_resume_does_not_stop_server(self):
        result = subprocess.run([str(self.executable), '100'], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertIn('ONLINE=0 PERSISTED=0 SHUTDOWN=0 DIAGNOSTICS=0', result.stdout)


if __name__ == '__main__':
    unittest.main()
