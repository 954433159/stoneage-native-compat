"""Exercise the actual sender against a reentrant native party-status update."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StatusInitializationTests(unittest.TestCase):
    def test_first_status_survives_group_adoption(self):
        text = (ROOT / 'native/overrides/gmsv/char/char.c').read_text(encoding='utf-8')
        begin = text.index('BOOL _CHAR_sendStatusString(')
        end = text.index('\nvoid CHAR_inputOwnTitle(', begin)
        harness = r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define CHAR_TYPEPLAYER 1
#define CHAR_WHICHTYPE 0
#define _NO_WAIGUA
static char buffer[128];
static int initialized;
int CHAR_getInt(int index, int field) { return 1; }
int getfdFromCharaIndex(int index) { return 7; }
int CONNECT_checkfd(int fd) { return fd == 7; }
int CONNECT_getloginmark(int fd) { return 4; }
int stone_app_primary_slot(int fd) {
    if (!initialized) { initialized = 1; strcpy(buffer, "Nparty-update"); }
    return 2;
}
/* Native P's tail resolves the slot; adopting mode-2 companions emits N
 * through the same shared buffer. Keep that side effect in this boundary. */
char *CHAR_makeStatusString(int index, char *category) {
    strcpy(buffer, "P50|100|41|32");
    int length = strlen(buffer);
    snprintf(buffer+length, sizeof(buffer)-length, "|%d", stone_app_primary_slot(7));
    return buffer;
}
void lssproto_S_send(int fd, char *data) {
    assert(strcmp(data, "P50|100|41|32|2") == 0);
}
'''
        harness += text[begin:end]
        harness += '\nint main(void) { assert(_CHAR_sendStatusString(1, "P", __FILE__, __LINE__)); return 0; }\n'
        with tempfile.TemporaryDirectory(prefix='stone-status-init-') as directory:
            root = Path(directory)
            (root / 'status.c').write_text(harness)
            subprocess.run(['cc', '-O2', str(root / 'status.c'), '-o', str(root / 'status')],
                           check=True, capture_output=True)
            result = subprocess.run([str(root / 'status')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
