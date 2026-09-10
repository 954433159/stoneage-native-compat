"""Run native SQL tests with credentials restricted to a new empty fixture DB.

Only this wrapper connects as local socket root to create/drop the named fixture.
The tested executable receives a temporary user with privileges on that DB alone,
never this instance's gameplay database credentials. Run from the WSL workspace.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import time

import pymysql
from runtime import PROJECT, SOCKET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('feature', choices=('market', 'storage'))
    args = parser.parse_args()
    token = secrets.token_hex(5)
    database = 'stoneage_' + args.feature + '_test_' + token
    user, password = 'stone_fixture_' + token, secrets.token_hex(24)
    executable = PROJECT / ('evidence/services-market-db-test' if args.feature == 'market'
                            else 'evidence/storage-db-test')
    if not executable.is_file():
        raise RuntimeError('Compile the real native fixture before running')
    if not SOCKET.exists():
        raise RuntimeError('This local instance database socket is unavailable')
    result = None
    with pymysql.connect(unix_socket=str(SOCKET), user='root', autocommit=True) as db:
        with db.cursor() as cur:
            created_db = created_user = False
            try:
                # Identifiers consist only of a fixed prefix and generated hex.
                cur.execute(f'CREATE DATABASE `{database}` CHARACTER SET utf8mb4 COLLATE utf8mb4_bin')
                created_db = True
                cur.execute('CREATE USER %s@localhost IDENTIFIED BY %s', (user, password))
                created_user = True
                cur.execute(f'GRANT ALL ON `{database}`.* TO %s@localhost', (user,))
                environment = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'),
                               'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1'}
                prefix = 'STONE_' + args.feature.upper() + '_TEST_'
                for key, value in dict(DATABASE=database, USER=user, PASSWORD=password,
                                       SOCKET=str(SOCKET)).items():
                    environment[prefix + key] = value
                begin = time.monotonic()
                result = subprocess.run([str(executable)], cwd=PROJECT, env=environment,
                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=55)
                output = result.stdout + result.stderr
                if password in output:
                    raise RuntimeError('Test output unexpectedly contained a credential; not saved')
                evidence = PROJECT / 'evidence' / (args.feature + '-real-db.txt')
                evidence.write_text(output, encoding='utf-8')
                report = dict(feature=args.feature, database=database, restricted_user=user,
                    exit_code=result.returncode, seconds=round(time.monotonic()-begin, 2),
                    log=str(evidence.relative_to(PROJECT)), fixture_cleanup=True)
            finally:
                if created_db:
                    cur.execute(f'DROP DATABASE `{database}`')
                if created_user:
                    cur.execute('DROP USER %s@localhost', (user,))
    if result is None:
        raise RuntimeError('Fixture failed before execution')
    (PROJECT / 'evidence' / (args.feature + '-real-db.json')).write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    print(result.stdout[-2500:])
    if result.returncode:
        print(result.stderr[-3500:])
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
