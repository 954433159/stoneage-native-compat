"""Static target machine-code anchors + native-serializer packet replay, no client execution.
The replay is intentionally small; it is not described as executing target machine code.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture',type=Path,default=ROOT/'evidence/attributes-ordinary-item-fields.txt')
parser.add_argument('--output-dir',type=Path,default=ROOT/'evidence')
args=parser.parse_args()
sys.path.insert(0,str(ROOT/'analysis-deps'))
import capstone
import pefile
raw=(ROOT/'client/stoneage.exe').read_bytes()
sha=hashlib.sha256(raw).hexdigest()
assert sha=='98d03f47eb00066b34cf1913ce1923e4554aae2542cf941aeca1a4af7a26d8b9'
pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def instruction(address):
    ins=next(cs.disasm(pe.get_data(address-base,16),address))
    return ins.mnemonic,ins.op_str
anchors={
    0x86c80a:('imul','eax, dword ptr [ebp - 0x18], 0x16'),
    0x87460e:('cmp','dword ptr [ebp - 0xd7c], 0x36'),
    0x87461b:('imul','eax, dword ptr [ebp - 0xd7c], 0x15'),
    0x8c8aa0:('mov','byte ptr [edx + ecx], 0'),
    0x86cbdf:('mov','byte ptr [edx + 0xaf50479], al'),
    0x86cbb4:('add','ecx, 0xaf5023c'),
    0x86cbd0:('call','0x8c8970'),
    0x870a61:('mov','dword ptr [0xadd08d4], eax'),
    0x870903:('call','0x7f5800'),
    0x6d00f2:('cmp','dword ptr [ebp - 8], 0x36'),
}
for address,expected in anchors.items():assert instruction(address)==expected,(hex(address),instruction(address),expected)
swap=list(cs.disasm(pe.get_data(0x7f5800-base,0x2ff),0x7f5800))
assert swap[-1].mnemonic=='ret'
assert all('0xaf50479'not in ins.op_str and '0xaf5023c'not in ins.op_str for ins in swap)
for function,receiver in [(22,0x86c7c0),(24,0x8708d0),(46,0x871230),(63,0x870a40)]:
    slot=pe.get_data(0x8614dc-base+function-2,1)[0]
    address=struct.unpack('<I',pe.get_data(0x8613e4-base+4*slot,4))[0]
    dispatch=list(cs.disasm(pe.get_data(address-base,128),address))
    assert any(ins.mnemonic=='call'and ins.op_str==hex(receiver)for ins in dispatch)
fixture={}
for line in args.fixture.read_text(encoding='utf-8').splitlines():
    if '='in line: k,v=line.split('=',1);fixture[k]=v
types=[int(v)for v in fixture['TYPES'].split('|')]
full=fixture['S'];assert full.startswith('I')
tokens=full[1:].split('|');assert len(tokens)==54*21
cache=[dict(use=True,name='stale',type=99,alchemy='stale')for _ in range(54)]
def row(values):
    assert len(values)==21
    return dict(use=True,name=values[0],type=int(values[12]),alchemy=values[11],
        graphic=int(values[4]),level=int(values[7]),pile=int(values[10]),
        inlay_type=values[17],inlay_code=values[18],can_use=int(values[19]))
for slot in range(54):
    values=tokens[slot*21:(slot+1)*21]
    if not values or not values[0]:cache[slot]['use']=False
    else:cache[slot]=row(values)
assert [i for i,v in enumerate(cache)if v['use']]==[0,53]
for slot,item in [(0,0),(53,1)]:
    value=cache[slot]
    assert value['name']==f'fixture-{item}' and value['type']==types[item]
    assert value['graphic']==24000+item and value['level']==5+item and value['pile']==1
    assert value['inlay_type']=='2,4' and value['inlay_code']=='9,11' and value['can_use']==1
updates=fixture['I'].split('|');assert len(updates)==44
for offset in range(0,len(updates),22):
    slot=int(updates[offset]);values=updates[offset+1:offset+22];cache[slot]=row(values)
assert cache[0]['type']==types[1] and cache[53]['type']==types[0]
empty=fixture['EMPTY'].split('|');assert len(empty)==22 and empty[1]==''
cache[int(empty[0])]['use']=False;assert not cache[0]['use']
out=dict(target_exe_sha256=sha,anchors={hex(k):v for k,v in anchors.items()},
    native_slots=54,equipment_slots=9,target_slots=54,full_row_tokens=21,incremental_row_tokens=22,
    missing_slots_cleared=[],si_omissions=['type','alchemy'],
    machine_code_executed=False,gui_tested=False)
args.output_dir.mkdir(parents=True,exist_ok=True)
(args.output_dir/'attributes-ordinary-packet-proof.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
print('PASS: actual target anchors and native packet replay agree on 54/54 slots, 21/22 fields, SI omissions and SKUP')
