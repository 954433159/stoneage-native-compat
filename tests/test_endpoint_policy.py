"""Server-list, game port and update-document policy without a database."""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import endpoint_policy


SETTINGS = {"game_port": 29065, "http_port": 19090}


class EndpointPolicyTests(unittest.TestCase):
    def setUp(self):
        for name in ("STONEAGE_GAME_HOST", "STONEAGE_GAME_PORT", "STONEAGE_HTTP_HOST"):
            os.environ.pop(name, None)

    def test_server_list_keeps_the_client_field_order_and_game_port(self):
        groups = endpoint_policy.server_list(SETTINGS)
        self.assertEqual(list(groups[0]), ["name", "list"])
        entry = groups[0]["list"][0]
        self.assertEqual(list(entry), ["name", "ip", "port", "type"])
        self.assertEqual(entry["ip"], "127.0.0.1")
        self.assertEqual(entry["port"], 29065)
        self.assertTrue(endpoint_policy.health(SETTINGS)["same_machine_only"])

    def test_advertised_game_host_can_differ_from_loopback_http(self):
        settings = {**SETTINGS, "game_host": "192.0.2.10", "http_host": "127.0.0.1"}
        self.assertEqual(endpoint_policy.server_list(settings)[0]["list"][0]["ip"], "192.0.2.10")
        self.assertTrue(endpoint_policy.health(settings)["same_machine_only"])
        os.environ["STONEAGE_HTTP_HOST"] = "192.0.2.10"
        os.environ["STONEAGE_GAME_HOST"] = "192.0.2.10"
        self.assertFalse(endpoint_policy.health(SETTINGS)["same_machine_only"])

    def test_original_web_host_and_port_90_are_not_a_game_endpoint(self):
        with self.assertRaises(ValueError):
            endpoint_policy.server_list({**SETTINGS, "game_host": "202.189.23.53"})
        with self.assertRaises(ValueError):
            endpoint_policy.server_list({**SETTINGS, "game_port": 90})
        with self.assertRaises(ValueError):
            endpoint_policy.server_list({**SETTINGS, "game_port": 19090})
        with self.assertRaises(ValueError):
            endpoint_policy.http_bind_host({**SETTINGS, "http_host": "0.0.0.0"})

    def test_update_document_rejects_foreign_urls_and_the_game_port(self):
        self.assertEqual(endpoint_policy.update_document(SETTINGS), [])
        local = {"url": "http://127.0.0.1:19090/updates/patch_1.zip", "name": "patch_1.zip"}
        self.assertEqual(endpoint_policy.update_document({**SETTINGS, "update_document": [local]}), [local])
        for url in (
            "http://shiqiuu.test.upcdn.net/patch_1.zip",
            "http://www.juuding.com/182server/update/update.json",
            "http://202.189.23.53:90/25server/update/listnew.php",
            "http://127.0.0.1:29065/patch_1.zip",
        ):
            with self.assertRaises(ValueError):
                endpoint_policy.update_document({**SETTINGS, "update_document": [{"url": url}]})


if __name__ == "__main__":
    unittest.main()
