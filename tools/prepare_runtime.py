"""Prepare a fresh instance from the data archive shipped beside the source."""
from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import secrets
import zipfile

if __package__:
    from .project_paths import REFERENCE_SOURCE
else:
    from project_paths import REFERENCE_SOURCE

PROJECT = Path(__file__).resolve().parents[1]
ORIGINAL = REFERENCE_SOURCE / "sq_server_game"
RUNTIME = PROJECT / "runtime"


def write_new(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_bytes(data)


def configuration(text: str, values: dict[str, object], separator: str) -> str:
    remaining = dict(values)
    output = []
    for line in text.splitlines():
        stripped = line.strip()
        key = re.split(r"[=\s]", stripped, maxsplit=1)[0]
        if key in values:
            if key in remaining:
                output.append(f"{key}{separator}{remaining.pop(key)}")
        else:
            output.append(line)
    output.extend(f"{key}{separator}{value}" for key, value in remaining.items())
    return "\n".join(output) + "\n"


def main() -> None:
    RUNTIME.mkdir(exist_ok=True)
    settings_path = RUNTIME / "settings.json"
    if not settings_path.exists():
        settings_path.write_text(json.dumps({
            "database": "stoneclean", "db_user": "stoneclean",
            "db_password": secrets.token_hex(12), "ac_password": secrets.token_hex(12),
            "db_port": 23306, "ac_port": 21003, "game_port": 29065, "http_port": 19090,
        }, indent=2) + "\n", encoding="utf-8")
    settings = json.loads(settings_path.read_text(encoding="utf-8"))
    archive = ORIGINAL / "projects.zip"
    manifest = {"archive": str(archive), "archive_sha256": hashlib.file_digest(archive.open("rb"), "sha256").hexdigest(), "files": {}}
    map_formats = Counter()
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            relative = PurePosixPath(entry.filename)
            if entry.is_dir() or ".." in relative.parts or relative.is_absolute():
                continue
            # Only gameplay resources and required static tables; no old saves/logs/accounts.
            if entry.filename.startswith("175sa/gmsv/data/") or entry.filename in {
                "175sa/gmsv/deskey.txt", "175sa/gmsv/desrand.txt", "175sa/gmsv/announce.txt", "175sa/gmsv/loopannounce.txt", "175sa/gmsv/log/log.cf",
            }:
                data = bundle.read(entry)
                destination = RUNTIME / Path(*relative.parts[1:])
                write_new(destination, data)
                manifest["files"][destination.relative_to(RUNTIME).as_posix()] = hashlib.sha256(data).hexdigest()
                if data[:2] == b"LS":
                    map_formats[data[:6].decode("ascii")] += 1
                elif b"\x00" not in data and data:
                    try:
                        data.decode("utf-8")
                    except UnicodeDecodeError:
                        try:
                            converted = data.decode("gb18030").encode("utf-8")
                        except UnicodeDecodeError:
                            continue
                        # A source/data encoding correction; preserve edited runtime files.
                        if destination.read_bytes() == data:
                            destination.write_bytes(converted)
                        manifest.setdefault("text_conversions", {})[destination.relative_to(RUNTIME).as_posix()] = {
                            "from": "gb18030", "to": "utf-8", "sha256": hashlib.sha256(converted).hexdigest(),
                        }
        sql_values = {
            "sql_IP": "127.0.0.1", "sql_Port": settings["db_port"], "sql_ID": settings["db_user"],
            "sql_PS": settings["db_password"], "sql_DataBase": settings["database"],
            "sql_Table": "csalogin", "sql_NAME": "Name", "sql_PASS": "PassWord", "sql_LOCK": "lock", "AutoReg": 0,
        }
        configs = {
            "gmsv/sql.cf": (sql_values, " "),
            "saac/acserv.cf": ({**sql_values, "port": settings["ac_port"], "pass": settings["ac_password"], "CPUUSE": 1, "AutoReg": 0, "AUTOBACKUPDAY": 0}, " "),
            "gmsv/setup.cf": ({
                "acserv": "127.0.0.1", "acservport": settings["ac_port"], "acpasswd": settings["ac_password"],
                "port": settings["game_port"], "gameservname": "LocalStone", "serverip": "127.0.0.1", "topdir": ".",
                "storedir": "../saac/char", "debuglevel": 0,
            }, "="),
        }
        for relative, (values, separator) in configs.items():
            original = bundle.read("175sa/" + relative).decode("utf-8-sig")
            write_new(RUNTIME / relative, configuration(original, values, separator).encode())
    for relative in ["saac/data/family", "saac/data/fmpointdir", "saac/data/fmsmemodir", "saac/data/wklog",
                     "saac/char", "saac/char_sleep", "saac/mail", "saac/log", "saac/lock", "gmsv/log", "gmsv/Schedule", "database", "logs"]:
        (RUNTIME / relative).mkdir(parents=True, exist_ok=True)
    sql = (ORIGINAL / "175sa.sql").read_text(encoding="utf-8-sig")
    # Import DDL only. Never execute the reference dump's INSERT/UPDATE/account data.
    tables = re.findall(r"^CREATE TABLE `[^`]+`.*?^\)[^;]*;", sql, re.M | re.S)
    if not tables:
        raise RuntimeError("No table definitions found")
    ddl = "SET FOREIGN_KEY_CHECKS=0;\n" + "\n\n".join(tables).replace("utf8mb4_0900_ai_ci", "utf8mb4_unicode_ci")
    (RUNTIME / "schema.sql").write_text(ddl + "\n", encoding="utf-8")
    manifest["map_formats"] = dict(map_formats)
    manifest["table_count"] = len(tables)
    (PROJECT / "evidence/data-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Prepared {len(manifest['files'])} game resource files; map formats {dict(map_formats)}; {len(tables)} empty table definitions.")
    print("Instance ports: HTTP 19090, game 29065, SAAC 21003, MariaDB 23306.")
    if (PROJECT / "native/resource-patches.json").exists():
        from apply_resource_patches import main as apply_patches
        apply_patches()


if __name__ == "__main__":
    main()
