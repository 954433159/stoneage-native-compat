"""Compile the real NPC argument-file reader; exercise long paths with fortify."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class NativeNpcPathTests(unittest.TestCase):
    def test_long_native_argument_path_and_oversize_rejection(self):
        source = (PROJECT / 'native/overrides/gmsv/npc/npcutil.c').read_text(encoding='utf-8')
        begin = source.index('static char *NPC_Util_MargeStrFromArgFile(')
        end = source.index('\nchar *NPC_Util_GetArgStr(', begin)
        harness = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#define NPC_UTIL_GETARGSTR_LINEMAX 4096
#define BOOL int
#define TRUE 1
#define FALSE 0
static char *directory;
static char *getNpcdir(void) { return directory; }
static void DecryptKey(char *line) { (void)line; }
static void chompex(char *s) { s[strcspn(s, "\r\n")] = 0; }
static void strcatsafe(char *to, int size, const char *from) {
    size_t n = strlen(to); if (n < (size_t)size) snprintf(to+n, size-n, "%s", from);
}
'''
        harness += source[begin:end]
        harness += r'''
int main(int argc, char **argv) {
    assert(argc == 2); directory = argv[1];
    char result[128] = "";
    assert(NPC_Util_MargeStrFromArgFile("casino/petracepet1.arg", result, sizeof(result)) == result);
    assert(strcmp(result, "native=correct|second=kept") == 0);
    char oversized[8192]; memset(oversized, 'a', sizeof(oversized)-1); oversized[sizeof(oversized)-1] = 0;
    assert(NPC_Util_MargeStrFromArgFile(oversized, result, sizeof(result)) == NULL);
    assert(result[0] == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-npc-path-') as temporary:
            root = Path(temporary)
            (root / 'casino').mkdir()
            (root / 'casino/petracepet1.arg').write_text('native=correct\nsecond=kept\n')
            (root / 'reader.c').write_text(harness)
            for encrypted in (False, True):
                executable = root / ('reader-crypto' if encrypted else 'reader')
                command = ['cc', '-O2', '-g', '-D_FORTIFY_SOURCE=2',
                           str(root / 'reader.c'), '-o', str(executable)]
                if encrypted:
                    command.insert(1, '-D_CRYPTO_DATA')
                subprocess.run(command, check=True, capture_output=True, text=True)
                result = subprocess.run([str(executable), str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
