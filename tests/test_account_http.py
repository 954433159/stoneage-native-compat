"""Exercise the HTTP routes the 20260805 client calls before gameplay."""
import json
import threading
import unittest
import urllib.error
import urllib.request

from account_service import AccountStore, handler_for
from http.server import ThreadingHTTPServer


class AccountHttpTests(unittest.TestCase):
    def setUp(self):
        self.settings = {
            "game_port": 29065, "http_port": 19090, "game_host": "192.0.2.10",
            "db_port": 1, "db_user": "unused", "db_password": "unused", "database": "unused",
        }
        self.store = AccountStore(self.settings)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(self.store))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def test_server_list_advertises_the_game_socket_not_the_web_port(self):
        with urllib.request.urlopen(self.base + "/25server/serverlist.php", timeout=3) as response:
            self.assertEqual(response.status, 200)
            self.assertNotIn("Location", response.headers)
            groups = json.load(response)
        entry = groups[0]["list"][0]
        self.assertEqual([entry["ip"], entry["port"], entry["type"]], ["192.0.2.10", 29065, 1])
        self.assertNotEqual(entry["port"], 90)

    def test_version_check_stays_local_and_resource_list_has_no_foreign_url(self):
        request = urllib.request.Request(self.base + "/25server/clientupdate.php", data=b"platform=android&version=20260805")
        with urllib.request.urlopen(request, timeout=3) as response:
            self.assertEqual(response.read(), b"1")
            self.assertNotIn("Location", response.headers)
        with urllib.request.urlopen(self.base + "/25server/update/listnew.php", timeout=3) as response:
            self.assertEqual(json.load(response), [])
        self.store.settings = {**self.settings, "update_document": [{"url": "http://shiqiuu.test.upcdn.net/patch_1.zip"}]}
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(self.base + "/25server/update/list.php", timeout=3)
        body = raised.exception.read()
        self.assertEqual(raised.exception.code, 503)
        self.assertNotIn(b"shiqiuu.test.upcdn.net", body)
        self.assertNotIn(b"202.189.23.53", body)

    def test_login_is_not_redirected_when_the_database_is_down(self):
        request = urllib.request.Request(
            self.base + "/25server/reg/sign-in.php",
            data=json.dumps({"Name": "10001", "PassWord": "secret-password"}).encode())
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=3)
        body = raised.exception.read()
        self.assertEqual(raised.exception.code, 503)
        self.assertNotIn(b"Location", raised.exception.headers)
        self.assertNotIn(b"secret-password", body)
        self.assertNotIn(b"202.189.23.53", body)


if __name__ == "__main__":
    unittest.main()
