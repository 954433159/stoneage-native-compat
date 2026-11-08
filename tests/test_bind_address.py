"""Compile the game listen-address helper without the proprietary server."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BindAddressTests(unittest.TestCase):
    def test_loopback_default_and_unicast_selection(self):
        binary = ROOT / "build" / "bind_address_test"
        binary.parent.mkdir(exist_ok=True)
        subprocess.run(
            ["cc", "-Werror", "-I", str(ROOT / "native/include"),
             str(ROOT / "native/bind_address.c"), str(ROOT / "tests/bind_address_test.c"),
             "-o", str(binary)],
            check=True, cwd=ROOT, timeout=30)
        completed = subprocess.run([str(binary)], check=True, cwd=ROOT, text=True,
                                   capture_output=True, timeout=10)
        self.assertIn("PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
