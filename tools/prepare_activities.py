"""Record the target activity adapter and verified native reward item variants."""
import argparse
import csv
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKER = 'app-events-'


def prepare(verify=False):
    path = ROOT / 'native/resource-patches.json'
    all_patches = json.loads(path.read_text(encoding='utf8'))
    previous = [p for p in all_patches if p.get('id', '').startswith(MARKER)]
    patches = [p for p in all_patches if not p.get('id', '').startswith(MARKER)]
    raw = (ROOT / 'runtime/gmsv/data/itemset6.csv').read_text(encoding='utf8')
    for patch in reversed(previous):
        if patch['file'] == 'gmsv/data/itemset6.csv' and patch['after'] in raw:
            raw = raw.replace(patch['after'], patch['before'], 1)
    rows = {int(r[16]): r for r in csv.reader(raw.splitlines()) if len(r) > 17 and r[16].isdigit()}
    variants = [
        (31000, 26007, '20000石币', '可获得20000石币', '20000', 24052),
        (31001, 26034, '10活力', '可获得10活力', '10', 51521),
        (31002, 21004, '*三倍智慧果(4小时)', '4小时内获得经验值上升200% 不可与其他倍数果实同时使用', '增200分240', 24236),
        (31003, 21010, '*8倍智慧果(4小时)', '4小时内获得经验值上升700% 不可与其他倍数果实同时使用', '增700分240', 24236),
    ]
    generated=[]
    for new, base, name, memo, argument, graphic in variants:
        if new in rows: raise RuntimeError('Activity item ID already exists: '+str(new))
        row=rows[base].copy()
        row[0]=row[1]=name;row[2]=memo;row[3]=argument;row[16]=str(new);row[17]=str(graphic)
        buffer=io.StringIO();csv.writer(buffer,lineterminator='\n').writerow(row)
        generated.append(buffer.getvalue())
    anchor=raw.splitlines(keepends=True)[-1]
    if not anchor.strip() or raw.count(anchor)!=1: raise RuntimeError('Item tail anchor is not unique')
    patches.append({'id':MARKER+'items','file':'gmsv/data/itemset6.csv','before':anchor,
        'after':anchor+('' if anchor.endswith('\n') else '\n')+''.join(generated),'reason':'Observed external daily/level rewards use native ITEM_Gold, ITEM_VIGOR and ITEM_Addexp; original items remain unchanged.'})
    module=(ROOT/'native/lua/app_events.lua').read_text(encoding='utf8')
    changes=[('module','function main()',module+'\nfunction main()'),
             ('create','\tAppPetGrowthCreate()','\tAppPetGrowthCreate()\n\tAppEventsCreate()'),
             ('menu','function FreeSaMenu(meindex, index)','function FreeSaMenu(meindex, index)\n\tif index == 33 or index == 34 then AppEventsOpen(meindex); return end'),
             ('shortcut','function FreeNewSaMenu(fd, index, data)','function FreeNewSaMenu(fd, index, data)\n\tif index == 20 then AppEventsOpen(net.getCharaindex(fd)); return end')]
    for key,before,after in changes:
        patches.append({'id':MARKER+key,'file':'gmsv/data/ablua/samenu.lua','before':before,'after':after,
            'reason':'Route the target welfare button through native role-owned, persisted reward state and window 1100.'})
    if verify:
        if all_patches!=patches: raise RuntimeError('Activity recipes differ from their source module/native variants')
    else:
        path.write_text(json.dumps(patches,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    return {'reward_variants':len(variants),'resource_recipes':5}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--verify',action='store_true')
    print(json.dumps(prepare(parser.parse_args().verify)))
