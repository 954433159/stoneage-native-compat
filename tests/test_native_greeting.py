"""Exercise the actual native greeting appends under the migrated build flags."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class NativeGreetingTests(unittest.TestCase):
    def test_version_and_key_survive_timestamp_append(self):
        source = (PROJECT / 'native/overrides/gmsv/net.c').read_text(encoding='utf-8')
        appends = [line.strip() for line in source.splitlines()
                   if 'connecttime)' in line and ('sprintf(mess' in line or 'sprintf(head' in line
                       or 'snprintf(mess' in line or 'snprintf(head' in line)]
        self.assertEqual(len(appends), 4)
        with tempfile.TemporaryDirectory(prefix='stone-greeting-') as temporary:
            root = Path(temporary)
            for index, statement in enumerate(appends):
                variable = 'mess' if 'mess' in statement else 'head'
                code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct { int connecttime; } Connect[1] = {{1788938888}};
int main(void) {
    int sockfd = 0, fdremember = 0;
    char mess[64] = "N0123456789abcdef";
    char head[1024] = "N0123456789abcdef";
'''
                code += statement + '\n'
                code += f'assert(strcmp({variable}, "N0123456789abcdef1788938888") == 0);\nreturn 0;\n}}\n'
                (root / 'greeting.c').write_text(code)
                subprocess.run(['cc', '-O2', '-D_FORTIFY_SOURCE=2', str(root / 'greeting.c'),
                                '-o', str(root / 'greeting')], check=True, capture_output=True)
                result = subprocess.run([str(root / 'greeting')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, f'branch {index}: {result.stderr}')


if __name__ == '__main__':
    unittest.main()
