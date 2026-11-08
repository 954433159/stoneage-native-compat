"""Local HTTP account contract, backed by the same database as native SAAC/GMSV.

Account fields follow the original 175 PHP handlers plus the target client's
three public character preview fields. Gameplay remains in native GMSV/SAAC.
"""
from __future__ import annotations

import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import secrets
import sys
from urllib.parse import parse_qs, urlsplit

import pymysql
import pymysql.cursors

PROJECT = Path(__file__).resolve().parent
_TOOLS = PROJECT / "tools"
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))
from endpoint_policy import health, http_bind_host, server_list, update_document


class LoginError(ValueError):
    pass


def password_hash(password: str, salt: str | None = None) -> str:
    salt = salt or secrets.token_hex(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode(), bytes.fromhex(salt), 200_000)
    return "pbkdf2_sha256$200000$" + salt + "$" + digest.hex()


class AccountStore:
    def __init__(self, settings: dict):
        self.settings = settings

    def connection(self):
        return pymysql.connect(host="127.0.0.1", port=self.settings["db_port"],
                               user=self.settings["db_user"], password=self.settings["db_password"],
                               database=self.settings["database"], charset="utf8mb4",
                               cursorclass=pymysql.cursors.DictCursor, connect_timeout=3)

    def register(self, name: str, password: str) -> int:
        if not re.fullmatch(r"[0-9]{5,15}", name) or not 6 <= len(password) <= 72:
            raise LoginError("账号应为 5 至 15 位数字，密码至少 6 位")
        with self.connection() as db, db.cursor() as cursor:
            cursor.execute("SELECT Id FROM csaloginmaster WHERE Name=%s", (name,))
            if cursor.fetchone():
                raise LoginError("账号已存在")
            cursor.execute("INSERT INTO csaloginmaster (Name,PassWord,api_token,created_at,updated_at) VALUES (%s,%s,'',NOW(),NOW())", (name, password_hash(password)))
            db.commit()
            return cursor.lastrowid

    def login(self, name: str, password: str) -> str:
        with self.connection() as db, db.cursor() as cursor:
            cursor.execute("SELECT Id,PassWord FROM csaloginmaster WHERE Name=%s", (name,))
            row = cursor.fetchone()
            encoded = row["PassWord"] if row else ""
            try:
                verified = hmac.compare_digest(encoded, password_hash(password, encoded.split("$")[2]))
            except (ValueError, IndexError):
                verified = False
            if not verified:
                raise LoginError("账号或密码错误")
            token = secrets.token_hex(32)
            cursor.execute("UPDATE csaloginmaster SET api_token=%s,updated_at=NOW() WHERE Id=%s", (token, row["Id"]))
            db.commit()
            return token

    @staticmethod
    def authorize(cursor, name: str, token: str) -> int:
        if not token:
            raise LoginError("请重新登录")
        cursor.execute("SELECT Id,api_token FROM csaloginmaster WHERE Name=%s FOR UPDATE", (name,))
        row = cursor.fetchone()
        if not row or not hmac.compare_digest(row["api_token"] or "", token):
            raise LoginError("请重新登录")
        return row["Id"]

    def accounts(self, name: str, token: str) -> list[dict]:
        with self.connection() as db, db.cursor() as cursor:
            master = self.authorize(cursor, name, token)
            cursor.execute("SELECT Id,Name,PassWord,Online,OnlineName,Offline FROM csalogin WHERE MasterId=%s ORDER BY Id", (master,))
            rows = list(cursor.fetchall())
        for row in rows:
            row.update(self._character_preview(row))
        return rows

    @staticmethod
    def _character_preview(account: dict) -> dict:
        empty = {"LV": 0, "Trans": 0, "face": 0}
        child, character = account["Name"], account["OnlineName"]
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", child) or not character:
            return empty
        # SAAC util.c:getHash/makeDirFilename and char.c:makeCharFileName.
        # Only the authenticated DB child selects files; neither HTTP nor DB Path
        # supplies a filesystem path. MAXCHAR_PER_USER is two in this build.
        root = (PROJECT / "runtime/saac/char").resolve()
        directory = root / f"0x{sum(child.encode('ascii')) & 0xff:x}"
        escaped_name = character.translate(str.maketrans(
            {"\n": r"\n", ",": r"\c", "|": r"\z", "\\": r"\y"}))
        for slot in range(2):
            path = directory / f"{child}.{slot}.char"
            try:
                # A link must not redirect an owned filename to another save.
                if path.resolve() != path:
                    continue
                with path.open("rb") as saved:
                    raw = saved.read(1024 * 1024 + 1)  # SAAC CHARDATASIZE.
                if len(raw) > 1024 * 1024:
                    continue
                parts = raw.decode("utf-8").split("|", 2)
            except (OSError, UnicodeDecodeError, RuntimeError):
                continue
            if len(parts) != 3 or parts[0] != escaped_name:
                continue
            entries = [part.partition("=") for part in re.split(r"\\[an]", parts[2])]
            preview = {}
            # Target EXE 0x58ac40..0x58acf4 reads integer LV/Trans/face.
            # Match top-level native fields, never similarly named pet fields.
            for key, field, minimum in (("LV", "lv", 1), ("Trans", "trn", 0), ("face", "bi", 1)):
                values = [value for name, separator, value in entries if name == field and separator]
                if (len(values) != 1 or not re.fullmatch(r"[0-9]{1,10}", values[0])
                        or not minimum <= int(values[0]) <= 2**31 - 1):
                    break
                preview[key] = int(values[0])
            if len(preview) == 3:
                return preview
        return empty

    def add_account(self, name: str, token: str, group: int) -> dict:
        if group != 1:
            raise LoginError("请选择本地服务器")
        with self.connection() as db, db.cursor() as cursor:
            master = self.authorize(cursor, name, token)
            cursor.execute("SELECT COUNT(*) AS count FROM csalogin WHERE MasterId=%s", (master,))
            count = cursor.fetchone()["count"]
            if count >= 5:
                raise LoginError("最多创建 5 个小号")
            # Target 0x86381e resolves Q's role slot by splitting Name on '-'.
            # Stay within the original narrowest 12-byte native account fields.
            username = "lc" + secrets.token_hex(4) + "-" + str(count + 1)
            password = secrets.token_hex(16)
            cursor.execute("INSERT INTO csalogin (MasterId,Name,PassWord,RegIP,GroupId,GroupName,ServerName,playeruid,RegTime,created_at,updated_at) VALUES (%s,%s,%s,'127.0.0.1',1,'LocalStone','LocalStone','',NOW(),NOW(),NOW())", (master, username, password))
            result = {"Id": cursor.lastrowid, "Name": username, "PassWord": password, "Online": 0, "OnlineName": "", "Offline": 0,
                      "LV": 0, "Trans": 0, "face": 0}
            db.commit()
            return result


