"""Build missing native species from the completed, quarantined public evidence.

Existing templates and saved pets are never rebalanced. Inferred parameters of
new species are labelled as a local fit, not recovered foreign server grades.
Runtime edits are generated exclusively through resource-patches.json.
"""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import zipfile

from apply_resource_patches import replay
from project_paths import host_path

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'evidence/external-pets-20260910'
MARKER = 'external-pets-20260910/'
FIELDS = ('hp', 'attack', 'defend', 'quick')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def raw_stats(v):
    h, a, b, q = v
    return [(4*h+a+b+q)/100, (h*.1+a+b*.1+q*.05)/100,
            (h*.1+a*.1+b+q*.05)/100, q/100]


def inverse_stats(stats):
    h, a, b, q = [v*100 for v in stats]
    vital = (1.1*(h-q)-(a+b-.1*q))/4.2
    total = h-q-4*vital
    difference = (a-b)/.9
    return [vital, (total+difference)/2, (total-difference)/2, q]


def rank(grades):
    return next(i for i, minimum in enumerate((100, 95, 90, 85, 80, 0))
                if sum(grades) >= minimum)


def fit_parameters(versions):
    """Fit expected 1->140 growth; sample extrema cannot identify exact grades.

Native creation draws each base grade +/-2 and ten independent extra points;
each level draws another ten points and the rank-specific shared rate. Use
midranges, expected extra=2.5, and half-unit truncation correction. Select the
rank-consistent integer solution and the birth multiplier with minimum error.
No individual leaderboard record enters this fit.
    """
    birth = [statistics.median((float(v['pet'][k]['1']) + float(v['pet'][k]['2']))/2
                               for v in versions) for k in FIELDS]
    full = [statistics.median((float(v['pet'][k]['3']) + float(v['pet'][k]['4']))/2
                              for v in versions) for k in FIELDS]
    if any(not math.isfinite(v) or v <= 0 for v in birth+full):
        raise ValueError('Missing or invalid public growth ranges')
    delta = [(last-first)/139 for first, last in zip(birth, full)]
    internal_gain = inverse_stats(delta)
    candidates = []
    for expected_rank in range(6):
        rate = 4.75 + .2*expected_rank
        grades = [round((v+.5)/rate-2.5) for v in internal_gain]
        if any(g < 2 or g > 253 for g in grades) or rank(grades) != expected_rank:
            continue
        predicted_gain = raw_stats([(g+2.5)*rate-.5 for g in grades])
        growth_error = sum(((x-y)/max(y, .1))**2 for x, y in zip(predicted_gain, delta))
        # Birth extrema are correlated differently for each displayed statistic;
        # an expected value is an approximation, explicitly retained in the report.
        unit_birth = raw_stats([g+2.5 for g in grades])
        ratios = [v/t for v,t in zip(unit_birth, birth)]
        continuous_initial = sum(ratios)/sum(r*r for r in ratios)
        if not 1 <= continuous_initial <= 10000:
            continue
        initial = min({math.floor(continuous_initial), math.ceil(continuous_initial)}, key=lambda n: sum(
            ((x-y)/max(y, 1))**2 for x, y in
            zip(raw_stats([(g+2.5)*n for g in grades]), birth)))
        predicted_birth = raw_stats([(g+2.5)*initial for g in grades])
        predicted_full = [a+139*b for a, b in zip(predicted_birth, predicted_gain)]
        candidates.append((growth_error, {
            'initial': initial, 'grades': grades, 'rank': expected_rank,
            'method': 'native_expected_growth_midrange_fit_not_exact_recovery',
            'observed_birth_midrange': birth, 'observed_140_midrange': full,
            'expected_birth': [round(v, 4) for v in predicted_birth],
            'expected_140': [round(v, 4) for v in predicted_full],
            'maximum_140_relative_error': round(max(abs(a-b)/b for a, b in zip(predicted_full, full)), 6),
        }))
    if not candidates:
        raise ValueError('Public ranges cannot be fitted to a valid native pet rank')
    return min(candidates, key=lambda row: row[0])[1]


def original_table(name, recipes):
    manifest = json.loads((ROOT/'evidence/data-manifest.json').read_text(encoding='utf-8'))
    archive = host_path(manifest['archive'])
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != manifest['archive_sha256']:
            raise RuntimeError('Original resource archive changed')
    with zipfile.ZipFile(archive) as bundle:
        raw = bundle.read('175sa/'+name)
    if sha(raw) != manifest['files'][name]:
        raise RuntimeError('Original resource changed: '+name)
    conversion = manifest.get('text_conversions', {}).get(name)
    if conversion:
        raw = raw.decode(conversion['from']).encode(conversion['to'])
        if sha(raw) != conversion['sha256']:
            raise RuntimeError('Original resource conversion changed')
    return replay(raw, [p for p in recipes if p.get('file') == name and p.get('scope', 'runtime') == 'runtime'], name)


