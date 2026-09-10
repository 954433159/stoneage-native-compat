"""Build the original client 6021 VM as a local resource-checking CLI, not a game client."""
import hashlib
import json
from pathlib import Path
import subprocess
from project_paths import PROJECT, REFERENCE_SOURCE

source = REFERENCE_SOURCE / 'Stoneage_80/lua/lua'
inputs = sorted(source.glob('*.c')) + sorted(source.glob('*.h'))
assert len(inputs) > 40
manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
record = PROJECT / 'evidence/client-lua-reference-source.json'
if record.exists():
    previous = json.loads(record.read_text())
    if previous['files'] != manifest:
        raise RuntimeError('Original client Lua sources changed; review before rebuilding')
else:
    record.write_text(json.dumps({'source': str(source), 'files': manifest}, indent=2)+'\n')
output = PROJECT / 'build/client_lua_reference'
subprocess.run(['gcc', '-std=gnu99', '-O2', '-D_GNU_SOURCE', '-DLUA_USE_POSIX',
                '-Wno-overflow', '-I'+str(source),
                str(PROJECT/'tools/client_lua_reference.c'),
                *map(str, sorted(source.glob('*.c'))), '-lm', '-ldl', '-o', str(output)], check=True)
assert manifest == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
print(json.dumps({'files': len(inputs), 'output': str(output),
                  'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}))
