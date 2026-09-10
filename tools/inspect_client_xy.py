"""Read-only decoding of verified 20260805 .xy Lua source assets.

The transform is derived from target EXE 0x6ad230 -> 0x6acef0 ->
0x6b0910, with reverse helper 0x6b1dd0 and complement helper 0x6b1e40.
This module never imports or executes Lua and never changes client assets.
Unlike 6021 .cf chunks, .xy files contain permuted/complemented source bytes.
"""
import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE_SHA256 = '98d03f47eb00066b34cf1913ce1923e4554aae2542cf941aeca1a4af7a26d8b9'
VERIFIED = {
    'menu.xy': '9b787048493f9e7986a9e03a2274940fb58d49fdaf8958da8e1e10b7ae4e09c8',
    'protocol.xy': '10a567410e4c24ef53ea833372845bca35c5b9787339dcf9025507391fde8143',
    'winconfig.xy': '87aecfb88f6592550812919c4a3269fce45d70c9c1b21ae03cd8ed61a6eb78ce',
    'win/1026.xy': 'a104747c6017340464896d83507c55f0f15d4a8a9611141ceef02b1477f7747f',
}
STEPS = (81, 94, 66, 5, 9, 35, 71, 13, 26)


def decode(raw):
    """Apply the target's mode-zero inverse in reverse seed order."""
    n = len(raw)
    for percent in reversed(STEPS):
        cut = n - n * percent // 100
        raw = bytes(byte ^ 255 for byte in raw[cut:]) + raw[:cut][::-1]
    return raw


def encode(decoded):
    """Exact forward inverse, used only for lossless verification."""
    n = len(decoded)
    for percent in STEPS:
        count = n * percent // 100
        decoded = decoded[count:][::-1] + bytes(byte ^ 255 for byte in decoded[:count])
    return decoded


def read(relative):
    """Decode only an audited asset after checking both EXE and asset hashes."""
    if relative not in VERIFIED:
        raise ValueError('Asset is outside the verified target allowlist')
    if hashlib.sha256((ROOT / 'client/stoneage.exe').read_bytes()).hexdigest() != EXE_SHA256:
        raise ValueError('Target EXE SHA256 mismatch')
    raw = (ROOT / 'client/lua' / relative).read_bytes()
    if hashlib.sha256(raw).hexdigest() != VERIFIED[relative]:
        raise ValueError('Target Lua asset SHA256 mismatch')
    decoded = decode(raw)
    if encode(decoded) != raw:
        raise ValueError('Target resource transform does not round-trip')
    return decoded.decode('utf-8', errors='strict')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('asset', choices=sorted(VERIFIED))
    parser.add_argument('--lines', type=int, nargs=2, metavar=('FIRST', 'LAST'))
    args = parser.parse_args()
    lines = read(args.asset).splitlines()
    first, last = args.lines or (1, len(lines))
    if not 1 <= first <= last <= len(lines):
        parser.error('Line range is outside decoded source')
    print(f'Verified {args.asset}: {VERIFIED[args.asset]}; {len(lines)} logical lines', file=sys.stderr)
    for number in range(first, last + 1):
        print(f'{number}: {lines[number - 1]}')


if __name__ == '__main__':
    main()
