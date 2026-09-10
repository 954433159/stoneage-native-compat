"""Start and inspect only this project's local native instance (run inside WSL)."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import signal
import subprocess
import time

PROJECT = Path(__file__).resolve().parents[1]
RUNTIME = PROJECT / "runtime"
SETTINGS = json.loads((RUNTIME / "settings.json").read_text())
SOCKET = Path("/tmp") / ("stoneclean-" + hashlib.sha256(str(PROJECT).encode()).hexdigest()[:12] + ".sock")


def alive(pid: int) -> bool:
    try:
        command = Path(f"/proc/{pid}/cmdline").read_bytes()
        return str(PROJECT).encode() in command
    except FileNotFoundError:
        return False


def listening(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def start_process(name: str, command: list[str], cwd: Path, port: int) -> None:
    pidfile = RUNTIME / (name + ".pid")
    if pidfile.exists() and alive(int(pidfile.read_text())):
        print(f"{name}: already running; port {port} listening={listening(port)}")
        return
    if listening(port):
        raise RuntimeError(f"Port {port} belongs to another process; refusing to reuse it")
    with (RUNTIME / "logs" / (name + ".log")).open("ab") as log:
        environment = os.environ.copy()
        if name == 'gmsv':
            environment['STONEAGE_LOCAL_DUNGEONS'] = '1'
        process = subprocess.Popen(command, cwd=cwd, env=environment, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    pidfile.write_text(str(process.pid))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{name} exited with {process.returncode}; inspect runtime/logs/{name}.log")
        if listening(port):
            print(f"{name}: pid {process.pid}, listening on 127.0.0.1:{port}")
            return
        time.sleep(0.25)
    raise RuntimeError(f"{name} startup still pending; inspect runtime/logs/{name}.log")


def mysql(sql: str, database: str | None = None) -> None:
    command = ["mariadb", "--no-defaults", "--protocol=socket", f"--socket={SOCKET}", "--user=root", "--default-character-set=utf8mb4"]
    if database:
        command.append(database)
    subprocess.run(command, input=sql, text=True, check=True, stdout=subprocess.DEVNULL)


def database() -> None:
    datadir = RUNTIME / "database"
    if not (datadir / "mysql").is_dir():
        with (RUNTIME / "logs/database-install.log").open("wb") as log:
            subprocess.run(["mariadb-install-db", "--no-defaults", f"--datadir={datadir}", "--auth-root-authentication-method=normal", "--skip-test-db"], stdout=log, stderr=subprocess.STDOUT, check=True)
    start_process("database", [
        "/usr/sbin/mariadbd", "--no-defaults", f"--datadir={datadir}", f"--socket={SOCKET}",
        f"--pid-file={RUNTIME / 'mariadb-native.pid'}", "--bind-address=127.0.0.1", f"--port={SETTINGS['db_port']}",
        "--lower-case-table-names=1", "--skip-name-resolve", "--innodb-buffer-pool-size=64M", "--sql-mode=NO_ENGINE_SUBSTITUTION",
    ], RUNTIME, SETTINGS["db_port"])
    marker = RUNTIME / "schema-imported"
    if not marker.exists():
        # Names/passwords here are generated locally by prepare_runtime, never HTTP input.
        mysql(f"CREATE DATABASE IF NOT EXISTS `{SETTINGS['database']}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
              f"CREATE USER IF NOT EXISTS '{SETTINGS['db_user']}'@'127.0.0.1' IDENTIFIED BY '{SETTINGS['db_password']}';"
              f"GRANT ALL ON `{SETTINGS['database']}`.* TO '{SETTINGS['db_user']}'@'127.0.0.1';")
        schema = (RUNTIME / "schema.sql").read_text().replace("CREATE TABLE `", "CREATE TABLE IF NOT EXISTS `")
        mysql(schema, SETTINGS["database"])
        marker.write_text("Empty original table definitions imported; no historical records.\n")
        print("Imported the original empty schema.")
    for migration in sorted((PROJECT / 'native/migrations').glob('*.sql')):
        mysql(migration.read_text(encoding='utf-8'), SETTINGS['database'])


def status() -> None:
    for name, key in [("database", "db_port"), ("saac", "ac_port"), ("gmsv", "game_port"), ("http", "http_port")]:
        pidfile = RUNTIME / (name + ".pid")
        pid = int(pidfile.read_text()) if pidfile.exists() else 0
        print(f"{name}: pid={pid}, own_process={alive(pid)}, listening={listening(SETTINGS[key])}")


def ready() -> None:
    """Wait for this native run's game loop, not just its early-opened socket."""
    # DrvFS cold loading of the original maps/NPCs can exceed three minutes.
    deadline = time.monotonic() + 600
    pid = int((RUNTIME / 'gmsv.pid').read_text())
    logpath = RUNTIME / 'logs/gmsv.log'
    offset, current = 0, ''
    while time.monotonic() < deadline:
        if not alive(pid):
            raise RuntimeError('This instance game server exited during initialization')
        # Read the historical log only once; repeated full DrvFS reads compete
        # with the map/NPC loader on a migrated instance with large logs.
        if logpath.stat().st_size < offset:
            offset, current = 0, ''
        with logpath.open('rb') as log:
            log.seek(offset)
            current += log.read().decode('utf-8', errors='replace')
            offset = log.tell()
        current = current.rsplit('This Program is compiled at', 1)[-1]
        if '在线玩家=' in current:
            if 'lua:' in current:
                raise RuntimeError('Native game loop started with Lua errors; inspect this run before testing')
            print(f'gmsv: pid {pid}, resources initialized and native game loop running')
            return
        time.sleep(0.5)
    raise RuntimeError('Native initialization did not reach the game loop within 600 seconds')


