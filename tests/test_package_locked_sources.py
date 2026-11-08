"""The source packer must hash every locked file and omit assets and dumps."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_locked_sources as packer

MARKERS = {
    "sq_server_game/175Gmsv/GameServer/GameServer.vcxproj":
        "0ec34c73dbd3d29149e12711e0a830e2bffca15a6041f8e9e49da86dc06947d4",
    "sq_server_acc/175Server/AccServer.vcxproj":
        "edc41140054d0c8787dca346905bff53dde3e1597bdbbac5d4f5fd4487d7047a",
    "sq_server_game/175sa.sql":
        "3671e9ac14a7b9fa5d72fdeeebf00b83c7699756256838e041c20830289673d1",
    "sq_server_game/projects.zip":
        "a3811ad6597b3bcd7078b062fc4c701ca0307c7afd9d2cfed194a5bfb76c9262",
    "Stoneage_80/lua/lua/VMProtectSDK.h":
        "a8358cfbf53f148c043c6ee3643fcf9d5b712e0c6f0571b43122ed060fc56c9d",
}
LOCAL_EXES = {
    "stoneage.local.exe": "2075d9a8c3e8084e2edb236e2be5d8ac59aa89f390fcf833323e7a9e16307da3",
    "stoneage_local.exe": "6c5187771d767dd34ef99a47763fd9fc1ca744c40090f1d86d83121fb6bdabfa",
}


class PackageLockedSourcesTests(unittest.TestCase):
    def test_reported_markers_match_the_public_lock_and_local_exes_do_not(self):
        lock = packer.load_lock(ROOT / "release-input-lock.json")
        reference = {entry["path"]: entry["sha256"] for entry in lock["reference"]}
        client = {entry["path"]: entry["sha256"] for entry in lock["client"]}
        for path, digest in MARKERS.items():
            self.assertEqual(reference[path], digest)
        self.assertEqual(client["stoneage.exe"],
                         "916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7")
        for name, digest in LOCAL_EXES.items():
            self.assertNotIn(name, client)
            self.assertNotEqual(digest, client["stoneage.exe"])
        layout = packer.locked_layout(lock)
        self.assertEqual(layout["reference_files"], 564)
        self.assertEqual(layout["package_files"], 561)
        self.assertEqual({item["path"] for item in layout["excluded_files"]}, {
            "sq_server_game/175sa.sql",
            "sq_server_game/projects.zip",
            "sq_server_game/175Gmsv/GameServer/char/ls2data.dat",
        })
        self.assertEqual(layout["unknown_files"], 0)
        directories = {item["path"]: item["files"] for item in layout["directories"]}
        self.assertEqual(directories["sq_server_game/175Gmsv/GameServer"], 474)
        self.assertEqual(directories["sq_server_acc/175Server"], 33)
        self.assertEqual(directories["Stoneage_80/lua/lua"], 55)

    def test_package_requires_every_hash_and_skips_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            output = root / "pack"
            files = {
                "sq_server_game/175Gmsv/GameServer/main.c": b"int main(void){return 0;}\n",
                "sq_server_game/175sa.sql": b"CREATE TABLE t(id int);\nINSERT INTO accounts VALUES ('secret');\n",
                "sq_server_game/projects.zip": b"PK commercial-resource",
                "sq_server_game/175Gmsv/GameServer/char/ls2data.dat": b"\x00opaque",
            }
            lock = {"format": 2, "reference": [], "client": []}
            for relative, content in files.items():
                path = source.joinpath(*Path(relative).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                lock["reference"].append({"path": relative, "sha256": hashlib.sha256(content).hexdigest()})
            before = {relative: path.read_bytes() for relative, path in (
                (name, source.joinpath(*Path(name).parts)) for name in files)}
            manifest = packer.package_sources(source, output, lock)
            self.assertEqual(manifest["packaged_files"], 1)
            self.assertEqual((output / "sq_server_game/175Gmsv/GameServer/main.c").read_bytes(),
                             files["sq_server_game/175Gmsv/GameServer/main.c"])
            self.assertFalse((output / "sq_server_game/175sa.sql").exists())
            self.assertFalse((output / "sq_server_game/projects.zip").exists())
            self.assertFalse((output / "sq_server_game/175Gmsv/GameServer/char/ls2data.dat").exists())
            self.assertEqual(before, {relative: source.joinpath(*Path(relative).parts).read_bytes()
                                       for relative in files})
            files["sq_server_game/175Gmsv/GameServer/main.c"] = b"changed\n"
            (source / "sq_server_game/175Gmsv/GameServer/main.c").write_bytes(b"changed\n")
            report = packer.verify_reference(source, lock)
            self.assertFalse(report["ok"])
            self.assertEqual(report["mismatches"][0]["path"], "sq_server_game/175Gmsv/GameServer/main.c")

    def test_symlink_repo_output_and_partial_failure_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "main.c").write_bytes(b"int x;\n")
            outside = root / "outside"
            outside.mkdir()
            link = source / "link.c"
            link.symlink_to(outside / "main.c")
            (outside / "main.c").write_bytes(b"int y;\n")
            digest = hashlib.sha256(b"int x;\n").hexdigest()
            lock = {"format": 2, "reference": [
                {"path": "main.c", "sha256": digest},
                {"path": "link.c", "sha256": hashlib.sha256(b"int y;\n").hexdigest()},
            ], "client": []}
            report = packer.verify_reference(source, lock)
            self.assertFalse(report["ok"])
            self.assertIn("link.c", report["missing"])
            safe = {"format": 2, "reference": [{"path": "main.c", "sha256": digest}], "client": []}
            with self.assertRaises(RuntimeError):
                packer.package_sources(source, ROOT / "native" / "packed-sources", safe)
            with self.assertRaises(RuntimeError):
                packer.package_sources(source, source / "nested-pack", safe)


if __name__ == "__main__":
    unittest.main()
