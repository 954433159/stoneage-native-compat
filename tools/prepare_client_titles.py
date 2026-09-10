"""Replay recorded local title compatibility, preserving original client assets.

Default builds a candidate only. --apply accepts original or previously recorded
bytes, preflights every asset and atomically installs them. --verify is read-only
for client assets. No executable patch, player data, or service is involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

if __package__:
    from .project_paths import PROJECT, ORIGINAL_CLIENT
    from .inspect_client_xy import decode, encode
    from .inspect_client_lua import read as read_chunk
else:
    from project_paths import PROJECT, ORIGINAL_CLIENT
    from inspect_client_xy import decode, encode
    from inspect_client_lua import read as read_chunk


def digest(value):
    return hashlib.sha256(value).hexdigest()


def compile_chunk(source, target):
    if not (PROJECT/'build/client_lua_reference').exists():
        if os.name == 'nt':
            root='/mnt/'+PROJECT.drive[0].lower()+'/'+PROJECT.as_posix()[3:]
            command=['wsl','-d','Ubuntu','-u','root','--cd',root,
                     '.venv/bin/python','tools/build_client_lua_reference.py']
        else:
            import sys
            command=[sys.executable,str(PROJECT/'tools/build_client_lua_reference.py')]
        subprocess.run(command,check=True)
    if os.name == 'nt':
        def wsl(path):
            return '/mnt/'+path.drive[0].lower()+'/'+path.as_posix()[3:]
        command = ['wsl', '-d', 'Ubuntu', '-u', 'root', '--cd', wsl(PROJECT),
                   'build/client_lua_reference', '--compile', wsl(source), wsl(target)]
    else:
        command = [str(PROJECT/'build/client_lua_reference'), '--compile', str(source), str(target)]
    subprocess.run(command, check=True)
    read_chunk(target)  # Validate the complete 6021 format after compilation.


def candidate():
    patches = json.loads((PROJECT/'native/resource-patches.json').read_text(encoding='utf8'))
    patches = [p for p in patches if p.get('scope') == 'client' and p.get('profile','local') == 'local']
    required={'lua/pcwnd.cf', 'lua/title.xy'}
    supported=required | {'lua/menu.xy','lua/win/1199.xy','lua/win/1027.xy','lua/win/activity/3.xy','lua/petwnd.cf'}
    actual={p['file'] for p in patches}
    if not required.issubset(actual) or not actual.issubset(supported) or len(patches)!=len(actual):
        raise RuntimeError('Unexpected title resource patch set')
    base = PROJECT/'build/client-title-candidate'
    (base/'lua').mkdir(parents=True, exist_ok=True)
    (base/'compat').mkdir(exist_ok=True)
    outputs, records = {}, {}
    for p in patches:
        name = p['file']
        original_path=ORIGINAL_CLIENT/name
        raw = original_path.read_bytes() if original_path.exists() else None
        if p['operation']=='create-xy' and name=='lua/win/1199.xy':
            if raw is not None or p.get('source_sha256') is not None:
                raise RuntimeError('Local-only resource conflicts with an original: '+name)
        elif raw is None or digest(raw) != p['source_sha256']:
            raise RuntimeError('Original client resource changed: '+name)
        module = (PROJECT/p['module']).read_bytes()
        names_raw=b'';names_lua=''
        if name in required:
            names_raw = (PROJECT/p['names']).read_bytes()
            names = json.loads(names_raw)
            if set(names) != {str(i) for i in range(1,42)}:
                raise RuntimeError('Only verified native text title IDs 1..41 are supported')
            names_lua = 'local NativeTitleNames={\n'+''.join(
                f'[{i}]={json.dumps(names[str(i)],ensure_ascii=False)},\n' for i in range(1,42))+'}\n'
        if p['operation']=='wrap-6021' and name in {'lua/pcwnd.cf','lua/petwnd.cf'}:
            read_chunk(ORIGINAL_CLIENT/name)
            stem=Path(name).stem
            variable='nativePcWnd' if stem=='pcwnd' else 'nativePetWnd'
            original_name=f'compat/{stem}-original.cf'
            outputs[original_name]=raw
            wrapper = f"local {variable}=assert(loadfile(sa.getLuaPath()..'/../{original_name}'))()\n"
            source=base/f'{stem}-wrapper.lua'
            source.write_bytes(wrapper.encode()+names_lua.encode()+module+f'\nreturn {variable}\n'.encode())
            compile_chunk(source, base/name)
            result=(base/name).read_bytes()
        elif p['operation']=='append-xy' and name in {'lua/title.xy','lua/menu.xy','lua/win/1027.xy','lua/win/activity/3.xy'}:
            original=decode(raw)
            original.decode('utf8',errors='strict')
            if encode(original)!=raw:
                raise RuntimeError('Original title XY transform does not round-trip')
            result=encode(original+b'\n'+names_lua.encode()+module)
            if encode(decode(result))!=result:
                raise RuntimeError('Prepared title XY transform does not round-trip')
        elif p['operation']=='create-xy' and name=='lua/win/1199.xy':
            module.decode('utf8',errors='strict')
            result=encode(module)
            if decode(result)!=module: raise RuntimeError('New XY transform does not round-trip')
        else:
            raise RuntimeError('Unsupported title patch operation')
        outputs[name]=result
        records[name]={'source_sha256':digest(raw) if raw is not None else None,'after_sha256':digest(result),
            'patch':p,'module_sha256':digest(module),'names_sha256':digest(names_raw)}
    # The original pcwnd still imports the exact original cons module.
    cons=(ORIGINAL_CLIENT/'lua/cons.cf').read_bytes()
    read_chunk(ORIGINAL_CLIENT/'lua/cons.cf')
    (base/'lua/cons.cf').write_bytes(cons)
    for name,data in outputs.items():
        (base/name).parent.mkdir(parents=True,exist_ok=True)
        (base/name).write_bytes(data)
    report={'resources':records,'outputs':{name:digest(data) for name,data in outputs.items()},
            'original_cons_sha256':digest(cons),
            'limits':['Native graphic titles 42..64 retain original mappings and are not semantically verified']}
    return base,outputs,report


def verify_or_apply(apply=False, verify=False):
    base,outputs,report=candidate()
    record=PROJECT/'evidence/client-title-resource-changes.json'
    prior=json.loads(record.read_text(encoding='utf8')) if record.exists() else None
    checked=[]
    for name,data in outputs.items():
        path=PROJECT/'client'/name
        actual=path.read_bytes() if path.exists() else None
        original=ORIGINAL_CLIENT/name
        allowed={digest(data)}
        if original.exists():
            allowed.add(digest(original.read_bytes()))
        if prior and name in prior['outputs']:
            allowed.add(prior['outputs'][name])
        if actual is not None and digest(actual) not in allowed:
            raise RuntimeError('Unrecorded client resource edit: '+name)
        if verify and actual!=data:
            raise RuntimeError('Client title compatibility not fully applied: '+name)
        checked.append((path,actual,data))
    if digest((PROJECT/'client/lua/cons.cf').read_bytes())!=report['original_cons_sha256']:
        raise RuntimeError('Local cons module differs from original')
    if verify and prior!=report:
        raise RuntimeError('Client title change manifest is not current')
    if apply:
        # Preflight all files before changing any; recheck immediately before writes.
        for path,actual,_ in checked:
            if (path.read_bytes() if path.exists() else None)!=actual:
                raise RuntimeError('Client resource changed during preparation')
        for path,actual,data in checked:
            if actual==data: continue
            path.parent.mkdir(parents=True,exist_ok=True)
            fd,temp=tempfile.mkstemp(prefix='.'+path.name,dir=path.parent)
            try:
                with os.fdopen(fd,'wb') as stream:
                    stream.write(data);stream.flush();os.fsync(stream.fileno())
                os.replace(temp,path)
            finally:
                if os.path.exists(temp): os.unlink(temp)
        fd,temp=tempfile.mkstemp(prefix='.'+record.name,dir=record.parent)
        try:
            with os.fdopen(fd,'w',encoding='utf8') as stream:
                json.dump(report,stream,ensure_ascii=False,indent=2)
                stream.write('\n');stream.flush();os.fsync(stream.fileno())
            os.replace(temp,record)
        finally:
            if os.path.exists(temp): os.unlink(temp)
    return {'candidate':str(base),'outputs':len(outputs),'applied':apply,'verified':verify}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--apply',action='store_true')
    mode.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    print(json.dumps(verify_or_apply(args.apply,args.verify)))
