"""Prepare a NEW checkout from explicitly supplied, hash-locked original inputs.

Never copies a running instance, imports player rows or starts a service. Run
inside Ubuntu/WSL; the regular local entrypoints start the prepared instance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def checked_inputs(reference, client, lock):
    for group, base in (('reference', reference), ('client', client)):
        for entry in lock[group]:
            relative, expected = entry['path'], entry['sha256']
            path = base / relative
            if path.is_symlink() or not path.is_file():
                raise RuntimeError('Missing or redirected input: ' + group + '/' + relative)
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != expected:
                raise RuntimeError('Original input hash mismatch: ' + group + '/' + relative)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-source', required=True, type=Path)
    parser.add_argument('--original-client', required=True, type=Path)
    parser.add_argument('--check-inputs', action='store_true')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    args = parser.parse_args()
    if os.name == 'nt':
        parser.error('Run this command in Ubuntu/WSL, using /mnt/c/... paths.')
    reference, client = args.reference_source.resolve(), args.original_client.resolve()
    if ROOT == reference or ROOT == client or reference.is_relative_to(ROOT) or client == ROOT/'client':
        parser.error('Original inputs must be separate from the prepared checkout.')
    if not 1 <= args.jobs <= 64:
        parser.error('--jobs must be between 1 and 64')
    required=('prepare_sources','prepare_runtime','build_client_lua_reference','prepare_client',
              'prepare_client_tasks','apply_client_tasks','verify_prepared')
    missing=[name for name in required if not (ROOT/'tools'/(name+'.py')).is_file()]
    if missing:
        raise RuntimeError('Incomplete source distribution: '+', '.join(missing))
    lock = json.loads((ROOT/'release-input-lock.json').read_text(encoding='utf8'))
    if lock.get('format') != 2:
        raise RuntimeError('Unsupported original-input lock format')
    checked_inputs(reference, client, lock)
    print('Verified all pinned original source, archive and client inputs.', flush=True)
    if args.check_inputs:
        return
    # A failed preparation may be inspected and resumed explicitly by its owner.
    # This entrypoint never writes over an existing instance or client directory.
    occupied = [name for name in ('runtime', 'client', 'build', 'vendor', 'native/overrides')
                if (ROOT/name).exists()]
    if occupied:
        raise RuntimeError('Use a fresh checkout; generated paths already exist: ' + ', '.join(occupied))
    (ROOT/'runtime').mkdir()
    (ROOT/'evidence').mkdir(exist_ok=True)
    (ROOT/'build').mkdir()
    (ROOT/'runtime/inputs.json').write_text(json.dumps({
        'reference_source': str(reference), 'original_client': str(client)
    }, indent=2)+'\n', encoding='utf8')
    env = os.environ.copy()
    env['STONEAGE_REFERENCE_SOURCE'] = str(reference)
    env['STONEAGE_ORIGINAL_CLIENT'] = str(client)

    def run(*arguments):
        subprocess.run(arguments, cwd=ROOT, env=env, check=True)

    run(sys.executable, 'tools/prepare_sources.py')
    run(sys.executable, 'tools/prepare_runtime.py')
    run('cmake', '-S', 'native', '-B', 'build', '-DCMAKE_BUILD_TYPE=RelWithDebInfo')
    run('cmake', '--build', 'build', '-j', str(args.jobs))
    run(sys.executable, 'tools/build_client_lua_reference.py')
    run(sys.executable, 'tools/prepare_client.py')
    run(sys.executable, 'tools/prepare_client_tasks.py')
    run(sys.executable, 'tools/apply_client_tasks.py')
    run(sys.executable, 'tools/verify_prepared.py')
    print('Prepared native server and patched original client; no services or player accounts were started.')


if __name__ == '__main__':
    main()
