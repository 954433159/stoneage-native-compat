"""Prepare the optional read-only research module in client-reference only."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import tempfile
from project_paths import PROJECT,ORIGINAL_CLIENT
from inspect_client_xy import decode,encode


def digest(data): return hashlib.sha256(data).hexdigest()


def prepare(apply=False,verify=False):
    recipes=json.loads((PROJECT/'native/resource-patches.json').read_text(encoding='utf-8'))
    selected=[p for p in recipes if p.get('profile')=='reference']
    if len(selected)!=2 or {p['file'] for p in selected}!={'lua/win/1027.xy','lua/win/1000.xy'}:
        raise RuntimeError('Unexpected reference research recipe set')
    executable=PROJECT/'client-reference/stoneage.exe'
    if digest(executable.read_bytes())!='916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7':
        raise RuntimeError('Reference executable must remain the original')
    record=PROJECT/'evidence/reference-pet-collector-manifest.json'
    previous=json.loads(record.read_text(encoding='utf-8')) if record.exists() else None
    prior_resources=previous.get('resources',{}) if previous else {}
    if previous and 'recipe' in previous:
        prior_resources={previous['recipe']['file']:previous}
    resources={};writes=[]
    for patch in selected:
        if patch['scope']!='client' or patch['operation']!='append-xy':
            raise RuntimeError('Research recipe escaped the public pet windows')
        original=(ORIGINAL_CLIENT/patch['file']).read_bytes()
        if digest(original)!=patch['source_sha256']: raise RuntimeError('Original pet resource changed')
        source=decode(original)
        if encode(source)!=original: raise RuntimeError('Original XY round-trip failed')
        module=(PROJECT/patch['module']).read_bytes()
        output=encode(source+b'\n'+module)
        if decode(output)!=source+b'\n'+module: raise RuntimeError('Collector XY round-trip failed')
        target=PROJECT/'client-reference'/patch['file']
        actual=target.read_bytes()
        allowed={digest(original),digest(output)}
        prior=prior_resources.get(patch['file'])
        if prior: allowed.add(prior['output_sha256'])
        if digest(actual) not in allowed: raise RuntimeError('Unrecorded reference resource change: '+patch['file'])
        resources[patch['file']]={'recipe':patch,'source_sha256':digest(original),
            'module_sha256':digest(module),'output_sha256':digest(output),
            'target':target.relative_to(PROJECT).as_posix()}
        if verify and actual!=output: raise RuntimeError('Reference collector is stale: '+patch['file'])
        writes.append((target,actual,output))
    result={'resources':resources,
            'read_only_protocol':'Original growth G/D and ranking G/P/S; public pet statistics only; no game mutations',
            'request_spacing_seconds':2,'response_timeout_seconds':15}
    if verify and previous!=result: raise RuntimeError('Reference collector manifest is stale')
    if apply:
        staged=[]
        try:
            for target,actual,output in writes:
                if target.read_bytes()!=actual: raise RuntimeError('Reference resource changed during preparation')
                fd,tmp=tempfile.mkstemp(prefix='.pet-collector-',dir=target.parent)
                staged.append((tmp,target))
                with os.fdopen(fd,'wb') as stream: stream.write(output);stream.flush();os.fsync(stream.fileno())
            fd,tmp=tempfile.mkstemp(prefix='.pet-collector-manifest-',dir=record.parent)
            staged.append((tmp,record))
            with os.fdopen(fd,'w',encoding='utf-8') as stream:
                stream.write(json.dumps(result,ensure_ascii=False,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
            for target,actual,_ in writes:
                if target.read_bytes()!=actual: raise RuntimeError('Reference resource changed before replacement')
            for tmp,target in staged: os.replace(tmp,target)
        finally:
            for tmp,_ in staged:
                if os.path.exists(tmp): os.unlink(tmp)
    return {'applied':apply,'verified':verify,
            'output_sha256':{name:r['output_sha256'] for name,r in resources.items()}}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    mode=ap.add_mutually_exclusive_group();mode.add_argument('--apply',action='store_true');mode.add_argument('--verify',action='store_true')
    args=ap.parse_args();print(json.dumps(prepare(args.apply,args.verify)))
