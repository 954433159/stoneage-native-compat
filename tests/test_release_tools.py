"""Publication checks must examine staged bytes and fail on live-state leakage."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bootstrap_release import checked_inputs
from scan_public_release import scan


class ReleaseChecks(unittest.TestCase):
    def test_private_state_and_secret_values_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'runtime').mkdir();(root/'runtime/settings.json').write_text('{}')
            (root/'code.txt').write_text('gh'+'p_'+'a'*36)
            findings=scan(root,['runtime/settings.json','code.txt'])
            self.assertEqual({f['kind'] for f in findings},{'private-or-generated-state','github-token'})
            self.assertNotIn('a'*36,str(findings))

    def test_private_value_is_checked_without_printing_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);value=b'generated-local-credential'
            (root/'code.txt').write_bytes(value)
            result=scan(root,['code.txt'],[value])
            self.assertEqual(result,[{'file':'code.txt','kind':'known-private-value'}])

    def test_exact_input_hash_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);reference=root/'reference';client=root/'original'
            reference.mkdir();client.mkdir();(reference/'source.c').write_bytes(b'original')
            (client/'target.exe').write_bytes(b'target')
            lock={'reference':[{'path':'source.c','sha256':hashlib.sha256(b'original').hexdigest()}],
                  'client':[{'path':'target.exe','sha256':hashlib.sha256(b'target').hexdigest()}]}
            checked_inputs(reference,client,lock)
            (reference/'source.c').write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError,'hash mismatch'):
                checked_inputs(reference,client,lock)

    def test_index_scan_cannot_be_bypassed_by_cleaning_only_worktree(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            subprocess.run(['git','init','-q',str(root)],check=True)
            (root/'code.txt').write_text('gh'+'p_'+'b'*36)
            subprocess.run(['git','-C',str(root),'add','code.txt'],check=True)
            (root/'code.txt').write_text('clean working tree; staged secret remains')
            result=subprocess.run([sys.executable,str(ROOT/'tools/scan_public_release.py'),
                '--root',str(root),'--git-index'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('github-token',result.stdout)
            self.assertNotIn('b'*36,result.stdout)
            subprocess.run(['git','-C',str(root),'add','code.txt'],check=True)
            safe=subprocess.run([sys.executable,str(ROOT/'tools/scan_public_release.py'),
                '--root',str(root),'--git-index'],capture_output=True,text=True)
            self.assertEqual(safe.returncode,0,safe.stdout+safe.stderr)

    def test_bootstrap_scripts_are_included_in_distribution(self):
        import re
        source=(ROOT/'tools/bootstrap_release.py').read_text(encoding='utf8')
        commands=re.findall(r"run\(sys.executable, '([^']+)'",source)
        self.assertGreaterEqual(len(commands),7)
        self.assertEqual([name for name in commands if not (ROOT/name).is_file()],[])


if __name__=='__main__':unittest.main()
