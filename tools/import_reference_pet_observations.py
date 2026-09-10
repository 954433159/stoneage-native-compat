"""Import public growth replies recorded by the optional reference client.

Lengths count UTF-8 bytes. A live import accepts an unfinished last frame but
never skips malformed frames or accepts a detail without its matching catalog.
Existing manual observations and different returned ranges are retained.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'reference client received public pet window data'
EXE_HASH = '916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7'


def decode_frames(data, live=False):
    if len(data) > 16 * 1024 * 1024:
        raise ValueError('Collection exceeds the public catalog size limit')
    frames, offset = [], 0
    while offset < len(data):
        colon = data.find(b':', offset, offset + 9)
        if colon < 0:
            tail = data[offset:]
            if live and tail.isdigit() and len(tail) <= 7:
                break
            raise ValueError('Incomplete or invalid frame length')
        length = data[offset:colon]
        if not re.fullmatch(rb'[1-9][0-9]{0,6}', length) or int(length) > 262144:
            raise ValueError('Invalid frame length')
        end = colon + 1 + int(length)
        if end > len(data):
            if live:
                break
            raise ValueError('Incomplete final frame')
        frames.append(data[colon + 1:end].decode('utf-8', 'strict'))
        offset = end
    return frames, offset


def tokens(payload):
    if not payload.endswith('|'):
        raise ValueError('Reply does not have its protocol terminator')
    return payload[:-1].split('|')


def integer(value, minimum=0, maximum=10000000):
    if not re.fullmatch(r'-?[0-9]+', value):
        raise ValueError('Invalid integer in public pet reply')
    number = int(value)
    if not minimum <= number <= maximum:
        raise ValueError('Public pet number outside the supported range')
    return number


def parse_catalog(payload):
    fields = tokens(payload)
    if len(fields) < 2 or fields[0] != 'L':
        raise ValueError('Invalid catalog reply')
    count = integer(fields[1], 1, 2048)
    if len(fields) != 2 + count * 2:
        raise ValueError('Catalog field count mismatch')
    result = {}
    for i in range(count):
        name, number = fields[2 + i * 2:4 + i * 2]
        if not name or len(name) > 128:
            raise ValueError('Invalid public pet name')
        result[str(i + 1)] = {'name': name, 'index': integer(number, 1)}
    if len({row['index'] for row in result.values()}) != count:
        raise ValueError('Duplicate catalog species ID')
    return result


def parse_detail(payload):
    # Real D replies have 50 fields without a separate terminator. The final
    # empty skill description therefore ends in '|'. Also accept one explicit
    # terminator, as used by the compatible local reply builder.
    fields = payload.split('|')
    if len(fields) == 51 and fields[-1] == '':
        fields.pop()
    if len(fields) != 50 or fields[0] != 'D':
        raise ValueError('Growth detail must have the original 50 fields')
    pet = {'graNo': integer(fields[1], 1), 'name': fields[2],
           'property': {str(i + 1): integer(fields[3 + i], 0, 100) for i in range(4)},
           'tx': integer(fields[35])}
    for i, key in enumerate(('growthMin', 'growthMax', 'growthMin1', 'growthMax1')):
        value = fields[7 + i]
        if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', value) or float(value) > 1000:
            raise ValueError('Invalid growth range')
        pet[key] = value  # Retain server precision; do not reverse-invent grades.
    for i, key in enumerate(('hp', 'attack', 'defend', 'quick')):
        pet[key] = {str(j + 1): integer(fields[11 + j * 4 + i]) for j in range(6)}
    pet['skillNames'] = {str(i + 1): fields[36 + i] for i in range(7)}
    pet['skillDescs'] = {str(i + 1): fields[43 + i] for i in range(7)}
    return pet


def import_frames(data, catalog, previous=None, live=False):
    result = deepcopy(previous) if previous else {
        'source': SOURCE, 'executable_sha256': EXE_HASH, 'catalog': catalog, 'observations': {}}
    if result['source'] != SOURCE or result['executable_sha256'] != EXE_HASH or result['catalog'] != catalog:
        raise ValueError('Existing observations have a different source or catalog')
    frames, consumed = decode_frames(data, live)
    current, details, conflicts = None, 0, 0
    quarantined = result.setdefault('conflicts', {})
    for frame in frames:
        prefix = frame.split('|', 3)
        if len(prefix) != 4:
            raise ValueError('Missing frame metadata')
        timestamp, kind, number, payload = prefix
        stamp = datetime.fromtimestamp(integer(timestamp, 1, 4102444800), timezone.utc).isoformat()
        number = integer(number)
        if kind == 'catalog' and number == 0:
            received = parse_catalog(payload)
            if received != catalog:
                raise ValueError('Received catalog differs from the verified external catalog')
            current = {int(p['index']): p['name'] for p in received.values()}
        elif kind in ('detail', 'conflict'):
            pet = parse_detail(payload)
            if current is None or number not in current:
                raise ValueError('Detail species was not in the received catalog')
            mismatch = current[number] != pet['name']
            if (kind == 'detail' and mismatch) or (kind == 'conflict' and not mismatch):
                raise ValueError('Detail species does not match the received catalog')
            if kind == 'conflict':
                conflicts += 1
                record = {'captured_at': stamp, 'requested_name': current[number], 'returned_pet': pet,
                          'status': 'quarantined_not_species_evidence'}
                records = quarantined.setdefault(str(number), [])
                if not any(r['returned_pet'] == pet for r in records):
                    records.append(record)
                continue
            details += 1
            records = result['observations'].setdefault(str(number), [])
            if not any(r['pet'] == pet for r in records):
                records.append({'captured_at': stamp, 'pet': pet,
                                'received_payload_sha256': hashlib.sha256(payload.encode('utf-8')).hexdigest()})
        else:
            raise ValueError('Unsupported public frame kind')
    record = {'sha256': hashlib.sha256(data[:consumed]).hexdigest(), 'consumed_bytes': consumed,
              'catalog_frames': len(frames) - details - conflicts, 'detail_frames': details,
              'conflict_frames': conflicts}
    imports = result.setdefault('frame_imports', [])
    if record not in imports:
        imports.append(record)
    return result


def import_ranking_frames(data, previous=None, live=False):
    """Keep public individual statistics separate from species growth bounds."""
    result = deepcopy(previous) if previous else {
        'source': 'reference client received public pet ranking statistics',
        'executable_sha256': EXE_HASH, 'catalog': None, 'observations': {}}
    if result['source'] != 'reference client received public pet ranking statistics' or result['executable_sha256'] != EXE_HASH:
        raise ValueError('Existing ranking observations have a different source')
    frames, consumed = decode_frames(data, live)
    current, count, empty = None, 0, 0
    for frame in frames:
        prefix = frame.split('|', 3)
        if len(prefix) != 4:
            raise ValueError('Missing ranking metadata')
        timestamp, kind, number, payload = prefix
        stamp = datetime.fromtimestamp(integer(timestamp, 1, 4102444800), timezone.utc).isoformat()
        number = integer(number)
        if kind == 'catalog' and number == 0:
            current = parse_catalog(payload)
            if result['catalog'] not in (None, current):
                raise ValueError('Ranking catalog changed between observations')
            result['catalog'] = current
            current = {int(row['index']): row['name'] for row in current.values()}
            continue
        if kind not in ('rank', 'empty') or current is None or number not in current:
            raise ValueError('Ranking reply does not have a matching catalog request')
        if kind == 'empty':
            if payload != 'P|1|0|0|0|0|0|0|0':
                raise ValueError('Empty ranking evidence must have seven absent public slots')
            pet = {'catalog_name': current[number], 'available': False,
                   'reason': 'all_seven_slots_absent_in_actual_P_reply'}
            records = result['observations'].setdefault(str(number), [])
            if not any(r['pet'] == pet for r in records):
                records.append({'captured_at': stamp, 'pet': pet})
            empty += 1
            count += 1
            continue
        fields = payload.split('|')
        if fields[-1] == '':
            fields.pop()
        if len(fields) < 2 or fields[0] != 'S' or fields[1] not in ('0', '1'):
            raise ValueError('Invalid ranking statistics reply')
        available = fields[1] == '1'
        if len(fields) != (27 if available else 2):
            raise ValueError('Unexpected ranking fields; do not import unknown identifiers')
        pet = {'catalog_name': current[number], 'available': available}
        if available:
            values = [integer(v, -1000000, 10000000) for v in fields[2:]]
            pet.update(image=values[0], elements=values[1:5], blessing=values[5:9],
                       level_and_stats=values[9:14], initial_level_and_stats=values[14:19],
                       without_blessing=values[19:23], effects=values[23:25])
            levels = values[9] - values[14]
            if levels > 0:
                pet['observed_growth_without_blessing'] = [
                    round((values[19 + i] - values[15 + i]) / levels, 6) for i in range(4)]
            pet['use_as_species_parameters'] = False
        else:
            empty += 1
        records = result['observations'].setdefault(str(number), [])
        if not any(r['pet'] == pet for r in records):
            records.append({'captured_at': stamp, 'pet': pet,
                            'received_payload_sha256': hashlib.sha256(payload.encode()).hexdigest()})
        count += 1
    result['frame_summary'] = {'sha256': hashlib.sha256(data[:consumed]).hexdigest(),
                              'consumed_bytes': consumed, 'ranking_frames': count,
                              'unavailable_frames': empty}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence', type=Path, default=ROOT / 'evidence/external-pets-20260910')
    ap.add_argument('--live', action='store_true', help='Retain an unfinished last frame for the next import')
    ap.add_argument('--ranking', action='store_true', help='Import public individual ranking statistics separately')
    args = ap.parse_args()
    folder = args.evidence.resolve()
    if not folder.is_relative_to((ROOT / 'evidence').resolve()):
        ap.error('Evidence must belong to this project')
    output = folder / ('ranking-observed.json' if args.ranking else 'growth-observed.json')
    prior_bytes = output.read_bytes() if output.exists() else None
    previous = json.loads(prior_bytes) if prior_bytes else None
    data = (folder / ('received-ranking.frames' if args.ranking else 'received-growth.frames')).read_bytes()
    if args.ranking:
        result = import_ranking_frames(data, previous, args.live)
        summary = result['frame_summary']
    else:
        original = json.loads((folder / 'growth-catalog.json').read_text(encoding='utf-8'))
        if original['source'] != SOURCE or original['executable_sha256'] != EXE_HASH:
            raise ValueError('Unverified external catalog source')
        result = import_frames(data, original['data']['petlist'], previous, args.live)
        summary = result['frame_imports'][-1]
    if (output.read_bytes() if output.exists() else None) != prior_bytes:
        raise RuntimeError('Another observer changed the output; retry without overwriting it')
    temporary = output.with_suffix('.importing')
    temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(output)
    print(json.dumps({'observed_species': len(result['observations']),
                      'frame_summary': summary,
                      'unconsumed_bytes': len(data) - summary['consumed_bytes']}))


if __name__ == '__main__':
    main()
