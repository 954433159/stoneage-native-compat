"""Replay recorded changes from the verified archive; preflight all resources before writes."""
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
import zipfile

if __package__:
    from .project_paths import host_path
else:
    from project_paths import host_path

PROJECT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Resource:
    name: str
    path: Path
    current: bytes
    result: bytes


@dataclass(frozen=True)
class PatchPlan:
    resources: tuple[Resource, ...]
    records: dict
    record_path: Path
    inputs: tuple[tuple[Path, bytes | None], ...]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replay(original, changes, name):
    result = original
    for patch in changes:
        if patch['file'] != name:
            raise RuntimeError(f'Recorded resource patch targets another file: {name}')
        before, after = patch['before'].encode('utf-8'), patch['after'].encode('utf-8')
        if not before or result.count(before) != 1:
            raise RuntimeError(f'Nonunique resource patch: {name}')
        result = result.replace(before, after, 1)
    return result


def resource_path(runtime, name):
    relative = PurePosixPath(name)
    if (relative.is_absolute() or '..' in relative.parts or relative.as_posix() != name
            or '\\' in name or ':' in name or '\0' in name):
        raise RuntimeError('Resource path escaped this instance')
    path = (runtime / name).resolve()
    if not path.is_relative_to(runtime):
        raise RuntimeError('Resource path escaped this instance')
    return path


def build_plan(project=None):
    """Read-only verification API, also usable by prepared-tree checks."""
    project = Path(project) if project is not None else PROJECT
    runtime = (project / 'runtime').resolve()
    manifest_path = project / 'evidence/data-manifest.json'
    patches_path = project / 'native/resource-patches.json'
    record_path = project / 'evidence/resource-changes.json'
    manifest_raw, patches_raw = manifest_path.read_bytes(), patches_path.read_bytes()
    previous_raw = record_path.read_bytes() if record_path.exists() else None
    manifest = json.loads(manifest_raw.decode('utf-8'))
    patches = json.loads(patches_raw.decode('utf-8'))
    previous = json.loads(previous_raw.decode('utf-8')) if previous_raw is not None else {}
    grouped = {}
    for patch in patches:
        if patch.get('scope') == 'client':
            continue  # Separately hash-verified by prepare_client_titles.py.
        if patch.get('scope', 'runtime') != 'runtime':
            raise RuntimeError('Unknown resource patch scope')
        grouped.setdefault(patch['file'], []).append(patch)
    # Retain verification of previously managed files when their last patch is removed.
    for name in previous:
        grouped.setdefault(name, [])
    paths = {name: resource_path(runtime, name) for name in grouped}
    if len(set(paths.values())) != len(paths):
        raise RuntimeError('Multiple resource names resolve to the same file')
    output = {}
    resources = []
    with host_path(manifest['archive']).open('rb') as archive:
        if hashlib.file_digest(archive, 'sha256').hexdigest() != manifest['archive_sha256']:
            raise RuntimeError('Original resource archive hash mismatch')
        with zipfile.ZipFile(archive) as bundle:
            members = {}
            for entry in bundle.infolist():
                members.setdefault(entry.filename, []).append(entry)
            for name, changes in grouped.items():
                entries = members.get('175sa/' + name, [])
                if name not in manifest['files'] or len(entries) != 1 or entries[0].is_dir():
                    raise RuntimeError(f'Missing or nonunique recorded archive resource: {name}')
                original = bundle.read(entries[0])
                if digest(original) != manifest['files'][name]:
                    raise RuntimeError(f'Original resource hash mismatch: {name}')
                conversion = manifest.get('text_conversions', {}).get(name)
                if conversion:
                    if (conversion['from'], conversion['to']) != ('gb18030', 'utf-8'):
                        raise RuntimeError(f'Unsupported recorded resource conversion: {name}')
                    original = original.decode('gb18030').encode('utf-8')
                    if digest(original) != conversion['sha256']:
                        raise RuntimeError(f'Converted resource hash mismatch: {name}')
                expected = digest(original)
                result = replay(original, changes, name)
                accepted = [original, result]
                prior = previous.get(name)
                if prior is not None:
                    old_result = replay(original, prior['patches'], name)
                    if prior['before_sha256'] != expected or digest(old_result) != prior['after_sha256']:
                        raise RuntimeError(f'Previous resource record mismatch: {name}')
                    accepted.append(old_result)
                actual = paths[name].read_bytes()
                if actual not in accepted:
                    raise RuntimeError(f'Unrecorded resource edit; refusing to overwrite: {name}')
                resources.append(Resource(name, paths[name], actual, result))
                output[name] = {'before_sha256': expected, 'after_sha256': digest(result), 'patches': changes}
    inputs = ((manifest_path, manifest_raw), (patches_path, patches_raw), (record_path, previous_raw))
    return PatchPlan(tuple(resources), output, record_path, inputs)


def unchanged(plan):
    for path, expected in plan.inputs:
        actual = path.read_bytes() if path.exists() else None
        if actual != expected:
            raise RuntimeError(f'Resource inputs changed after preflight: {path.name}')
    for resource in plan.resources:
        if resource.path.read_bytes() != resource.current:
            raise RuntimeError(f'Resource changed after preflight: {resource.name}')


def apply_plan(plan):
    """Stage all writes, then replace individual files atomically; record completion last.

    An OS failure between replacements may leave old/new files mixed. Both complete
    byte sequences are verified by build_plan on retry; partial/unrecorded bytes are not.
    """
    unchanged(plan)
    writes = [(r.path, r.result) for r in plan.resources if r.current != r.result]
    record_bytes = (json.dumps(plan.records, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    if plan.inputs[-1][1] != record_bytes:
        writes.append((plan.record_path, record_bytes))
    staged = []
    try:
        for path, result in writes:
            fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', suffix='.tmp', dir=path.parent)
            temporary = Path(temporary)
            staged.append((temporary, path))
            with os.fdopen(fd, 'wb') as stream:
                stream.write(result)
                stream.flush()
                os.fsync(stream.fileno())
            if path.exists():
                os.chmod(temporary, path.stat().st_mode & 0o777)
        unchanged(plan)
        for temporary, path in staged:
            os.replace(temporary, path)
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)


def main():
    plan = build_plan()
    apply_plan(plan)
    changed = sum(r.current != r.result for r in plan.resources)
    patches = sum(len(record['patches']) for record in plan.records.values())
    print(f'Verified {len(plan.resources)} resources and {patches} patches; updated {changed} resource files; manifest current.')


if __name__ == '__main__':
    main()
