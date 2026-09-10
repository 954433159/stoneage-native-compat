"""Real temporary archives/resources; no game instance, server or database access."""
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools import apply_resource_patches as tool

def digest(raw):return hashlib.sha256(raw).hexdigest()
def change(name,before,after):return dict(file=name,before=before,after=after)

class ResourceRecoveryTests(unittest.TestCase):
    A='gmsv/data/ablua/a.lua'
    B='gmsv/data/ablua/b.lua'

    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='stone-resource-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        (self.root/'evidence').mkdir();(self.root/'native').mkdir()
        self.original={self.A:b"left = 'one'\nright = 'already'\n",self.B:b"value = 'old'\n"}
        self.archive=self.root/'projects.zip'
        with zipfile.ZipFile(self.archive,'w')as bundle:
            for name,raw in self.original.items():bundle.writestr('175sa/'+name,raw)
        self.manifest=dict(archive=str(self.archive),archive_sha256=digest(self.archive.read_bytes()),
            files={n:digest(v)for n,v in self.original.items()})
        self.write_json('evidence/data-manifest.json',self.manifest)
        for name,raw in self.original.items():
            path=self.root/'runtime'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)

    def write_json(self,name,value):
        (self.root/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

    def patches(self,value):self.write_json('native/resource-patches.json',value)
    def current(self,name):return(self.root/'runtime'/name).read_bytes()
    def record(self):return json.loads((self.root/'evidence/resource-changes.json').read_text(encoding='utf-8'))
    def apply(self):
        with patch.object(tool,'PROJECT',self.root),redirect_stdout(io.StringIO()):tool.main()

    def test_reapply_when_after_text_is_not_unique(self):
        self.patches([change(self.A,'one','already')]);self.apply()
        self.assertEqual(self.current(self.A).count(b'already'),2)
        self.apply()
        self.assertEqual(self.record()[self.A]['after_sha256'],digest(self.current(self.A)))

    def test_client_scoped_entry_is_not_applied_to_runtime(self):
        self.patches([change(self.A,'one','two'),
            dict(scope='client',file='lua/title.xy',operation='append-xy')])
        self.apply()
        self.assertEqual(set(self.record()),{self.A})
        self.assertFalse((self.root/'runtime/lua').exists())

    def test_unknown_scope_refuses_before_any_write(self):
        entry=change(self.A,'one','two');entry['scope']='unknown'
        self.patches([entry])
        with self.assertRaisesRegex(RuntimeError,'Unknown resource patch scope'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A])

    def test_accept_new_result_after_interruption_before_manifest_write(self):
        self.patches([change(self.A,'one','two')]);self.apply()
        # A legitimate previous result can advance to a different new patch set.
        self.patches([change(self.A,'one','intermediate')]);self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A].replace(b'one',b'intermediate'))
        previous=(self.root/'evidence/resource-changes.json').read_bytes()
        self.patches([change(self.A,'one','three')])
        expected=self.original[self.A].replace(b'one',b'three')
        (self.root/'runtime'/self.A).write_bytes(expected)
        self.assertEqual((self.root/'evidence/resource-changes.json').read_bytes(),previous)
        self.apply()
        self.assertEqual(self.current(self.A),expected)
        self.assertEqual(self.record()[self.A]['after_sha256'],digest(expected))

    def test_preflight_all_files_before_any_resource_write(self):
        self.patches([change(self.A,'one','two'),change(self.B,'old','new')])
        edited=self.original[self.B]+b'-- user edit\n'
        (self.root/'runtime'/self.B).write_bytes(edited)
        with self.assertRaisesRegex(RuntimeError,'Unrecorded resource edit'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A], 'preflight failure must not write an earlier file')
        self.assertEqual(self.current(self.B),edited)
        self.assertFalse((self.root/'evidence/resource-changes.json').exists())

    def test_nonunique_later_patch_does_not_write_earlier_file(self):
        self.patches([change(self.A,'one','two'),change(self.B,'not present','new')])
        with self.assertRaisesRegex(RuntimeError,'Nonunique resource patch'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A])
        self.assertFalse((self.root/'evidence/resource-changes.json').exists())

    def test_archive_hash_mismatch_is_never_bypassed(self):
        self.patches([change(self.A,'one','two')]);self.apply()
        expected=self.current(self.A);record=self.record()
        self.manifest['archive_sha256']='0'*64
        self.write_json('evidence/data-manifest.json',self.manifest)
        with self.assertRaisesRegex(RuntimeError,'archive hash mismatch'):self.apply()
        self.assertEqual(self.current(self.A),expected);self.assertEqual(self.record(),record)

    def test_original_member_hash_mismatch_is_rejected(self):
        self.patches([change(self.A,'one','two')])
        self.manifest['files'][self.A]='0'*64
        self.write_json('evidence/data-manifest.json',self.manifest)
        with self.assertRaisesRegex(RuntimeError,'Original resource hash mismatch'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A])

    def test_recorded_gb18030_conversion_preserves_exact_newlines(self):
        text='名称 = "原版"\r\n';raw=text.encode('gb18030');prepared=text.encode('utf-8')
        self.original[self.A]=raw
        with zipfile.ZipFile(self.archive,'w')as bundle:
            for name,data in self.original.items():bundle.writestr('175sa/'+name,data)
        self.manifest['archive_sha256']=digest(self.archive.read_bytes())
        self.manifest['files'][self.A]=digest(raw)
        self.manifest['text_conversions']={self.A:dict(**{'from':'gb18030','to':'utf-8'},sha256=digest(prepared))}
        self.write_json('evidence/data-manifest.json',self.manifest)
        (self.root/'runtime'/self.A).write_bytes(prepared)
        self.patches([change(self.A,'原版','新版')]);self.apply();self.apply()
        expected=text.replace('原版','新版').encode('utf-8')
        self.assertEqual(self.current(self.A),expected)
        record=self.record()
        self.manifest['text_conversions'][self.A]['sha256']='0'*64
        self.write_json('evidence/data-manifest.json',self.manifest)
        with self.assertRaisesRegex(RuntimeError,'Converted resource hash mismatch'):self.apply()
        self.assertEqual(self.current(self.A),expected);self.assertEqual(self.record(),record)

    def test_prior_record_must_replay_to_its_recorded_hash(self):
        self.patches([change(self.A,'one','two')]);self.apply()
        forged=self.current(self.A)+b'-- unrecorded edit\n'
        prior=self.record();prior[self.A]['after_sha256']=digest(forged)
        self.write_json('evidence/resource-changes.json',prior)
        (self.root/'runtime'/self.A).write_bytes(forged)
        with self.assertRaisesRegex(RuntimeError,'Previous resource record mismatch'):self.apply()
        self.assertEqual(self.current(self.A),forged);self.assertEqual(self.record(),prior)

    def test_unrecorded_edit_is_rejected_even_when_new_result_is_a_prefix(self):
        self.patches([change(self.A,'one','two')]);self.apply()
        record=self.record();edited=self.current(self.A)+b'-- keep my edit\n'
        (self.root/'runtime'/self.A).write_bytes(edited)
        with self.assertRaisesRegex(RuntimeError,'Unrecorded resource edit'):self.apply()
        self.assertEqual(self.current(self.A),edited);self.assertEqual(self.record(),record)

    def test_deletion_patch_and_removed_patch_are_recoverable(self):
        self.patches([change(self.A,'one','')]);self.apply();self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A].replace(b'one',b''))
        self.patches([]);self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A])
        self.assertEqual(self.record()[self.A]['patches'],[])

    def test_path_escape_is_rejected_before_writes(self):
        outside=self.root/'outside.lua';outside.write_bytes(b'outside')
        self.patches([change(self.A,'one','two'),change('../outside.lua','outside','changed')])
        with self.assertRaisesRegex(RuntimeError,'Resource path escaped'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A]);self.assertEqual(outside.read_bytes(),b'outside')

    def test_duplicate_archive_member_is_rejected(self):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning)
            with zipfile.ZipFile(self.archive,'a')as bundle:bundle.writestr('175sa/'+self.A,self.original[self.A])
        self.manifest['archive_sha256']=digest(self.archive.read_bytes())
        self.write_json('evidence/data-manifest.json',self.manifest)
        self.patches([change(self.A,'one','two')])
        with self.assertRaisesRegex(RuntimeError,'nonunique recorded archive resource'):self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A])

    def test_late_resource_edit_is_rejected_after_read_only_plan(self):
        self.patches([change(self.A,'one','two'),change(self.B,'old','new')])
        plan=tool.build_plan(self.root)
        self.assertEqual(self.current(self.A),self.original[self.A])
        edited=self.original[self.B]+b'-- late edit\n';(self.root/'runtime'/self.B).write_bytes(edited)
        with self.assertRaisesRegex(RuntimeError,'Resource changed after preflight'):tool.apply_plan(plan)
        self.assertEqual(self.current(self.A),self.original[self.A]);self.assertEqual(self.current(self.B),edited)
        self.assertFalse((self.root/'evidence/resource-changes.json').exists())

    def test_late_patch_edit_is_rejected(self):
        self.patches([change(self.A,'one','two')]);plan=tool.build_plan(self.root)
        self.patches([change(self.A,'one','three')])
        with self.assertRaisesRegex(RuntimeError,'Resource inputs changed after preflight'):tool.apply_plan(plan)
        self.assertEqual(self.current(self.A),self.original[self.A])

    def test_replace_interruption_leaves_only_recoverable_complete_files(self):
        self.patches([change(self.A,'one','two'),change(self.B,'old','new')])
        original_replace=tool.os.replace;calls=[]
        def interrupt(source,destination):
            calls.append(destination)
            if len(calls)==2:raise OSError('injected replace interruption')
            return original_replace(source,destination)
        with patch.object(tool.os,'replace',interrupt),self.assertRaisesRegex(OSError,'injected replace interruption'):
            self.apply()
        self.assertEqual(self.current(self.A),self.original[self.A].replace(b'one',b'two'))
        self.assertEqual(self.current(self.B),self.original[self.B])
        self.assertFalse((self.root/'evidence/resource-changes.json').exists())
        self.assertEqual(list(self.root.rglob('*.tmp')),[])
        self.apply();self.apply()
        self.assertEqual(self.current(self.B),self.original[self.B].replace(b'old',b'new'))
        for name in [self.A,self.B]:self.assertEqual(self.record()[name]['after_sha256'],digest(self.current(name)))

    def test_staging_error_keeps_all_destinations_unchanged(self):
        self.patches([change(self.A,'one','two'),change(self.B,'old','new')])
        with patch.object(tool.os,'fsync',side_effect=OSError('injected staging error')):
            with self.assertRaisesRegex(OSError,'injected staging error'):self.apply()
        for name in [self.A,self.B]:self.assertEqual(self.current(name),self.original[name])
        self.assertFalse((self.root/'evidence/resource-changes.json').exists())
        self.assertEqual(list(self.root.rglob('*.tmp')),[])

if __name__=='__main__':unittest.main()
