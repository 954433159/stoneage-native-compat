"""Prepare a fresh, traceable local-connection copy of the original client."""
import hashlib
import json
import os
from pathlib import Path
import shutil

if __package__:
    from .client_endpoints import retarget_client_blob
    from .project_paths import ORIGINAL_CLIENT
    from .prepare_client_titles import verify_or_apply
else:
    from client_endpoints import retarget_client_blob
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
    # Account, directory and every observed update host move together. A longer
    # LAN origin is rejected instead of truncating a native C string.
    account_origin = os.environ.get("STONEAGE_ACCOUNT_ORIGIN", "http://127.0.0.1:19090")
    update_origin = os.environ.get("STONEAGE_UPDATE_ORIGIN") or None
    retargeted = retarget_client_blob(original, account_origin, update_origin)
    output, patches = retargeted["blob"], retargeted["changes"]
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
        "unchanged_hosts": sorted({item["host"] for item in retargeted["unchanged"]}),
        "resource_policy": "Separate file copies; no hard links, junctions, session cache, or old patched executable.",
    }
    (PROJECT / "evidence/client-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Prepared original target copy with {len(patches)} local HTTP URL changes.")
    print(json.dumps(verify_or_apply(apply=True)))


if __name__ == "__main__":
    main()
