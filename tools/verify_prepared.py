"""Read-only replay of the current original-source and client change manifests."""
import hashlib
import json
from pathlib import Path

from prepare_sources import normalize_source
from apply_resource_patches import build_plan
from prepare_client_tasks import ORIGINAL as TASK_ORIGINAL, LOCAL as TASK_LOCAL, build_candidate
from project_paths import host_path

PROJECT = Path(__file__).resolve().parents[1]


def read_json(name):
    return json.loads((PROJECT / name).read_text(encoding='utf-8'))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    manifest = read_json('evidence/source-manifest.json')
    patches = read_json('native/patches.json')
    files = normalized = 0
    expected_prepared = set()
    for component, info in manifest['components'].items():
        require(digest(host_path(info['project_file']).read_bytes()) == info['project_sha256'],
                f'Original compilation project changed: {component}')
        for relative, expected_hash in info['files'].items():
            name = f'{component}/{relative}'
            original = (host_path(info['source']) / relative).read_bytes()
            require(digest(original) == expected_hash, f'Original source changed: {name}')
            require((PROJECT / 'vendor' / name).read_bytes() == original, f'Vendor copy changed: {name}')
            prepared, encoding = normalize_source(original)
            normalized += encoding != 'utf-8'
            for patch in patches:
                if patch['file'] != name:
                    continue
                before, after = patch['before'].encode(), patch['after'].encode()
                require(prepared.count(before) == 1, f'Nonunique source patch: {name}')
                prepared = prepared.replace(before, after)
            target = PROJECT / 'native/overrides' / name
            expected_prepared.add(target.resolve())
            require(target.read_bytes() == prepared, f'Unrecorded prepared source edit: {name}')
            require(digest(prepared) == info['build_files'][relative]['sha256'], f'Prepared manifest mismatch: {name}')
            files += 1
    actual_prepared = {path.resolve() for path in (PROJECT / 'native/overrides').rglob('*') if path.is_file()}
    require(actual_prepared == expected_prepared, 'Prepared tree contains missing or additional files')

    client = read_json('evidence/client-manifest.json')
    original = host_path(client['source']).read_bytes()
    require(digest(original) == client['source_sha256'], 'Original client executable changed')
    output = bytearray(original)
    for patch in client['changes']:
        at = patch['offset']
        before = patch['before'].encode() + b'\0'
        after = patch['after'].encode() + b'\0'
        require(output[at:at+len(before)] == before and len(after) <= len(before), 'Client URL patch does not match')
        output[at:at+len(before)] = after.ljust(len(before), b'\0')
    require(host_path(client['output']).read_bytes() == output, 'Client copy contains unrecorded executable changes')
    require(digest(output) == client['output_sha256'], 'Client copy hash mismatch')

    expected_tasks, task_report = build_candidate(TASK_ORIGINAL.read_bytes())
    require(TASK_LOCAL.read_bytes() == expected_tasks, 'Local task resource differs from verified sparse-ID repair')

    resources = read_json('evidence/resource-changes.json')
    resource_plan = build_plan(PROJECT)
    for resource in resource_plan.resources:
        require(resource.current == resource.result, f'Resource patches not fully applied: {resource.name}')
    require(resources == resource_plan.records, 'Resource change manifest does not match verified forward patches')

    from prepare_client_titles import verify_or_apply
    title_resources = verify_or_apply(verify=True)
    reference_resources = 0
    if (PROJECT / 'evidence/reference-pet-collector-manifest.json').exists():
        from prepare_reference_pet_collector import prepare
        reference_resources = len(prepare(verify=True)['output_sha256'])
    external_pack = read_json('native/external-pet-species.json')
    from prepare_activities import prepare as verify_activities
    activities = verify_activities(verify=True)
    from prepare_level140_pill import prepare as verify_level140
    verify_level140(verify=True)
    from prepare_ride_models import prepare as verify_rides
    verify_rides(verify=True)
    from prepare_external_pets import build, MARKER
    current_recipes = read_json('native/resource-patches.json')
    expected_pack, expected_recipes = build([p for p in current_recipes if not p.get('id', '').startswith(MARKER)])
    require(external_pack == expected_pack, 'External pet pack differs from its public evidence and native profiles')
    require([p for p in current_recipes if p.get('id', '').startswith(MARKER)] == expected_recipes,
            'External pet resource recipes differ from generated native data')

    result = {'source_files': files, 'normalized_gb18030_files': normalized,
              'native_patches': len(patches), 'client_url_changes': len(client['changes']),
              'resource_files_with_patches': len(resources),
              'client_title_resources': title_resources['outputs'],
              'reference_pet_collector_resources': reference_resources,
              'activity_reward_variants': activities['reward_variants'],
              'external_pet_templates_added': external_pack['new_template_count'],
              'external_pet_skills_enabled': sum(s['native_id'] is not None for s in external_pack['skills']),
              'client_task_sha256': task_report['after']['sha256'],
              'original_client_sha256': client['source_sha256'], 'local_client_sha256': digest(output)}
    (PROJECT / 'evidence/prepared-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
