"""Record a character-only level item using the existing native item Lua state."""
import argparse
import csv
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKER = 'level140-pill-'


def prepare(verify=False):
    path = ROOT / 'native/resource-patches.json'
    existing = json.loads(path.read_text(encoding='utf8'))
    previous = [p for p in existing if p.get('id', '').startswith(MARKER)]
    patches = [p for p in existing if not p.get('id', '').startswith(MARKER)]
    name = 'gmsv/data/itemset6.csv'
    raw = (ROOT / 'runtime' / name).read_text(encoding='utf8')
    for patch in reversed(previous):
        if patch['file'] == name and patch['after'] in raw:
            raw = raw.replace(patch['after'], patch['before'], 1)
    all_rows = {}
    for source in (ROOT / 'runtime/gmsv/data').glob('itemset*.csv'):
        text = raw if source.name == 'itemset6.csv' else source.read_text(encoding='utf8')
        for row in csv.reader(text.splitlines()):
            if len(row)>17 and row[16].isdigit(): all_rows[int(row[16])] = row
    if 31020 in all_rows: raise RuntimeError('Level140 item ID already exists')
    row = all_rows[22057].copy()
    row[0] = row[1] = '*人物经验丹[Lv140]'
    row[2] = '人物升至140级并获得升级属性点；不改变转生和任务完成状态'
    row[3], row[10], row[16] = '140', 'ITEM_APP_LEVEL140', '31020'
    output = io.StringIO(); csv.writer(output,lineterminator='\n').writerow(row)
    candidates = [line for line in raw.splitlines(keepends=True)
                  if len(next(csv.reader([line])))>17 and next(csv.reader([line]))[16]=='22057']
    if len(candidates)!=1: raise RuntimeError('Nonunique native level item anchor')
    anchor = candidates[0]
    module = (ROOT / 'native/lua/app_level140_pill.lua').read_text(encoding='utf8')
    recipes = [
        {'id':MARKER+'item','file':name,'before':anchor,
         'after':anchor+('' if anchor.endswith('\n') else '\n')+output.getvalue(),
         'reason':'Separate native character level item; user-authorized quest QA must retain all quest flags.'},
        {'id':MARKER+'callback','file':'gmsv/data/ablua/item/explv.lua',
         'before':'function main()','after':module+'\nfunction main()',
         'reason':'Keep original experience item behavior; register a bounded, character-only native item callback.'},
        {'id':MARKER+'register','file':'gmsv/data/ablua/item/explv.lua',
         'before':'function main()','after':'function main()\n\titem.addLUAListFunction("ITEM_APP_LEVEL140", "AppLevel140Pill", "")',
         'reason':'Register the additional native item function through the existing Lua item loader.'},
    ]
    at = next((i for i,p in enumerate(existing) if p.get('id','').startswith(MARKER)),
              next((i for i,p in enumerate(patches) if p.get('id','').startswith('app-events-')),len(patches)))
    patches[at:at] = recipes
    if verify:
        if patches!=existing: raise RuntimeError('Level140 recipes differ from source module/native variant')
    else:
        path.write_text(json.dumps(patches,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    return {'level140_item':31020,'resource_recipes':len(recipes)}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true')
    print(json.dumps(prepare(parser.parse_args().verify)))
