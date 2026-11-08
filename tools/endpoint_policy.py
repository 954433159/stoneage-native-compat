"""HTTP account, server-list and update responses for this instance.

The 20260805 client reads three different URLs for the server list, the app
version check and the resource list. Port 90 on the original web host is not
the game TCP port. Login stays on this process; update documents may not name
the original web, CDN or Java update hosts.
"""
from __future__ import annotations

import os
import re
from urllib.parse import urlsplit

from client_endpoints import FOREIGN_UPDATE_HOSTS

_IPV4 = re.compile(r"(?:\d{1,3}\.){3}\d{1,3}\Z")
_NAME = re.compile(r"[A-Za-z0-9.-]{1,253}\Z")


def _ipv4_parts(value: str) -> list[int] | None:
    if not _IPV4.fullmatch(value):
        return None
    parts = [int(part) for part in value.split(".")]
    if any(part > 255 for part in parts):
        return None
    return parts


def require_host(value: object, *, label: str, allow_name: bool = False) -> str:
    text = str(value).strip()
    if text.lower() in FOREIGN_UPDATE_HOSTS:
        raise ValueError(label + " still names an original public server")
    parts = _ipv4_parts(text)
    if parts is not None:
        if parts[0] >= 224 or parts == [0, 0, 0, 0] or parts == [255, 255, 255, 255]:
            raise ValueError(label + " is not one IPv4 unicast address")
        return text
    if allow_name and _NAME.fullmatch(text) and ".." not in text and not text.startswith("."):
        return text
    raise ValueError(label + " is not a usable host")


def game_endpoint(settings: dict) -> tuple[str, int]:
    host = require_host(os.environ.get("STONEAGE_GAME_HOST") or settings.get("game_host") or "127.0.0.1",
                        label="game_host", allow_name=True)
    port = int(os.environ.get("STONEAGE_GAME_PORT") or settings["game_port"])
    http_port = int(settings["http_port"])
    if not 1 <= port <= 65535:
        raise ValueError("game_port out of range")
    if port == http_port or port == 90:
        raise ValueError("game_port must be the game socket, not the HTTP list port")
    return host, port


def http_bind_host(settings: dict) -> str:
    return require_host(os.environ.get("STONEAGE_HTTP_HOST") or settings.get("http_host") or "127.0.0.1",
                        label="http_host")


def server_list(settings: dict) -> list[dict]:
    host, port = game_endpoint(settings)
    # Key order is the client's two-level, four-field parser.
    return [{"name": "我的石器", "list": [{"name": "本机一线", "ip": host, "port": port, "type": 1}]}]


def _allowed_origins(settings: dict) -> set[str]:
    # Resource URLs stay on HTTP. The game TCP port is not an update origin.
    http_host = http_bind_host(settings)
    allowed = {f"http://{http_host}:{int(settings['http_port'])}"}
    extra = settings.get("update_origin")
    if extra:
        parts = urlsplit(str(extra).strip())
        if parts.scheme not in {"http", "https"} or not parts.hostname or parts.path not in {"", "/"}:
            raise ValueError("update_origin must be an origin")
        if parts.hostname.lower() in FOREIGN_UPDATE_HOSTS:
            raise ValueError("update_origin still names an original public server")
        allowed.add(f"{parts.scheme}://{parts.netloc}")
    return allowed


def update_document(settings: dict):
    document = settings.get("update_document", [])
    if document is None:
        document = []
    if not isinstance(document, list):
        raise ValueError("update document must be a list")
    allowed = _allowed_origins(settings)

    def walk(value):
        if isinstance(value, str):
            if value.startswith(("http://", "https://")):
                parts = urlsplit(value)
                origin = f"{parts.scheme}://{parts.netloc}"
                if (parts.hostname or "").lower() in FOREIGN_UPDATE_HOSTS or origin not in allowed:
                    raise ValueError("foreign update url")
        elif isinstance(value, list):
            for item in value:
                walk(item)
        elif isinstance(value, dict):
            for item in value.values():
                walk(item)
        elif not isinstance(value, (int, float, bool)) and value is not None:
            raise ValueError("unsupported update value")

    walk(document)
    return document


def health(settings: dict) -> dict:
    game_host, game_port = game_endpoint(settings)
    bind = http_bind_host(settings)
    return {
        "account_service": "ready",
        "game_host": game_host,
        "game_port": game_port,
        "http_host": bind,
        "http_port": int(settings["http_port"]),
        "same_machine_only": game_host == "127.0.0.1" or bind == "127.0.0.1",
    }
