"""Retarget every observed 20260805 HTTP channel, not only the first host."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from client_endpoints import (OBSERVED_APP_UPDATE, OBSERVED_DIRECTORY, OBSERVED_JAVA_UPDATE,
                              OBSERVED_RESOURCE_CDN, OBSERVED_RESOURCE_LIST, plan_retarget,
                              retarget_client_blob, survey)


def c_string(url):
    return url.encode("ascii") + b"\x00"


class ClientEndpointTests(unittest.TestCase):
    def observed(self, *extra):
        urls = [OBSERVED_DIRECTORY, OBSERVED_APP_UPDATE, OBSERVED_RESOURCE_LIST,
                OBSERVED_RESOURCE_CDN, OBSERVED_JAVA_UPDATE, *extra]
        return b"HEAD" + b"".join(c_string(url) for url in urls) + b"TAIL"

    def test_one_host_replace_leaves_the_other_update_channels(self):
        original = self.observed()
        # Same length as the historical Windows substitution, including the two spare bytes.
        partial = original.replace(b"http://202.189.23.53:90", b"http://127.0.0.1:19090\x00\x00")
        hosts = survey(partial)["hosts"]
        self.assertIn("shiqiuu.test.upcdn.net", hosts)
        self.assertIn("www.juuding.com", hosts)
        self.assertIn("update", hosts["shiqiuu.test.upcdn.net"]["roles"])
        self.assertNotIn("token-secret", repr(hosts))

    def test_full_plan_moves_list_version_and_resource_channels_together(self):
        secret = "http://203.0.113.50:8080/25server/reg/sign-in.php?token=token-secret"
        blob = self.observed(secret, "https://example.test/help")
        result = retarget_client_blob(blob, "http://10.0.0.2:19090")
        text = result["blob"]
        for foreign in (b"202.189.23.53", b"shiqiuu.test.upcdn.net", b"juuding.com"):
            self.assertNotIn(foreign, text)
        self.assertIn(b"http://10.0.0.2:19090/25server/serverlist.php", text)
        self.assertIn(b"http://10.0.0.2:19090/25server/clientupdate.php", text)
        self.assertIn(b"http://10.0.0.2:19090/25server/update/listnew.php", text)
        self.assertIn(b"http://10.0.0.2:19090/25server/reg/sign-in.php?token=token-secret", text)
        self.assertIn(b"https://example.test/help", text)
        self.assertNotIn("token-secret", repr(survey(blob)))
        roles = {change["role"] for change in result["changes"]}
        self.assertIn("directory", roles)
        self.assertIn("update", roles)
        self.assertIn("account", roles)

    def test_longer_lan_origin_blocks_without_a_partial_rewrite(self):
        blob = self.observed()
        decision = plan_retarget(blob, "http://192.168.1.20:19090")
        self.assertEqual(decision["status"], "blocked")
        self.assertTrue(any(item["kind"] == "url-too-long" for item in decision["problems"]))
        with self.assertRaises(RuntimeError):
            retarget_client_blob(blob, "http://192.168.1.20:19090")
        self.assertIn(b"http://202.189.23.53:90/25server/serverlist.php", blob)

    def test_distinct_update_origin_can_differ_from_the_account_origin(self):
        blob = self.observed()
        result = retarget_client_blob(blob, "http://10.0.0.2:19090", "http://10.0.0.3:19091")
        text = result["blob"]
        self.assertIn(b"http://10.0.0.2:19090/25server/serverlist.php", text)
        self.assertIn(b"http://10.0.0.3:19091/25server/clientupdate.php", text)
        self.assertIn(b"http://10.0.0.3:19091/25server/update/listnew.php", text)
        self.assertIn(b"http://10.0.0.3:19091/", text)

    def test_utf16_update_url_blocks_the_ascii_rewrite(self):
        blob = self.observed() + OBSERVED_RESOURCE_CDN.encode("utf-16le") + b"\x00\x00"
        decision = plan_retarget(blob, "http://10.0.0.2:19090")
        self.assertEqual(decision["status"], "blocked")
        self.assertTrue(any(item["kind"] == "utf16-endpoint" for item in decision["problems"]))

    def test_original_public_origin_is_rejected_as_a_destination(self):
        with self.assertRaises(ValueError):
            plan_retarget(self.observed(), "http://202.189.23.53:90")


if __name__ == "__main__":
    unittest.main()
