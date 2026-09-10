"""Path portability checks using isolated copies; never prepare a live instance."""
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import apply_resource_patches


class MigratedPathTests(unittest.TestCase):
    def test_resource_preflight_reads_the_other_hosts_absolute_path(self):
        # A manifest saved in Windows must work in WSL and vice versa. Hash
        # mismatch must still reject the archive before accepting any resource.
        drive_directory = ROOT if str(ROOT).startswith("/mnt/") else None
        with tempfile.TemporaryDirectory(prefix="stone-manifest-paths-", dir=drive_directory) as temporary:
            root = Path(temporary)
            for directory in ("evidence", "native", "runtime/gmsv/data"):
                (root / directory).mkdir(parents=True)
            archive = root / "projects.zip"
            original = b"old resource\n"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("175sa/gmsv/data/example.lua", original)
            if os.name == "nt":
                absolute = PureWindowsPath(archive)
                recorded = "/mnt/" + absolute.drive[0].lower() + "/" + "/".join(absolute.parts[1:])
            else:
                # WSL test temp roots should reside on the project's drive.
                if not str(archive).startswith("/mnt/"):
                    self.skipTest("Reverse Windows path needs a WSL drive-backed temporary directory")
                absolute = archive.parts
                recorded = absolute[2].upper() + ":\\" + "\\".join(absolute[3:])
            manifest = {
                "archive": recorded,
                "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                "files": {"gmsv/data/example.lua": hashlib.sha256(original).hexdigest()},
            }
            manifest_path = root / "evidence/data-manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            (root / "native/resource-patches.json").write_text(json.dumps([
                {"file": "gmsv/data/example.lua", "before": "old", "after": "new"}
            ]), encoding="utf-8")
            resource = root / "runtime/gmsv/data/example.lua"
            resource.write_bytes(original)
            plan = apply_resource_patches.build_plan(root)
            self.assertEqual(plan.resources[0].result, b"new resource\n")
            self.assertEqual(resource.read_bytes(), original)
            self.assertFalse((root / "evidence/resource-changes.json").exists())
            manifest["archive_sha256"] = "0" * 64
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "archive hash mismatch"):
                apply_resource_patches.build_plan(root)

    def test_prepare_tools_read_the_migrated_sibling_inputs(self):
        # Reusing the old machine's absolute paths must fail this check even if
        # those paths happen to exist on the developer's machine.
        with tempfile.TemporaryDirectory(prefix="stone-migration-paths-") as temporary:
            base = Path(temporary)
            project = base / "stoneage-clean"
            tool_dir = project / "tools"
            tool_dir.mkdir(parents=True)
            names = ("prepare_sources.py", "prepare_runtime.py", "prepare_client.py",
                     "prepare_client_tasks.py", "project_paths.py")
            for name in names:
                source = ROOT / "tools" / name
                if source.exists():
                    shutil.copyfile(source, tool_dir / name)
            reference = base / "sqsd/第一期修复的175源码"
            fixtures = {
                reference / "sq_server_game/175Gmsv/GameServer/source.c": b"game source",
                reference / "sq_server_acc/175Server/source.c": b"account source",
                reference / "sq_server_game/projects.zip": b"archive",
                base / "stoneage/stoneage.exe": b"original client",
                base / "stoneage/data/task.bin": b"task resource",
            }
            for path, content in fixtures.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            code = """
import json, prepare_sources, prepare_runtime, prepare_client, prepare_client_tasks
paths = [prepare_sources.COMPONENTS['gmsv'] / 'source.c',
         prepare_sources.COMPONENTS['saac'] / 'source.c',
         prepare_runtime.ORIGINAL / 'projects.zip',
         prepare_client.ORIGINAL / 'stoneage.exe', prepare_client_tasks.ORIGINAL]
print(json.dumps([path.read_bytes().decode('ascii') for path in paths]))
"""
            result = subprocess.run([sys.executable, "-c", code], cwd=tool_dir,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout),
                             ["game source", "account source", "archive",
                              "original client", "task resource"])
            self.assertFalse((project / "runtime").exists())
            self.assertFalse((project / "evidence").exists())


if __name__ == "__main__":
    unittest.main()
