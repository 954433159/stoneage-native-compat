"""Offline backup behavior with temporary assets and fake process/dump boundaries."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class BackupInstanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="stone-backup-test-")
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name)
        self.runtime = self.project / "runtime"
        self.saac = self.runtime / "saac"
        self.assets = {
            "char/0x41/child.0.char": b"saved character with item and pet UIDs",
            "char/0x41/child.note": b"character auxiliary state",
            "char_sleep/sleep.char": b"sleeping character",
            "mail/child.mail": b"native mail",
            "data/family/family.1": b"family membership and assets",
            "data/fmpointdir/point.1": b"family point",
            "acserv.cf": b"local account configuration",
        }
        for name, raw in self.assets.items():
            path = self.saac / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        (self.saac / "data/empty-family-directory").mkdir()
        (self.runtime / "settings.json").write_bytes(b'{"database":"fixture_only"}')
        (self.runtime / "database.pid").write_text("901")
        self.socket = self.project / "fixture.sock"
        self.socket.touch()
        self.live = {901}
        self.ports = set()
        self.settings = dict(database="fixture_only", http_port=19090,
                             game_port=29065, ac_port=21003)
        fake_runtime = types.ModuleType("runtime")
        fake_runtime.PROJECT, fake_runtime.RUNTIME = self.project, self.runtime
        fake_runtime.SETTINGS, fake_runtime.SOCKET = self.settings, self.socket
        fake_runtime.alive = lambda pid: pid in self.live
        fake_runtime.listening = lambda port: port in self.ports
        spec = importlib.util.spec_from_file_location("backup_under_test", ROOT / "tools/backup_instance.py")
        self.tool = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"runtime": fake_runtime}):
            spec.loader.exec_module(self.tool)
        arguments = ["/usr/sbin/mariadbd", "--no-defaults",
                     f"--datadir={self.runtime / 'database'}", f"--socket={self.socket}"]
        self.command = self.enterContext(patch.object(self.tool, "process_command",
                                                       return_value=arguments, create=True))
        self.dump = self.enterContext(patch.object(self.tool.subprocess, "run", side_effect=self.write_dump))

    def write_dump(self, command, *, check, stdout, timeout):
        self.assertEqual(command, ["mariadb-dump", "--no-defaults", "--protocol=socket",
            f"--socket={self.socket}", "--user=root", "--lock-all-tables", "--routines",
            "--events", "--triggers", "--databases", "fixture_only"])
        self.assertTrue(check)
        stdout.write(b"CREATE DATABASE fixture_only;\n")
        return subprocess.CompletedProcess(command, 0)

    def run_backup(self):
        with redirect_stdout(io.StringIO()) as output:
            self.tool.main()
        return Path(json.loads(output.getvalue())["backup"])

    def assert_no_completed_backup(self):
        self.assertEqual(list((self.project / "backups").glob("*/manifest.json")), [])

    def test_complete_saac_assets_and_legacy_char_layout_have_verified_hashes(self):
        destination = self.run_backup()
        for name, raw in self.assets.items():
            path = destination / "runtime/saac" / name
            self.assertTrue(path.is_file(), f"Missing complete SAAC asset: {name}")
            self.assertEqual(path.read_bytes(), raw)
        self.assertTrue((destination / "runtime/saac/data/empty-family-directory").is_dir())
        for name in ("0x41/child.0.char", "0x41/child.note"):
            self.assertEqual((destination / "char" / name).read_bytes(),
                             (self.saac / "char" / name).read_bytes())
        manifest = json.loads((destination / "manifest.json").read_text())
        self.assertEqual(manifest["characters"], 1)
        expected = {path.relative_to(destination).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in destination.rglob("*") if path.is_file() and path.name != "manifest.json"}
        self.assertEqual(manifest["files"], expected)
        self.assertIn("runtime/saac/data/empty-family-directory", manifest["directories"])

    def reject_running_service(self, name):
        (self.runtime / (name + ".pid")).write_text("100")
        self.live.add(100)
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.dump.assert_not_called()
        self.assertFalse((self.project / "backups").exists())

    def test_running_http_refuses_before_copy_or_dump(self):
        self.reject_running_service("http")

    def test_running_gmsv_refuses_before_copy_or_dump(self):
        self.reject_running_service("gmsv")

    def test_running_saac_refuses_before_copy_or_dump(self):
        self.reject_running_service("saac")

    def test_listening_service_without_a_valid_pid_refuses_backup(self):
        self.ports.add(29065)
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.dump.assert_not_called()

    def test_database_pid_must_match_a_database_daemon(self):
        self.command.return_value = ["python", str(self.project / "account_service.py")]
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.dump.assert_not_called()

    def test_database_pid_must_match_this_instances_data_directory(self):
        self.command.return_value = ["/usr/sbin/mariadbd", "--datadir=/other/database", f"--socket={self.socket}"]
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.dump.assert_not_called()

    def test_family_change_during_copy_rejects_the_snapshot(self):
        real_copy = shutil.copytree
        changed = False
        def copy_then_change(*args, **kwargs):
            nonlocal changed
            result = real_copy(*args, **kwargs)
            if not changed:
                changed = True
                (self.saac / "data/family/family.1").write_bytes(b"changed after preflight")
            return result
        with patch.object(self.tool.shutil, "copytree", side_effect=copy_then_change):
            with self.assertRaises(RuntimeError):
                self.run_backup()
        self.assert_no_completed_backup()

    def test_character_change_during_dump_rejects_the_snapshot(self):
        def dump_then_change(*args, **kwargs):
            result = self.write_dump(*args, **kwargs)
            (self.saac / "char/0x41/child.0.char").write_bytes(b"new normal save")
            return result
        self.dump.side_effect = dump_then_change
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.assert_no_completed_backup()

    def test_configuration_change_during_dump_rejects_the_snapshot(self):
        def dump_then_change(*args, **kwargs):
            result = self.write_dump(*args, **kwargs)
            (self.runtime / "settings.json").write_bytes(b"changed config")
            return result
        self.dump.side_effect = dump_then_change
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.assert_no_completed_backup()

    def test_empty_dump_does_not_publish_a_success_manifest(self):
        self.dump.side_effect = lambda *args, **kwargs: subprocess.CompletedProcess(args[0], 0)
        with self.assertRaises(RuntimeError):
            self.run_backup()
        self.assert_no_completed_backup()


if __name__ == "__main__":
    unittest.main()
