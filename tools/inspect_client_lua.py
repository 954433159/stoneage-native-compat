"""Read-only disassembly of the reference-defined 6021 Lua chunk format.

No chunk execution or gameplay changes. Validates every string terminator,
opcode and byte boundary. See original client lua/lua/lundump.c/lopcodes.h.
"""
import argparse
import json
from pathlib import Path
import struct

OPS = ('LOADBOOL LE GETUPVAL TEST NOT SETUPVAL SUB MUL TESTSET EQ CALL TAILCALL '
       'GETTABLE UNM RETURN SETTABLE LOADNIL POW LOADK FORLOOP FORPREP MOVE '
       'GETGLOBAL ADD MOD TFORLOOP SETLIST LEN CLOSE CLOSURE JMP DIV CONCAT LT '
       'SELF NEWTABLE SETGLOBAL VARARG').split()
KEY = bytes([58,69,51,79,198,226,42,241])


class Reader:
    def __init__(self, raw):
        self.raw, self.pos = raw, 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.raw):
            raise ValueError(f'Chunk boundary at {self.pos}, requested {size}')
        out = self.raw[self.pos:self.pos+size]
        self.pos += size
        return out

    def uint(self):
        return struct.unpack('<I', self.take(4))[0]

    def string(self):
        size = self.uint()
        if size == 0:
            return None
        value = bytes(x ^ KEY[i % len(KEY)] for i,x in enumerate(self.take(size)))
        if value[-1] != 0:
            raise ValueError('Reference string key does not validate the chunk')
        return value[:-1].decode('utf-8')

    def function(self, depth=0):
        if depth > 100:
            raise ValueError('Excessive nesting')
        source, first, last = self.string(), self.uint(), self.uint()
        meta = list(self.take(4))
        count = self.uint()
        code = [self.uint() for _ in range(count)]
        constants = []
        for _ in range(self.uint()):
            tag = self.take(1)[0]
            if tag == 0: value = None
            elif tag == 1: value = bool(self.take(1)[0])
            elif tag == 3: value = struct.unpack('<d',self.take(8))[0]
            elif tag == 4: value = self.string()
            else: raise ValueError(f'Unknown constant type {tag}')
            constants.append(value)
        children = [self.function(depth+1) for _ in range(self.uint())]
        lines = [self.uint() for _ in range(self.uint())]
        locals_ = [(self.string(),self.uint(),self.uint()) for _ in range(self.uint())]
        upvalues = [self.string() for _ in range(self.uint())]
        instructions = []
        for pc, word in enumerate(code):
            op, a, b, c, bx = word&63, (word>>6)&255, word>>23, (word>>14)&511, word>>14
            if op >= len(OPS): raise ValueError(f'Unknown opcode {op}')
            item = {'pc':pc, 'op':OPS[op], 'a':a, 'b':b, 'c':c, 'bx':bx}
            if OPS[op] in ('LOADK','GETGLOBAL','SETGLOBAL'):
                item['constant'] = constants[bx]
            elif OPS[op] in ('GETTABLE','SETTABLE','SELF','ADD','SUB','MUL','DIV','EQ','LT','LE'):
                if b&256: item['kb'] = constants[b&255]
                if c&256: item['kc'] = constants[c&255]
            instructions.append(item)
        return dict(source=source,first_line=first,last_line=last,meta=meta,
                    constants=constants,instructions=instructions,children=children)


def read(path):
    reader = Reader(path.read_bytes())
    header = reader.take(12)
    if header != b'6021+\x00\x01\x00\x00\x00\x00\x00':
        raise ValueError(f'Unverified chunk header {header.hex()}')
    function = reader.function()
    if reader.pos != len(reader.raw):
        raise ValueError('Unconsumed chunk bytes')
    return function


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    value=read(args.source)
    args.output.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Validated complete chunk: {args.source}; output: {args.output}')
