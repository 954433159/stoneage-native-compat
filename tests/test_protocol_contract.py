"""Record what the 20260805 client symbols do and do not prove."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Demangled from the authorized arm64 symbol list. These are arities, not a
# captured handshake. The full mangled dump is not republished.
OBSERVED = {
    "version_name": "20260805",
    "symbol_count": 171,
    "AppClientLogin_send": "(int, char*, char*, char*, char*, char*, int, unsigned int, char*)",
    "ClientLogin_recv": "(int, char*)",
    "TK_recv": "(int, int, char*, int, char*, int, bool)",
    "WN_recv": "(int, int, int, int, int, char*)",
    "KS_send": "(int, int)",
    "KS_recv": "(int, int, int)",
    "PETST_send": "(int, int, int)",
    "PETST_recv": "(int, int, int)",
    "I_recv": "(int, char*)",
    "S_recv": "(int, char*)",
}


class ProtocolContractTests(unittest.TestCase):
    def test_login_patch_targets_the_same_version_string(self):
        source = (ROOT / "native/resource-patches.json").read_text(encoding="utf-8")
        self.assertIn("20260805", source)
        self.assertIn("5416d7cd6ef195a0f7622a9c56b55e85", source)
        login = (ROOT / "tests/login_platform.lua").read_text(encoding="utf-8")
        self.assertIn("attempt('android',2,now,false)", login)
        self.assertIn("attempt('win',2,now,false)", login)
        self.assertEqual(OBSERVED["version_name"], "20260805")
        self.assertEqual(OBSERVED["symbol_count"], 171)

    def test_window_and_pet_arities_match_the_native_calls(self):
        mail = (ROOT / "native/app_mail.c").read_text(encoding="utf-8")
        self.assertIn("lssproto_WN_send(s->fd, MAIL_WINDOW, 0, s->sequence, -1, payload)", mail)
        # Client WN_recv(fd, int, int, int, int, char*) has the same arity.
        self.assertEqual(OBSERVED["WN_recv"].count("int"), 5)
        self.assertEqual(OBSERVED["PETST_send"].count("int"), 3)
        self.assertIn("lssproto_PETST_recv(7, slot, slot < 2)",
                      (ROOT / "tests/pet_state.c").read_text(encoding="utf-8"))

    def test_chat_wire_layout_is_not_proven_by_the_recv_arity(self):
        chat = (ROOT / "tests/chat_packet.c").read_text(encoding="utf-8")
        self.assertIn('!memcmp(types,"ISISSII",7)', chat)
        # ISISSII is seven wire fields. The C++ recv signature has six
        # parameters after the connection, so the symbol does not by itself
        # prove that layout.
        after_fd = OBSERVED["TK_recv"].split(",", 1)[1]
        self.assertEqual(after_fd.count(","), 5)
        self.assertIn("I_recv", OBSERVED)
        self.assertIn("S_recv", OBSERVED)

    def test_extension_names_are_not_treated_as_covered(self):
        import re
        public = (ROOT / "native/patches.json").read_text(encoding="utf-8")
        for name in ("1ji5B", "redMoney", "SaMenu", "NewLogout"):
            self.assertIsNone(re.search(r"\b" + name + r"\b", public))


if __name__ == "__main__":
    unittest.main()
