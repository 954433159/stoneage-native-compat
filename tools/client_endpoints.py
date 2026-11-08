"""Classify client HTTP endpoints and retarget them without fetching them.

The 20260805 SDL client stores the server list, the app-version check and the
resource list as separate ASCII URLs. A resource CDN prefix and a Java update
URL were also present as static strings. Replacing only the first host leaves
those update channels on the original servers. This module plans a same-length
rewrite and refuses to apply part of it. It never opens a connection.
"""
from __future__ import annotations

from urllib.parse import urlsplit
import re

# Static strings from the authorized client survey. Not live endpoints.
OBSERVED_DIRECTORY = "http://202.189.23.53:90/25server/serverlist.php"
OBSERVED_APP_UPDATE = "http://202.189.23.53:90/25server/clientupdate.php"
OBSERVED_RESOURCE_LIST = "http://202.189.23.53:90/25server/update/listnew.php"
OBSERVED_RESOURCE_CDN = "http://shiqiuu.test.upcdn.net/"
OBSERVED_JAVA_UPDATE = "http://www.juuding.com/182server/update/update.json"
DIRECTORY_HOST = "202.189.23.53"
FOREIGN_UPDATE_HOSTS = frozenset({
    DIRECTORY_HOST,
    "shiqiuu.test.upcdn.net",
    "www.juuding.com",
    "juuding.com",
})
_ASCII_URL = re.compile(br"https?://[^\x00\s\"'<>\\]{1,300}(?=\x00)")


def _origin(value: str) -> str:
    parts = urlsplit(value.strip())
    if parts.scheme not in {"http", "https"} or not parts.hostname or parts.username or parts.password:
        raise ValueError("origin must be scheme://host[:port]")
    if parts.path not in {"", "/"} or parts.query or parts.fragment:
        raise ValueError("origin must not include a path or query")
    if parts.hostname in FOREIGN_UPDATE_HOSTS:
        raise ValueError("origin still names an original update host")
    return f"{parts.scheme}://{parts.netloc}"


def classify(url: str) -> str:
    parts = urlsplit(url)
    host = (parts.hostname or "").lower()
    path = parts.path.lower()
    if host in FOREIGN_UPDATE_HOSTS - {DIRECTORY_HOST}:
        return "update"
    if "/25server/reg/" in path or "/api/csa-account/" in path:
        return "account"
    if path.endswith("/serverlist.php") or path.endswith("/announce.php"):
        return "directory"
    if "clientupdate" in path or "/update/" in path or path.endswith("/list.php") or path.endswith("/listnew.php"):
        return "update"
    if host == DIRECTORY_HOST:
        return "same-origin"
    return "unclassified"


def _ascii_items(blob: bytes) -> list[dict]:
    items = []
    for match in _ASCII_URL.finditer(blob):
        url = match.group().decode("ascii")
        parts = urlsplit(url)
        items.append({
            "encoding": "ascii",
            "start": match.start(),
            "end": match.end() + 1,
            "url": url,
            "host": (parts.hostname or "").lower(),
            "origin": f"{parts.scheme}://{parts.netloc}",
            "suffix": parts.path + (f"?{parts.query}" if parts.query else ""),
            "role": classify(url),
            "query_keys": sorted(set(re.findall(r"(?:^|&)([^=&]+)", parts.query))),
        })
    return items


def _utf16_items(blob: bytes) -> list[dict]:
    items = []
    for scheme in (b"http://", b"https://"):
        needle = scheme.decode().encode("utf-16le")
        start = 0
        while True:
            at = blob.find(needle, start)
            if at < 0:
                break
            chars = []
            cursor = at
            while cursor + 1 < len(blob) and len(chars) < 300:
                unit = blob[cursor:cursor + 2]
                if unit == b"\x00\x00":
                    break
                if unit[1] != 0 or not 32 <= unit[0] <= 126:
                    chars = []
                    break
                chars.append(chr(unit[0]))
                cursor += 2
            start = at + 2
            if not chars:
                continue
            url = "".join(chars)
            parts = urlsplit(url)
            if parts.scheme not in {"http", "https"} or not parts.hostname:
                continue
            items.append({
                "encoding": "utf-16le",
                "host": parts.hostname.lower(),
                "role": classify(url),
                "query_keys": sorted(set(re.findall(r"(?:^|&)([^=&]+)", parts.query))),
            })
    return items


def survey(blob: bytes) -> dict:
    """Host and role summary. Query values are never returned."""
    ascii_items = _ascii_items(blob)
    utf16_items = _utf16_items(blob)
    hosts = {}
    for item in ascii_items + utf16_items:
        hosts.setdefault(item["host"], {"roles": set(), "encodings": set()})
        hosts[item["host"]]["roles"].add(item["role"])
        hosts[item["host"]]["encodings"].add(item["encoding"])
    return {
        "ascii_count": len(ascii_items),
        "utf16_count": len(utf16_items),
        "hosts": {
            host: {"roles": sorted(info["roles"]), "encodings": sorted(info["encodings"])}
            for host, info in sorted(hosts.items())
        },
    }


def plan_retarget(blob: bytes, account_origin: str, update_origin: str | None = None) -> dict:
    account = _origin(account_origin)
    update = _origin(update_origin) if update_origin else account
    problems = []
    rewrites = []
    unchanged = []
    for item in _utf16_items(blob):
        if item["role"] in {"account", "directory", "update", "same-origin"}:
            problems.append({"kind": "utf16-endpoint", "role": item["role"], "host": item["host"]})
    for item in _ascii_items(blob):
        must = item["role"] in {"account", "directory", "update", "same-origin"}
        target = update if item["role"] == "update" else account
        if not must:
            unchanged.append({"host": item["host"], "role": item["role"]})
            continue
        if item["origin"] == target:
            continue
        after = target + item["suffix"]
        slot = item["end"] - item["start"]
        if len(after.encode("ascii")) + 1 > slot:
            problems.append({
                "kind": "url-too-long", "role": item["role"], "host": item["host"],
                "capacity": slot - 1, "needed": len(after),
            })
            continue
        rewrites.append({
            "start": item["start"], "end": item["end"], "role": item["role"],
            "host": item["host"], "before": item["url"], "after": after,
        })
    status = "blocked" if problems else "ready"
    return {"status": status, "rewrites": rewrites, "problems": problems, "unchanged": unchanged}


def retarget_client_blob(blob: bytes, account_origin: str, update_origin: str | None = None) -> dict:
    decision = plan_retarget(blob, account_origin, update_origin)
    if decision["status"] != "ready":
        summary = [{"kind": item["kind"], "role": item["role"], "host": item["host"]}
                   for item in decision["problems"]]
        raise RuntimeError("Refusing partial client retarget: " + repr(summary))
    output = bytearray(blob)
    changes = []
    for item in decision["rewrites"]:
        encoded = item["after"].encode("ascii") + b"\x00"
        output[item["start"]:item["end"]] = encoded.ljust(item["end"] - item["start"], b"\x00")
        changes.append({"offset": item["start"], "role": item["role"],
                        "before": item["before"], "after": item["after"]})
    if not any(change["before"].endswith("/25server/serverlist.php")
               or change["after"].endswith("/25server/serverlist.php") for change in changes):
        if not account_origin.encode("ascii") + b"/25server/serverlist.php" in output:
            raise RuntimeError("Server list URL not found")
    return {"blob": bytes(output), "changes": changes, "unchanged": decision["unchanged"]}