def handler_for(store: AccountStore):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            # Record route/status only: credentials and request bodies do not belong in logs.
            print(json.dumps({"method": self.command, "path": urlsplit(self.path).path, "status": args[1] if len(args) > 1 else ""}), flush=True)

        def reply(self, payload, status=200):
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlsplit(self.path).path
            try:
                if path == "/25server/serverlist.php":
                    self.reply(server_list(store.settings))
                elif path in {"/25server/update/listnew.php", "/25server/update/list.php"}:
                    self.reply(update_document(store.settings))
                elif path == "/health":
                    self.reply(health(store.settings))
                elif path == "/25server/announce.php":
                    body = "本机石器服务器".encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                else:
                    self.reply({"status": 404, "message": "尚未实现的接口"}, 404)
            except ValueError:
                self.reply({"status": 503, "message": "端点配置拒绝外部地址或端口冲突"}, 503)
        def do_POST(self):
            path = urlsplit(self.path).path
            # The SDL client's login.cpp uses these names for the same PHP contract.
            path = {
                "/api/csa-account/sign-in": "/25server/reg/sign-in.php",
                "/api/csa-account/sign-up": "/25server/reg/sign-up.php",
                "/api/csa-account/account-list": "/25server/reg/account-list.php",
                "/api/csa-account/add-sub-account": "/25server/reg/add-sub-account.php",
            }.get(path, path)
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 16384:
                    raise LoginError("请求格式错误")
                raw = self.rfile.read(length).decode("utf-8")
                data = json.loads(raw) if raw.lstrip().startswith("{") else {k: v[-1] for k, v in parse_qs(raw).items()}
                print(json.dumps({"request_path": path, "fields": list(data), "group": data.get("GroupId")}, ensure_ascii=False), flush=True)
                name, password, token = str(data.get("Name", "")), str(data.get("PassWord", "")), str(data.get("api_token", ""))
                if path == "/25server/clientupdate.php":
                    # update.cpp: UpdateAppNewVersion accepts exactly the text "1".
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Content-Length", "1")
                    self.end_headers()
                    self.wfile.write(b"1")
                elif path == "/25server/reg/sign-in.php":
                    self.reply({"status": 200, "message": "", "Token": store.login(name, password)})
                elif path == "/25server/reg/sign-up.php":
                    store.register(name, password)
                    self.reply({"status": 200, "message": "注册成功"})
                elif path == "/25server/reg/account-list.php":
                    self.reply({"status": 200, "message": "", "Account": store.accounts(name, token)})
                elif path == "/25server/reg/add-sub-account.php":
                    # Target 20260805 sends "" for the sole group; the reference
                    # client sends its zero-based index. Both select this instance.
                    if data.get("GroupId", "") not in ("", None, 0, "0", 1, "1"):
                        raise LoginError("请选择本地服务器")
                    store.add_account(name, token, 1)
                    self.reply({"status": 200, "message": "", "Account": store.accounts(name, token)})
                else:
                    self.reply({"status": 404, "message": "尚未实现的接口"}, 404)
            except (LoginError, ValueError, TypeError, UnicodeDecodeError) as error:
                self.reply({"status": 201, "message": str(error) if isinstance(error, LoginError) else "请求格式错误"})
            except pymysql.MySQLError as error:
                print(json.dumps({"database_error": type(error).__name__, "code": error.args[0]}), flush=True)
                self.reply({"status": 503, "message": "本地数据库暂时不可用"}, 503)
    return Handler


if __name__ == "__main__":
    settings = json.loads((PROJECT / "runtime/settings.json").read_text())
    ThreadingHTTPServer((http_bind_host(settings), int(settings["http_port"])), handler_for(AccountStore(settings))).serve_forever()
