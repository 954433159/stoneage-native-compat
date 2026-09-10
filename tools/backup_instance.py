"""Back up this instance after normal HTTP/GMSV/SAAC shutdown (run in WSL).

Keep its MariaDB running. Complete SAAC assets are saved under runtime/saac;
the duplicate char directory preserves existing migration-tool compatibility.
Failed attempts remain in a .partial directory without a success manifest.
"""
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess

from runtime import PROJECT, RUNTIME, SETTINGS, SOCKET, alive, listening

CONFIGURATION = ('runtime/settings.json', 'runtime/gmsv/setup.cf',
                 'runtime/saac/acserv.cf', 'client/saCN.ini')


def process_command(pid):
    return [os.fsdecode(arg) for arg in Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0') if arg]


def require_backup_state():
    for name, key in (('http', 'http_port'), ('gmsv', 'game_port'), ('saac', 'ac_port')):
        pidfile = RUNTIME / (name + '.pid')
        if (pidfile.exists() and alive(int(pidfile.read_text()))) or listening(SETTINGS[key]):
            raise RuntimeError('Normally stop this instance HTTP/GMSV/SAAC before backup')
    pid = int((RUNTIME / 'database.pid').read_text())
    if pid <= 0 or not alive(pid) or not SOCKET.exists():
        raise RuntimeError('The verified local database must be running')
    command = process_command(pid)
    if (not command or Path(command[0]).name not in ('mariadbd', 'mysqld')
            or f'--datadir={RUNTIME / "database"}' not in command
            or f'--socket={SOCKET}' not in command):
        raise RuntimeError('Database PID does not belong to this instance daemon and socket')
    return pid


def digest(path):
    if path.is_symlink() or path.resolve() != path or not stat.S_ISREG(path.stat().st_mode):
        raise RuntimeError(f'Backup input is redirected or not a regular file: {path}')
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def tree_snapshot(root):
    if not root.is_dir() or root.is_symlink() or root.resolve() != root:
        raise RuntimeError(f'Backup source directory is missing or redirected: {root}')
    files, directories = {}, []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink() or path.resolve() != path:
            raise RuntimeError(f'Backup input is redirected: {path}')
        if path.is_dir():
            directories.append(relative)
        else:
            files[relative] = digest(path)
    return files, directories


def configuration_snapshot():
    return {relative: digest(PROJECT / relative) if (PROJECT / relative).exists() else None
            for relative in CONFIGURATION}


def main():
    pid = require_backup_state()
    source = RUNTIME / 'saac'
    before = tree_snapshot(source)
    configs_before = configuration_snapshot()
    characters = sum(name.startswith('char/') and name.endswith('.char') for name in before[0])
    if not characters:
        raise RuntimeError('No saved characters found; refusing an empty-instance backup')
    completed = PROJECT / 'backups' / datetime.now().strftime('%Y%m%d-%H%M%S-native-basics')
    if completed.exists():
        raise FileExistsError(completed)
    destination = completed.with_name(completed.name + '.partial')
    destination.mkdir(parents=True, exist_ok=False)
    saved_saac = destination / 'runtime/saac'
    shutil.copytree(source, saved_saac)
    shutil.copytree(saved_saac / 'char', destination / 'char')
    for relative, expected in configs_before.items():
        if expected is not None:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(PROJECT / relative, target)
    if require_backup_state() != pid:
        raise RuntimeError('Database process changed during backup')
    with (destination / 'database.sql').open('wb') as output:
        subprocess.run(['mariadb-dump', '--no-defaults', '--protocol=socket',
                        f'--socket={SOCKET}', '--user=root', '--lock-all-tables',
                        '--routines', '--events', '--triggers', '--databases', SETTINGS['database']],
                       check=True, stdout=output, timeout=60)
    if not (destination / 'database.sql').stat().st_size:
        raise RuntimeError('Database dump is empty; backup is incomplete')
    if (tree_snapshot(saved_saac) != before
            or tree_snapshot(destination / 'char') != tree_snapshot(saved_saac / 'char')
            or any(digest(destination / name) != expected for name, expected in configs_before.items()
                   if expected is not None)):
        raise RuntimeError('Backup copy differs from the verified source snapshot')
    hashes, directories = tree_snapshot(destination)
    if (require_backup_state() != pid or tree_snapshot(source) != before
            or configuration_snapshot() != configs_before):
        raise RuntimeError('SAAC assets, configuration or database process changed during backup')
    manifest = {'format': 2, 'characters': characters, 'files': hashes, 'directories': directories,
                'saac_files': len(before[0]), 'saac_path': 'runtime/saac',
                'legacy_char_path': 'char',
                'note': 'Complete SAAC assets and configuration were stable throughout a locked logical database dump. '
                        'Native characters reflect their most recent normal save; HTTP/GMSV/SAAC were stopped.'}
    if completed.exists():
        raise FileExistsError(completed)
    manifest_path = destination / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    try:
        destination.rename(completed)
    except BaseException:
        manifest_path.unlink(missing_ok=True)
        raise
    print(json.dumps({'backup': str(completed), 'characters': characters,
                      'saac_files': len(before[0]), 'files': len(hashes)}))


if __name__ == '__main__':
    main()
