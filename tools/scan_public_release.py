"""Fail closed on private state or high-confidence secrets in a publication tree.

Reports paths and finding categories only. Does not print matched secret values.
Use --git-index immediately before publishing; runtime files created for local
verification must never enter the index. This is an audit aid, not a guarantee.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

DENIED_DIRS = {'runtime', 'backups', '.venv', 'build', 'client', 'client-reference',
               'analysis-deps', 'vendor', '__pycache__', '.git'}
DENIED_NAMES = {'savedata.dat', 'serverdata.dat', 'chathis.dat', 'chatreg.dat',
                'usd.dat', 'auto.dat', 'settings.json', 'inputs.json'}
PATTERNS = {
    'private-key': re.compile(rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----'),
    'github-token': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})'),
    'machine-user-path': re.compile(rb'(?:[A-Za-z]:[\\/]+Users[\\/]+[A-Za-z0-9_-]+|/mnt/[a-z]/Users/[A-Za-z0-9_-]+)', re.I),
}


def scan(root, paths, forbidden=(), contents=None):
    findings = []
    for relative in sorted(paths):
        path = root/relative
        if path.is_symlink() or path.resolve() != path.absolute():
            findings.append({'file':relative, 'kind':'redirected-path'}); continue
        parts = Path(relative).parts
        if any(p in DENIED_DIRS for p in parts) or Path(relative).name in DENIED_NAMES:
            findings.append({'file':relative, 'kind':'private-or-generated-state'}); continue
        if contents is None and not path.is_file():
            findings.append({'file':relative, 'kind':'missing-file'}); continue
        raw = contents(relative) if contents else path.read_bytes()
        if len(raw) >= 100*1024*1024:
            findings.append({'file':relative, 'kind':'large-git-blob'})
        for label, regex in PATTERNS.items():
            if regex.search(raw): findings.append({'file':relative, 'kind':label})
        if any(value in raw for value in forbidden if len(value) >= 8):
            findings.append({'file':relative, 'kind':'known-private-value'})
    return findings


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--git-index',action='store_true')
    args=parser.parse_args();root=args.root.resolve()
    if args.git_index:
        data=subprocess.check_output(['git','-C',str(root),'ls-files','-z'])
        paths=[p.decode('utf8') for p in data.split(b'\0') if p]
        contents=lambda relative:subprocess.check_output(['git','-C',str(root),'show',':'+relative])
    else:
        paths=[]
        for base,directories,files in os.walk(root,followlinks=False):
            parent=Path(base)
            for name in list(directories):
                if name=='.git':directories.remove(name)
                elif name in DENIED_DIRS:
                    paths.append((parent/name).relative_to(root).as_posix())
                    directories.remove(name)
            paths.extend((parent/name).relative_to(root).as_posix() for name in files)
        contents=None
    findings=scan(root,paths,contents=contents)
    print(json.dumps({'checked_files':len(paths),'findings':findings},ensure_ascii=False))
    if findings:raise SystemExit(1)


if __name__=='__main__':main()
