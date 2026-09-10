"""Temporary client files only; verify preflight and interrupted deployment recovery."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools import prepare_client_titles as tool

class Recovery(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='stone-client-resource-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.original=self.root/'original'
        for root in (self.original,self.root/'client'):
            (root/'lua').mkdir(parents=True)
            for name,data in [('pcwnd.cf',b'old-ui'),('title.xy',b'old-title'),('cons.cf',b'cons')]:
                (root/'lua'/name).write_bytes(data)
        (self.root/'evidence').mkdir()
        self.outputs={'lua/pcwnd.cf':b'new-ui','lua/title.xy':b'new-title',
                      'compat/pcwnd-original.cf':b'old-ui','lua/win/1199.xy':b'new-local-entry'}
        self.report={'outputs':{n:tool.digest(v)for n,v in self.outputs.items()},
                     'original_cons_sha256':tool.digest(b'cons')}
        for name,value in [('PROJECT',self.root),('ORIGINAL_CLIENT',self.original)]:
            p=patch.object(tool,name,value);p.start();self.addCleanup(p.stop)
        p=patch.object(tool,'candidate',return_value=(self.root/'build',self.outputs,self.report))
        p.start();self.addCleanup(p.stop)

    def test_apply_verify_reapply(self):
        tool.verify_or_apply(apply=True)
        tool.verify_or_apply(verify=True)
        tool.verify_or_apply(apply=True)
        for name,value in self.outputs.items():self.assertEqual((self.root/'client'/name).read_bytes(),value)

    def test_unknown_edit_blocks_all_writes(self):
        path=self.root/'client/lua/title.xy';path.write_bytes(b'local unrelated edit')
        with self.assertRaisesRegex(RuntimeError,'Unrecorded client resource edit'):
            tool.verify_or_apply(apply=True)
        self.assertEqual((self.root/'client/lua/pcwnd.cf').read_bytes(),b'old-ui')
        self.assertEqual(path.read_bytes(),b'local unrelated edit')
        self.assertFalse((self.root/'evidence/client-title-resource-changes.json').exists())

    def test_partial_install_before_manifest_is_recoverable(self):
        (self.root/'client/lua/pcwnd.cf').write_bytes(b'new-ui')
        tool.verify_or_apply(apply=True);tool.verify_or_apply(verify=True)

    def test_verify_never_installs_missing_patch(self):
        with self.assertRaisesRegex(RuntimeError,'not fully applied'):
            tool.verify_or_apply(verify=True)
        self.assertEqual((self.root/'client/lua/pcwnd.cf').read_bytes(),b'old-ui')

    def test_unrecorded_local_entry_blocks_all_writes(self):
        path=self.root/'client/lua/win/1199.xy'
        path.parent.mkdir();path.write_bytes(b'another local window')
        with self.assertRaisesRegex(RuntimeError,'Unrecorded client resource edit'):
            tool.verify_or_apply(apply=True)
        self.assertEqual((self.root/'client/lua/pcwnd.cf').read_bytes(),b'old-ui')
        self.assertEqual(path.read_bytes(),b'another local window')

if __name__=='__main__':unittest.main()
