"""Record native riding mappings and a zero-model client preview, with provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from project_paths import ORIGINAL_CLIENT

ROOT=Path(__file__).resolve().parents[1]
MARKER='ride-models-20260911/'
PETWND_SHA='c9bc5072c364fcf3785d75ea6c5a36142bf66d89d6b320a0afbaf3c737efcb2f'


def prepare(verify=False):
    evidence=ROOT/'evidence/ride-models-20260911/external-riding-catalog.json'
    public=json.loads(evidence.read_text(encoding='utf8'))
    rows=public['data']['petData'].values()
    for name,number in (('拉奇鲁哥',101184),('卡卡金宝',101187),('石龟',128405)):
        if not any(r['name']==name and int(r['graNo'])==number for r in rows):
            raise RuntimeError('Public riding model no longer matches: '+name)
    sources={}
    available=set()
    for path in sorted((ROOT/'client/path').glob('*/spradrn.bin')):
        raw=path.read_bytes();original=(ORIGINAL_CLIENT/path.relative_to(ROOT/'client')).read_bytes()
        if raw!=original:raise RuntimeError('Original sprite index mismatch: '+str(path))
        sources[str(path.relative_to(ROOT/'client'))]=hashlib.sha256(raw).hexdigest()
        available.update(n for n,_,count,_ in struct.iter_unpack('<IIHH',raw) if count)
    models=[101184+8*i for i in range(12)]+[101187+8*i for i in range(12)]+[128405,128406]
    if not set(models)<=available:raise RuntimeError('Verified ride artwork is missing')
    if hashlib.sha256((ORIGINAL_CLIENT/'lua/petwnd.cf').read_bytes()).hexdigest()!=PETWND_SHA:
        raise RuntimeError('Original pet window changed')
    module=(ROOT/'native/lua/app_ride_models.lua').read_text(encoding='utf8')
    recipes=[{'id':MARKER+'native','file':'gmsv/data/ablua/familyridefunction.lua',
              'before':'function main()','after':module+'\nfunction main()',
              'reason':'Match public target rider models in query and native world callbacks; retain certificates for four unavailable models.'},
             {'id':MARKER+'client','scope':'client','file':'lua/petwnd.cf','operation':'wrap-6021',
              'module':'native/client-ride-models.lua','source_sha256':PETWND_SHA,
              'reason':'Keep original pet UI; zero riding model displays availability text instead of the client fallback Uli.'}]
    path=ROOT/'native/resource-patches.json';existing=json.loads(path.read_text(encoding='utf8'))
    patches=[p for p in existing if not p.get('id','').startswith(MARKER)]
    at=next((i for i,p in enumerate(existing) if p.get('id','').startswith(MARKER)),
            next((i for i,p in enumerate(patches) if p.get('id','').startswith('app-events-')),len(patches)))
    patches[at:at]=recipes
    if verify:
        if patches!=existing:raise RuntimeError('Ride recipes differ from maintained modules')
    else:path.write_text(json.dumps(patches,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    return {'mapped_species':3,'verified_rider_sprites':len(models),'unmatched_species':4,
            'public_catalog_sha256':hashlib.sha256(evidence.read_bytes()).hexdigest(),
            'sprite_index_sha256':sources}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--verify',action='store_true')
    result=prepare(parser.parse_args().verify)
    print(json.dumps({k:v for k,v in result.items() if k!='sprite_index_sha256'}))
