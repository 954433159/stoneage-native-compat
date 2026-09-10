"""Real database checks for the new HTTP account contract (run with WSL venv)."""
import json
from pathlib import Path
import secrets
import tempfile
import unittest
from unittest.mock import patch
from http.server import ThreadingHTTPServer
import threading
import urllib.request

from account_service import AccountStore, LoginError, handler_for

PROJECT = Path(__file__).resolve().parents[1]


class AccountTests(unittest.TestCase):
    def setUp(self):
        self.settings = json.loads((PROJECT / "runtime/settings.json").read_text())
        temporary = self.enterContext(tempfile.TemporaryDirectory(prefix="stone-account-test-"))
        self.fixture_project = Path(temporary)
        self.enterContext(patch("account_service.PROJECT", self.fixture_project))
        self.character_directory = self.fixture_project / "runtime/saac/char"
        self.store = AccountStore(self.settings)
        self.name = "8" + str(secrets.randbelow(10**13)).zfill(13)
        self.password = secrets.token_hex(8)
        self.master_ids = [self.store.register(self.name, self.password)]

    def tearDown(self):
        with self.store.connection() as db, db.cursor() as cursor:
            for master_id in self.master_ids:
                cursor.execute("DELETE FROM csalogin WHERE MasterId=%s", (master_id,))
                cursor.execute("DELETE FROM csaloginmaster WHERE Id=%s", (master_id,))
            db.commit()

    def another_master(self):
        name = "8" + str(secrets.randbelow(10**13)).zfill(13)
        password = secrets.token_hex(8)
        self.master_ids.append(self.store.register(name, password))
        return name, self.store.login(name, password)

    def save_preview(self, account, character, *, level=1, trans=0, face=100010,
                     slot=0, delimiter=r"\a", file_character=None):
        # These are the fields/filename written by native SAAC, under a temporary
        # root. No test reads or writes this instance's actual character files.
        folder = self.character_directory / f"0x{sum(account['Name'].encode('ascii')) & 0xff:x}"
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f"{account['Name']}.{slot}.char"
        escaped_name = (file_character if file_character is not None else character).translate(
            str.maketrans({"\n": r"\n", ",": r"\c", "|": r"\z", "\\": r"\y"}))
        path.write_text(escaped_name + r"|0\z0|" + delimiter.join([
            "", f"bi={face}", f"lv={level}", f"trn={trans}", "gld=987654",
            r"pet0=name:pet\zlv:99\zbi:100999", "caa16=private-receipt", ""]),
            encoding="utf-8")
        with self.store.connection() as db, db.cursor() as cursor:
            cursor.execute("UPDATE csalogin SET OnlineName=%s,Path=%s WHERE Id=%s",
                           (character, f"char/{folder.name}", account["Id"]))
            db.commit()
        return path

    def start_http(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(self.store))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def close():
            server.shutdown()
            server.server_close()
            thread.join()

        self.addCleanup(close)
        return f"http://127.0.0.1:{server.server_port}"

    def post(self, base, route, token, **fields):
        body = json.dumps({"Name": self.name, "api_token": token, **fields}).encode()
        request = urllib.request.Request(base + route, data=body)
        with urllib.request.urlopen(request, timeout=3) as response:
            return json.load(response)

    def test_wrong_password_rejected_and_login_rotates_token(self):
        with self.assertRaises(LoginError):
            self.store.login(self.name, "wrong-password")
        old = self.store.login(self.name, self.password)
        new = self.store.login(self.name, self.password)
        self.assertNotEqual(old, new)
        with self.assertRaises(LoginError):
            self.store.accounts(self.name, old)

    def test_created_subaccount_is_the_native_database_account_and_persists(self):
        token = self.store.login(self.name, self.password)
        account = self.store.add_account(self.name, token, 1)
        restarted = AccountStore(self.settings)
        self.assertEqual(restarted.accounts(self.name, token), [account])
        with self.store.connection() as db, db.cursor() as cursor:
            cursor.execute("SELECT Name, PassWord FROM csalogin WHERE Id=%s", (account["Id"],))
            row = cursor.fetchone()
        self.assertEqual(row, {"Name": account["Name"], "PassWord": account["PassWord"]})

    def test_five_native_names_resolve_target_q_slots_and_limit(self):
        token = self.store.login(self.name, self.password)
        children = [self.store.add_account(self.name, token, 1) for _ in range(5)]
        self.assertEqual([int(child['Name'].split('-')[1]) for child in children], [1,2,3,4,5])
        self.assertEqual(len({child['Name'] for child in children}), 5)
        self.assertTrue(all(len(child['Name']) == 12 for child in children))
        with self.assertRaises(LoginError):
            self.store.add_account(self.name, token, 1)
        self.assertEqual(self.store.accounts(self.name, token), children)

    def test_other_master_token_cannot_create_or_list_accounts(self):
        token = self.store.login(self.name, self.password)
        other_name, _ = self.another_master()
        with self.assertRaises(LoginError):
            self.store.add_account(other_name, token, 1)
        with self.assertRaises(LoginError):
            self.store.accounts(other_name, token)

    def test_account_list_returns_target_preview_fields_from_native_saves(self):
        token = self.store.login(self.name, self.password)
        first = self.store.add_account(self.name, token, 1)
        second = self.store.add_account(self.name, token, 1)
        self.save_preview(first, "石头甲", level=1)
        self.save_preview(second, "石头|乙,号\\A", level=2, trans=3, face=100015,
                          slot=1, delimiter=r"\n")
        base = self.start_http()
        result = self.post(base, "/api/csa-account/account-list", token)
        self.assertEqual(result["status"], 200)
        self.assertEqual([{key: row.get(key) for key in ("LV", "Trans", "face")}
                          for row in result["Account"]],
                         [{"LV": 1, "Trans": 0, "face": 100010},
                          {"LV": 2, "Trans": 3, "face": 100015}])
        for row in result["Account"]:
            self.assertEqual(set(row), {"Id", "Name", "PassWord", "Online", "OnlineName",
                                        "Offline", "LV", "Trans", "face"})
            self.assertTrue(all(type(row[key]) is int for key in ("LV", "Trans", "face")))
        restarted = AccountStore(self.settings)
        self.assertEqual(restarted.accounts(self.name, token), result["Account"])
        refreshed = self.post(base, "/api/csa-account/add-sub-account", token, GroupId="")
        self.assertEqual(refreshed["Account"][:2], result["Account"])
        self.assertEqual({key: refreshed["Account"][2][key] for key in ("LV", "Trans", "face")},
                         {"LV": 0, "Trans": 0, "face": 0})

    def test_preview_is_bound_to_authenticated_child_and_character_name(self):
        token = self.store.login(self.name, self.password)
        own = self.store.add_account(self.name, token, 1)
        other_name, other_token = self.another_master()
        other = self.store.add_account(other_name, other_token, 1)
        own_path = self.save_preview(own, "同名人物", level=2)
        self.save_preview(other, "同名人物", level=77, trans=6, face=100099)
        with self.store.connection() as db, db.cursor() as cursor:
            cursor.execute("UPDATE csalogin SET Path='../foreign' WHERE Id=%s", (own["Id"],))
            db.commit()
        result = self.post(self.start_http(), "/25server/reg/account-list.php", token,
                           Path=str(self.character_directory), OnlineName="同名人物",
                           ChildName=other["Name"])
        self.assertEqual([row["Id"] for row in result["Account"]], [own["Id"]])
        self.assertEqual(result["Account"][0].get("LV"), 2)
        own_path.unlink()
        row = self.store.accounts(self.name, token)[0]
        self.assertEqual({key: row.get(key) for key in ("LV", "Trans", "face")},
                         {"LV": 0, "Trans": 0, "face": 0})
        self.save_preview(own, "同名人物", file_character="另一个人物", level=88)
        row = self.store.accounts(self.name, token)[0]
        self.assertEqual({key: row.get(key) for key in ("LV", "Trans", "face")},
                         {"LV": 0, "Trans": 0, "face": 0})

    def test_incomplete_or_invalid_save_does_not_invent_character_preview(self):
        token = self.store.login(self.name, self.password)
        own = self.store.add_account(self.name, token, 1)
        path = self.save_preview(own, "SavedCharacter")
        for malformed in (
            b"", b"\xff", b"SavedCharacter|0|\\alv=2\\abi=100010",
            b"SavedCharacter|0|\\alv=2\\abi=100010\\atrn=0oops\\a",
            b"SavedCharacter|0|\\alv=2147483648\\abi=100010\\atrn=0\\a",
            b"SavedCharacter|0|\\alv=-1\\abi=100010\\atrn=0\\a",
            b"SavedCharacter|0|\\alv=bad\\alv=2\\abi=100010\\atrn=0\\a",
            b"SavedCharacter|0|\\apet0=lv:99\\zbi:100099\\ztrn:6\\a",
        ):
            with self.subTest(malformed=malformed):
                path.write_bytes(malformed)
                row = self.store.accounts(self.name, token)[0]
                self.assertEqual({key: row.get(key) for key in ("LV", "Trans", "face")},
                                 {"LV": 0, "Trans": 0, "face": 0})

    def test_preview_rejects_redirected_files_and_invalid_child_paths(self):
        token = self.store.login(self.name, self.password)
        own = self.store.add_account(self.name, token, 1)
        path = self.save_preview(own, "SavedCharacter", level=5)
        redirected = self.fixture_project / "redirected"
        redirected.mkdir()
        foreign = redirected / path.name
        foreign.write_bytes(path.read_bytes())
        path.unlink()
        path.symlink_to(foreign)
        row = self.store.accounts(self.name, token)[0]
        self.assertEqual(row.get("face"), 0)
        path.unlink()
        path.parent.rmdir()
        path.parent.symlink_to(redirected, target_is_directory=True)
        row = self.store.accounts(self.name, token)[0]
        self.assertEqual(row.get("face"), 0)
        with self.store.connection() as db, db.cursor() as cursor:
            cursor.execute("UPDATE csalogin SET Name='../redirected' WHERE Id=%s", (own["Id"],))
            db.commit()
        row = self.store.accounts(self.name, token)[0]
        self.assertEqual({key: row.get(key) for key in ("LV", "Trans", "face")},
                         {"LV": 0, "Trans": 0, "face": 0})

    def test_original_client_version_check_expects_literal_one(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(self.store))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            request = urllib.request.Request(f"http://127.0.0.1:{server.server_port}/25server/clientupdate.php", data=b"platform=win32&version=1&channel=local")
            with urllib.request.urlopen(request, timeout=3) as response:
                self.assertEqual(response.read(), b"1")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_server_list_matches_the_clients_ordered_two_and_four_field_parser(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(self.store))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{server.server_port}/25server/serverlist.php", timeout=3) as response:
                groups = json.load(response)
            self.assertEqual(list(groups[0]), ["name", "list"])
            self.assertEqual(list(groups[0]["list"][0]), ["name", "ip", "port", "type"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_target_client_empty_group_creates_native_subaccount(self):
        token = self.store.login(self.name, self.password)
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(self.store))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            body = json.dumps({"Name": self.name, "api_token": token, "GroupId": ""}).encode()
            request = urllib.request.Request(f"http://127.0.0.1:{server.server_port}/api/csa-account/add-sub-account", data=body)
            with urllib.request.urlopen(request, timeout=3) as response:
                result = json.load(response)
            self.assertEqual(result["status"], 200)
            self.assertEqual(result["Account"], self.store.accounts(self.name, token))
            self.assertEqual(len(result["Account"]), 1)
            first_id = result["Account"][0]["Id"]
            with urllib.request.urlopen(request, timeout=3) as response:
                result = json.load(response)
            self.assertEqual([row["Id"] for row in result["Account"]],
                             [row["Id"] for row in self.store.accounts(self.name, token)])
            self.assertEqual(len(result["Account"]), 2,
                             "the client replaces its displayed list with this response")
            self.assertEqual(result["Account"][0]["Id"], first_id)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
