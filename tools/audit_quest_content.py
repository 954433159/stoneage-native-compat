"""Read the target task catalog and prepared native NPC/map resources, without services."""
import hashlib
import json
import re
import struct
from pathlib import Path
from prepare_client_tasks import parse

ROOT = Path(__file__).resolve().parents[1]


def audit():
    data = ROOT / 'runtime/gmsv/data'
    maps, npcs = {}, {}
    for path in (data / 'map').rglob('*'):
        if not path.is_file():
            continue
        with path.open('rb') as source:
            header = source.read(60)
        if header[:6] not in (b'LS2MAP', b'LS&MAP') or len(header) < 60:
            continue
        floor = struct.unpack('>H', header[6:8])[0]
        # Prepared UTF-8 MAP_MAGIC LS&MAP uses CHARNAMELEN 48 (net.h).
        offset = 56 if header[:6] == b'LS&MAP' else 40
        width, height = struct.unpack('>HH', header[offset:offset+4])
        if not width or not height:
            raise RuntimeError('Invalid prepared map dimensions: '+str(path))
        maps.setdefault(floor, []).append({'file':path.relative_to(ROOT).as_posix(),
            'width':width, 'height':height})
    for path in (data / 'npc').rglob('*.create'):
        text = path.read_text(encoding='utf8')
        text = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
        for block in re.findall(r'\{([^{}]+)\}', text):
            fields = dict(re.findall(r'^\s*([a-zA-Z_]+)\s*=\s*([^\r\n]*)', block, re.M))
            if not fields.get('floorid', '').isdigit():
                continue
            position_key = 'borncorner' if 'borncorner' in fields else 'borncenter'
            corner = fields.get(position_key, '').split(',')
            if len(corner) != 4 or not all(c.strip().isdigit() for c in corner):
                continue
            script = re.search(r'(?:^|\|)file:([^|\s]+)', fields.get('enemy', ''))
            script = data / 'npc' / script[1] if script else None
            npcs.setdefault(int(fields['floorid']), []).append({'name':fields.get('name',''),
                'x':int(corner[0]), 'y':int(corner[1]), 'position_kind':position_key,
                'file':path.relative_to(ROOT).as_posix(),
                'handler':fields.get('enemy', ''),
                'script_exists':script.exists() if script else None})
    path = ROOT / 'client/data/task.bin'
    raw = path.read_bytes()
    tasks = []
    for task in parse(raw).tasks:
        steps = []
        for step in task.subtasks:
            nearby = sorted(npcs.get(step.map_no, []),
                key=lambda n:abs(n['x']-step.x)+abs(n['y']-step.y))[:3]
            steps.append({'id':step.sub_id,'name':step.name.decode('utf8'),
                'floor':step.map_no,'x':step.x,'y':step.y,
                'maps':maps.get(step.map_no,[]), 'nearby_native_npcs':nearby})
        tasks.append({'id':task.task_id,'name':task.name.decode('utf8'),
            'requirement':task.requirement.decode('utf8'),'reward':task.reward.decode('utf8'),'steps':steps})
    return {'evidence_kind':'read-only resource coverage; not completed quest acceptance',
        'task_sha256':hashlib.sha256(raw).hexdigest(),'task_count':len(tasks),
        'native_map_floors':len(maps),'native_npcs':sum(map(len,npcs.values())), 'tasks':tasks}


if __name__ == '__main__':
    result = audit()
    output = ROOT / 'evidence/events-quests-20260910/quest-content-audit.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='tasks'},ensure_ascii=False))
    for task in result['tasks']:
        missing = [s['id'] for s in task['steps'] if s['floor'] and not s['maps']]
        print(task['id'],task['name'],len(task['steps']),'missing maps',missing)
