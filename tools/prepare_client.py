"""Prepare a fresh, traceable local-connection copy of the original client."""
import hashlib
import json
from pathlib import Path
import re
import shutil

if __package__:
    from .project_paths import ORIGINAL_CLIENT
    from .prepare_client_titles import verify_or_apply
else:
    from project_paths import ORIGINAL_CLIENT
    from prepare_client_titles import verify_or_apply

PROJECT = Path(__file__).resolve().parents[1]
ORIGINAL = ORIGINAL_CLIENT
DESTINATION = PROJECT / "client"
EXPECTED = "916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7"
CLIENT_STATE = {'data/TaskEdit.exe', 'data/auto.dat', 'data/channel.dat', 'data/chathis.dat', 'data/chatreg.dat', 'data/list.dat', 'data/rules.dat', 'data/savedata.dat', 'data/serverdata.dat', 'data/update/list.dat', 'data/usd.dat'}


def main():
    original = (ORIGINAL / "stoneage.exe").read_bytes()
    if hashlib.sha256(original).hexdigest() != EXPECTED:
        raise RuntimeError("Original target executable changed; compare before preparing")
    output = bytearray(original)
    patches = []
    remote, local = b"http://202.189.23.53:90", b"http://127.0.0.1:19090"
    for match in re.finditer(re.escape(remote) + rb"[^\x00]*\x00", original):
        before = match.group()
        after = before.replace(remote, local, 1)
        if len(after) > len(before):
            raise RuntimeError("Local URL does not fit the existing string storage")
        output[match.start():match.end()] = after.ljust(len(before), b"\x00")
        patches.append({"offset": match.start(), "before": before[:-1].decode(), "after": after[:-1].decode()})
    if not any(p["before"].endswith("/25server/serverlist.php") for p in patches):
        raise RuntimeError("Server list URL not found")
    DESTINATION.mkdir(exist_ok=True)
    for directory in ["data", "lua", "map", "path"]:
        for source in (ORIGINAL / directory).rglob("*"):
            if not source.is_file():
                continue
            if source.relative_to(ORIGINAL).as_posix() in CLIENT_STATE:
                continue  # Login, endpoint and updater state belong to this instance.
            target = DESTINATION / source.relative_to(ORIGINAL)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                shutil.copyfile(source, target)
        print("Copied", directory, flush=True)
    for source in ORIGINAL.glob("*.dll"):
        target = DESTINATION / source.name
        if not target.exists():
            shutil.copyfile(source, target)
    for directory in ["chat", "battleMsg", "screenshot"]:
        (DESTINATION / directory).mkdir(exist_ok=True)
    (DESTINATION / "stoneage.exe").write_bytes(output)
    manifest = {
        "source": str(ORIGINAL / "stoneage.exe"), "source_sha256": EXPECTED,
        "output": str(DESTINATION / "stoneage.exe"), "output_sha256": hashlib.sha256(output).hexdigest(),
        "size": len(output), "changes": patches,
        "resource_policy": "Separate file copies; no hard links, junctions, session cache, or old patched executable.",
    }
    (PROJECT / "evidence/client-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Prepared original target copy with {len(patches)} local HTTP URL changes.")
    print(json.dumps(verify_or_apply(apply=True)))


if __name__ == "__main__":
    main()