def stop_process(name: str) -> None:
    pidfile = RUNTIME / (name + ".pid")
    if not pidfile.exists():
        return
    pid = int(pidfile.read_text())
    if not alive(pid):
        return
    completion = {
        'gmsv': 'GMSV正常停止，原生数据保存和连接关闭已完成',
        'saac': 'SAAC正常停止，家族、据点和备忘资料已保存',
    }.get(name)
    logpath = RUNTIME / 'logs' / (name + '.log')
    offset = logpath.stat().st_size if completion and logpath.exists() else 0
    os.kill(pid, signal.SIGTERM)
    for _ in range(80):
        if not alive(pid):
            if completion:
                with logpath.open('rb') as log:
                    log.seek(offset)
                    current = log.read().decode('utf-8', errors='replace')
                if completion not in current:
                    raise RuntimeError(f'{name} exited without this stop\'s native save-completion marker; inspect {logpath}')
            print(f"{name}: stopped own process {pid}")
            return
        time.sleep(0.1)
    raise RuntimeError(f"{name} is still shutting down; not forcing termination")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["database", "native", "http", "restart-http", "stop-gmsv", "maintenance", "stop", "status", "ready"])
    action = parser.parse_args().action
    if action == "database":
        database()
    elif action == "native":
        start_process("saac", ["/usr/bin/stdbuf", "-oL", "-eL", str(PROJECT / "build/saac")], RUNTIME / "saac", SETTINGS["ac_port"])
        start_process("gmsv", ["/usr/bin/stdbuf", "-oL", "-eL", str(PROJECT / "build/gmsv")], RUNTIME / "gmsv", SETTINGS["game_port"])
    elif action in {"http", "restart-http"}:
        if action == "restart-http":
            stop_process("http")
        start_process("http", [str(PROJECT / ".venv/bin/python"), "-u", str(PROJECT / "account_service.py")], RUNTIME, SETTINGS["http_port"])
    elif action == "stop-gmsv":
        stop_process("gmsv")
    elif action == "maintenance":
        for name in ("http", "gmsv", "saac"):
            stop_process(name)
    elif action == "stop":
        for name in ("http", "gmsv", "saac", "database"):
            stop_process(name)
    elif action == "ready":
        ready()
    else:
        status()


if __name__ == "__main__":
    main()