def build(recipes):
    observed = json.loads((EVIDENCE/'growth-observed.json').read_text(encoding='utf-8'))
    catalog = json.loads((EVIDENCE/'growth-catalog.json').read_text(encoding='utf-8'))['data']['petlist']
    if catalog != observed['catalog'] or len(catalog) != 514 or set(observed['conflicts']) != {'1016'}:
        raise RuntimeError('Completed catalog/quarantine evidence differs')
    frames = (EVIDENCE/'received-growth.frames').read_bytes()
    if sha(frames) != '57ffcda73704f5d5022a3940d5f08cb6dcde0d3c91d689d655b9704aa09e09a7':
        raise RuntimeError('Completed growth evidence changed')
    original = original_table('gmsv/data/enemybase1.txt', recipes)
    rows = list(csv.reader(io.StringIO(original.decode('utf-8'))))
    by_id, by_name = {}, {}
    for row in rows:
        if len(row) < 56 or not row[6].isdigit():
            continue
        number = int(row[6])
        if number in by_id:
            continue
        by_id[number] = row
        if row[37] == '1':
            by_name.setdefault(row[0], []).append(row)
    used = set(by_id)
    fallback = 6000
    additions, entries = [], []
    for ordinal in sorted(catalog, key=int):
        entry = catalog[ordinal]
        external_id, name = int(entry['index']), entry['name']
        if str(external_id) in observed['conflicts']:
            entries.append({'external_id': external_id, 'name': name, 'status': 'quarantined', 'native_id': None})
            continue
        versions = observed['observations'].get(str(external_id))
        if not versions or any(v['pet']['name'] != name for v in versions):
            raise RuntimeError('Missing/mismatched detail for '+name)
        detail = versions[-1]['pet']
        image = int(detail['graNo'])
        elements = [int(detail['property'][str(i)]) for i in range(1, 5)]
        candidates = by_name.get(name, [])
        alias = name == '雷帝洛斯' and image == 101501 and elements == [0, 0, 0, 0]
        if alias:
            candidates = [by_id[335]]
        same_id = [r for r in candidates if int(r[6]) == external_id]
        visual = [r for r in candidates if int(r[36]) == image and [int(x or 0) for x in r[15:19]] == elements]
        selected = (same_id or visual or candidates)
        record = {'external_id': external_id, 'name': name, 'image': image, 'elements': elements,
                  'external_skills': [{'name': n, 'description': detail['skillDescs'][k]}
                                      for k, n in detail['skillNames'].items() if n],
                  'external_acquisition': None}
        if selected:
            row = selected[0]
            record.update(native_id=int(row[6]), status='verified_spelling_alias' if alias else 'existing_native_preserved',
                          native_image=int(row[36]), native_elements=[int(x or 0) for x in row[15:19]])
        else:
            number = external_id
            if number in used:
                while fallback in used:
                    fallback += 1
                number = fallback
            used.add(number)
            fitted = fit_parameters(versions)
            if fitted['maximum_140_relative_error'] > .10:
                raise RuntimeError('Fit needs review for '+name)
            row = by_id[777].copy()
            row[0], row[6], row[7] = name, str(number), str(fitted['initial'])
            row[9:13] = [str(v) for v in fitted['grades']]
            row[15:19] = [str(v) for v in elements]
            row[25:32] = ['1', '2', '', '', '', '', '']
            row[36], row[54], row[55] = str(image), '140', '-1'
            additions.append(row)
            record.update(native_id=number, status='new_native_fitted_template', parameters=fitted)
        entries.append(record)
    if len(entries) != 514:
        raise RuntimeError('Incomplete generated catalog')
    out = io.StringIO(newline='')
    csv.writer(out, lineterminator='\n').writerows(additions)
    # A unique existing tail is the append anchor; the original is never edited.
    tail = original.decode('utf-8').splitlines()[-1]
    patch = {'id': MARKER+'templates', 'file': 'gmsv/data/enemybase1.txt',
             'before': tail, 'after': tail+'\n'+out.getvalue().rstrip('\n'),
             'reason': 'Add missing real native pet templates from matched public name/image/elements. New growth grades are documented local midpoint fits. Preserve every existing template and all saved pets; quarantine external 1016 mismatch.'}
    pack = {'growth_frames_sha256': sha(frames), 'level_baseline': 140,
            'new_template_count': len(additions), 'entries': entries,
            'limitations': ['Existing native template statistics are preserved; they are not claimed to match the external server.',
                           'New template grades are fitted, not recovered exact parameters.',
                           'Public growth and rankings do not establish acquisition routes.',
                           'External learnable skill names are evidence, not proof that a native implementation exists.']}
    profiles = json.loads((ROOT/'native/external-pet-skills.json').read_text(encoding='utf-8'))
    descriptions = {}
    for variants in observed['observations'].values():
        for version in variants:
            pet = version['pet']
            for key, name in pet['skillNames'].items():
                if name:
                    descriptions.setdefault(name, set()).add(pet['skillDescs'][key])
    if any(len(values) != 1 for values in descriptions.values()):
        raise RuntimeError('Skill description variants need review')
    skill_original = original_table('gmsv/data/petskill2.txt', recipes)
    native_skills = {}
    for row in csv.reader(io.StringIO(skill_original.decode('utf-8'))):
        if len(row) >= 12 and row[6].isdigit() and not row[0].startswith('#'):
            native_skills.setdefault(int(row[6]), row)
    supported = dict(profiles['existing'])
    for name, number in supported.items():
        if name not in descriptions or number not in native_skills:
            raise RuntimeError('Invalid existing skill mapping')
    extra_skills = []
    for profile in profiles['profiles']:
        number, name = profile['id'], profile['name']
        if number in native_skills or name in supported or name not in descriptions:
            raise RuntimeError('Invalid new skill identity')
        row = native_skills[profile['base']].copy()
        row[0], row[1], row[3], row[6] = name, next(iter(descriptions[name])), profile['option'], str(number)
        row[5], row[10], row[11] = '', '1000', 'PETSKILL_NONE'
        if 'target' in profile:
            row[8] = str(profile['target'])
        extra_skills.append(row)
        supported[name] = number
        native_skills[number] = row
    skill_output = io.StringIO(newline='')
    csv.writer(skill_output, lineterminator='\n').writerows(extra_skills)
    skill_tail = skill_original.decode('utf-8').splitlines()[-1]
    skill_patch = {'id': MARKER+'skills', 'file': 'gmsv/data/petskill2.txt',
                   'before': skill_tail, 'after': skill_tail+'\n'+skill_output.getvalue().rstrip('\n'),
                   'reason': 'Add explicit native skill profiles for received public descriptions. Preserve legacy IDs; only matched implementations enter the teaching allowlist.'}
    skill_audit = []
    for name in sorted(descriptions):
        number = supported.get(name)
        row = native_skills.get(number)
        skill_audit.append({'external_name': name, 'external_description': next(iter(descriptions[name])),
                            'native_id': number, 'native_name': row[0] if row else None,
                            'native_function': row[2] if row else None, 'native_option': row[3] if row else None,
                            'status': 'native_teaching_enabled' if number else 'not_implemented_not_teachable',
                            'external_acquisition': None})
    pack['skills'] = skill_audit
    lua_rows = []
    for entry in entries:
        if entry['native_id'] is None:
            continue
        names = entry['external_skills']
        entry['teachable_skills'] = [{'name': s['name'], 'id': supported[s['name']]}
                                     for s in names if s['name'] in supported]
        # The native basic commands stay obtainable even when an external list
        # only enumerates species-specific skills.
        allowed = list(dict.fromkeys([1, 2]+[s['id'] for s in entry['teachable_skills']]))
        pending = [s['name'] for s in names if s['name'] not in supported]
        lua_rows.append('{id=%d,name=%s,skills={%s},pending=%s}' % (
            entry['native_id'], json.dumps(entry['name'], ensure_ascii=False),
            ','.join(map(str, allowed)), json.dumps('、'.join(pending), ensure_ascii=False)))
    lua_catalog = 'AppExternalPetCatalog={\n'+',\n'.join(lua_rows)+'\n}\n'
    module = (ROOT/'native/external-pet-service.lua').read_text(encoding='utf-8')
    service_patch = {'id': MARKER+'service', 'file': 'gmsv/data/ablua/samenu.lua',
                     'before': 'function main()', 'after': lua_catalog+'\n'+module+'\nfunction main()',
                     'reason': 'Wire the matched catalog to real native pet creation and species-limited skill teaching, with current-role validation, confirmation, full-slot/balance checks and real stone deductions. Prices are local configuration, not foreign acquisition evidence.'}
    catalog_patch = {'id': MARKER+'catalog', 'file': 'gmsv/data/ablua/samenu.lua',
                     'before': '    -- Native template arrays are zero-based. Add real pet species omitted by the old menu.',
                     'after': "    -- Include all externally matched IDs, including distinct same-name elemental variants.\n    for _,entry in ipairs(AppExternalPetCatalog or {}) do add(entry.id,false) end\n    -- Native template arrays are zero-based. Add real pet species omitted by the old menu.",
                     'reason': 'Keep existing catalog choices and make every matched additional real template selectable, including four elemental demon Rubin variants.'}
    callback_patch = {'id': MARKER+'callback', 'file': 'gmsv/data/ablua/samenu.lua',
                      'before': "    if seq==0 and str=='' then AppPetGrowthOpen(talker);return end",
                      'after': "    if seq>=10 or (seq==1 and type(str)=='string' and str:match('^R|')) then\n        AppExternalPetWindow(me,talker,seq,button,str);return\n    end\n    if seq==0 and str=='' then AppPetGrowthOpen(talker);return end",
                      'reason': 'Route explicit local pet-service requests through the original native window object and its existing role/battle guards; ordinary G growth requests remain read-only updates.'}
    learnable_patch = {'id': MARKER+'learnable', 'file': 'gmsv/data/ablua/samenu.lua',
                       'before': "    local names,descs={},{}\n    for i=1,7 do\n        local id=enemytemp.getInt(p.array,'技能'..i)",
                       'after': "    local names,descs={},{}\n    local learnable=AppExternalPetLearnable(id)\n    for i=1,7 do\n        local id\n        if learnable then id=learnable[i] or 0 else id=enemytemp.getInt(p.array,'技能'..i) end",
                       'reason': 'Display skills that the real species-restricted native teaching route actually enables; preserve starting-skill display for other original species.'}
    label_patch = {'id': MARKER+'skill-label', 'file': 'gmsv/data/ablua/samenu.lua',
                   'before': "    parts[#parts+1]=p.level\n    growthCache[id]=table.concat(parts,'|')..'|'",
                   'after': "    parts[#parts+1]=p.level\n    parts[#parts+1]=learnable and 1 or 0\n    growthCache[id]=table.concat(parts,'|')..'|'",
                   'reason': 'Label actual learnability separately from original template starting skills in the local client; preserve native growth calculations and update-only selection.'}
    transport_open = {'id': MARKER+'transport-open', 'file': 'gmsv/data/ablua/samenu.lua',
                      'before': "    lssproto.windows(talker,1027,0,0,char.getWorkInt(growthNpc,'对象'),growthList)",
                      'after': "    lssproto.windows(talker,1027,0,0,char.getWorkInt(growthNpc,'对象'),AppPetGrowthFrames(growthList)[1])",
                      'reason': 'Keep every real catalog entry while bounding each WN below the original client 10240-byte diagnostic buffer, whose overflow corrupts the adjacent chat cursor.'}
    transport_page = {'id': MARKER+'transport-page', 'file': 'gmsv/data/ablua/samenu.lua',
                      'before': "    local id=seq==1 and type(str)=='string' and tonumber(str:match('^G|(%d+)|$'))",
                      'after': "    local page=seq==1 and type(str)=='string' and tonumber(str:match('^C|(%d+)|$'))\n    if page then\n        local frames=AppPetGrowthFrames(growthList)\n        if page>1 and frames[page] then\n            lssproto.windowsupdate(talker,1027,0,1,char.getWorkInt(me,'对象'),frames[page])\n        end\n        return\n    end\n    local id=seq==1 and type(str)=='string' and tonumber(str:match('^G|(%d+)|$'))",
                      'reason': 'Send subsequent catalog parts only after a client request from the created window, using the existing native object and role guards; no repeated window creation or truncated catalog.'}
    return pack, [patch, skill_patch, service_patch, catalog_patch, callback_patch, learnable_patch, label_patch, transport_open, transport_page]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    recipe_path = ROOT/'native/resource-patches.json'
    recipes = json.loads(recipe_path.read_text(encoding='utf-8'))
    baseline = [p for p in recipes if not p.get('id', '').startswith(MARKER)]
    pack, generated = build(baseline)
    packed = (json.dumps(pack, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    destination = ROOT/'native/external-pet-species.json'
    if args.verify:
        if not destination.exists() or destination.read_bytes() != packed or [p for p in recipes if p.get('id', '').startswith(MARKER)] != generated:
            raise RuntimeError('Generated external species pack is stale')
    elif args.apply:
        destination.write_bytes(packed)
        recipe_path.write_text(json.dumps(baseline+generated, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'external_entries': len(pack['entries']), 'new_templates': pack['new_template_count'],
                      'status': 'verified' if args.verify else 'recipes_written' if args.apply else 'preview'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
