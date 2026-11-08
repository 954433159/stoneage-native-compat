"""Read-only check of the 564 locked reference inputs, then an optional source pack.

The pack is created only after every locked reference hash matches. It copies
C, headers and project files. It does not copy the resource archive, the SQL
dump, or the opaque data file, and it never writes inside the source tree or
this repository. Run it on the machine that already has the source. This file
does not fetch or embed those inputs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_EXTENSIONS = {".c", ".h", ".hpp", ".vcxproj"}
EXCLUDED_EXTENSIONS = {
    ".sql": "SQL 转储可能含历史账号或记录，只在原目录就地使用，不打包",
    ".zip": "资源归档属于游戏素材，不打包",
    ".dat": "非源码二进制未审阅，不打包",
}
LOCKED_CLIENT_EXE = "stoneage.exe"


def load_lock(path: Path) -> dict:
    lock = json.loads(path.read_text(encoding="utf-8"))
    if lock.get("format") != 2 or not isinstance(lock.get("reference"), list):
        raise RuntimeError("Unsupported original-input lock format")
    seen = set()
    for entry in lock["reference"]:
        relative = entry.get("path")
        digest = entry.get("sha256")
        if not isinstance(relative, str) or not isinstance(digest, str):
            raise RuntimeError("Lock entry is incomplete")
        parts = Path(relative).parts
        if not parts or relative.startswith(("/", "\\")) or ".." in parts or "\\" in relative:
            raise RuntimeError("Lock path is not a relative file: " + relative)
        if len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest.lower()):
            raise RuntimeError("Lock digest is invalid: " + relative)
        if relative in seen:
            raise RuntimeError("Duplicate lock path: " + relative)
        seen.add(relative)
    return lock


def classify(relative: str) -> str:
    extension = Path(relative).suffix.lower()
    if extension in SOURCE_EXTENSIONS:
        return "package"
    if extension in EXCLUDED_EXTENSIONS:
        return "exclude"
    return "unknown"


def locked_layout(lock: dict) -> dict:
    groups: dict[str, int] = {}
    kinds = {"package": 0, "exclude": 0, "unknown": 0}
    excluded = []
    for entry in lock["reference"]:
        relative = entry["path"]
        kind = classify(relative)
        kinds[kind] += 1
        if kind == "exclude":
            excluded.append({"path": relative, "reason": EXCLUDED_EXTENSIONS[Path(relative).suffix.lower()]})
        parts = Path(relative).parts
        if parts[0] == "sq_server_acc":
            top = "sq_server_acc/175Server"
        elif parts[0] == "Stoneage_80":
            top = "Stoneage_80/lua/lua"
        elif len(parts) > 2 and parts[1] == "175Gmsv":
            top = "sq_server_game/175Gmsv/GameServer"
        else:
            top = relative
        groups[top] = groups.get(top, 0) + 1
    return {
        "reference_files": len(lock["reference"]),
        "package_files": kinds["package"],
        "excluded_files": excluded,
        "unknown_files": kinds["unknown"],
        "directories": [{"path": path, "files": count} for path, count in sorted(groups.items())],
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _locked_file(root: Path, relative: str) -> Path:
    path = root.joinpath(*Path(relative).parts)
    if path.is_symlink():
        raise RuntimeError("Refusing redirected input: " + relative)
    resolved = path.resolve(strict=True)
    resolved.relative_to(root.resolve())
    if not resolved.is_file() or resolved.is_symlink():
        raise RuntimeError("Missing or redirected input: " + relative)
    return resolved


def verify_reference(source: Path, lock: dict) -> dict:
    mismatches = []
    missing = []
    checked = []
    for entry in lock["reference"]:
        relative, expected = entry["path"], entry["sha256"].lower()
        try:
            path = _locked_file(source, relative)
        except (OSError, RuntimeError):
            missing.append(relative)
            continue
        actual = _sha256(path)
        if actual != expected:
            mismatches.append({"path": relative, "actual": actual, "expected": expected})
        else:
            checked.append({"path": relative, "sha256": actual, "kind": classify(relative)})
    return {
        "ok": not missing and not mismatches and len(checked) == len(lock["reference"]),
        "checked": len(checked),
        "missing": missing,
        "mismatches": mismatches,
        "files": checked,
    }


def _refuse_output(source: Path, output: Path) -> None:
    source_root = source.resolve()
    destination = output.resolve() if output.exists() else output.absolute()
    if output.exists():
        raise RuntimeError("Output already exists; choose a new directory")
    try:
        destination.relative_to(source_root)
    except ValueError:
        inside_source = False
    else:
        inside_source = True
    if inside_source or destination == source_root:
        raise RuntimeError("Refusing to write the package inside the source tree")
    repository = ROOT.resolve()
    try:
        destination.relative_to(repository)
    except ValueError:
        inside_repo = False
    else:
        inside_repo = True
    if inside_repo or destination == repository:
        raise RuntimeError("Refusing to write private sources inside this repository")


def package_sources(source: Path, output: Path, lock: dict) -> dict:
    report = verify_reference(source, lock)
    if not report["ok"]:
        raise RuntimeError("Reference hash check failed; nothing was packaged")
    unknown = [item["path"] for item in report["files"] if item["kind"] == "unknown"]
    if unknown:
        raise RuntimeError("Lock contains an unclassified file: " + unknown[0])
    _refuse_output(source, output)
    staging = output.with_name(output.name + ".partial")
    if staging.exists():
        raise RuntimeError("Partial output already exists; remove it before retrying")
    copied = []
    excluded = []
    try:
        for item in report["files"]:
            if item["kind"] != "package":
                excluded.append({"path": item["path"], "reason": EXCLUDED_EXTENSIONS[Path(item["path"]).suffix.lower()]})
                continue
            target = staging.joinpath(*Path(item["path"]).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            with _locked_file(source, item["path"]).open("rb") as incoming, target.open("xb") as outgoing:
                for chunk in iter(lambda: incoming.read(1024 * 1024), b""):
                    digest.update(chunk)
                    outgoing.write(chunk)
            if digest.hexdigest() != item["sha256"]:
                raise RuntimeError("Input changed while reading: " + item["path"])
            copied.append({"path": item["path"], "sha256": item["sha256"]})
        manifest = {
            "verified_reference_files": report["checked"],
            "packaged_files": len(copied),
            "excluded": excluded,
            "files": copied,
            "source_tree_modified": False,
        }
        (staging / "package-manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        staging.rename(output)
    except Exception:
        if staging.exists():
            for path in sorted(staging.rglob("*"), reverse=True):
                if path.is_file() or path.is_symlink():
                    path.unlink()
                elif path.is_dir():
                    path.rmdir()
        raise
    return manifest


def verify_client_exe(path: Path, lock: dict) -> dict:
    expected = next(entry["sha256"] for entry in lock["client"] if entry["path"] == LOCKED_CLIENT_EXE)
    if path.is_symlink() or not path.is_file():
        raise RuntimeError("Client executable is missing or redirected")
    actual = _sha256(path)
    return {"path": str(path), "matches_locked_stoneage_exe": actual == expected.lower(),
            "actual": actual, "expected": expected.lower()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=ROOT / "release-input-lock.json")
    parser.add_argument("--explain", action="store_true")
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--client-exe", type=Path)
    args = parser.parse_args()
    lock = load_lock(args.lock)
    if args.explain:
        print(json.dumps(locked_layout(lock), indent=2, ensure_ascii=False))
        return
    if args.client_exe:
        print(json.dumps(verify_client_exe(args.client_exe, lock), ensure_ascii=False))
    if args.source_root is None:
        if args.client_exe:
            return
        parser.error("Pass --explain or --source-root")
    source = args.source_root
    if args.output is None or args.check_only:
        report = verify_reference(source, lock)
        printable = {key: report[key] for key in ("ok", "checked", "missing", "mismatches")}
        print(json.dumps(printable, indent=2, ensure_ascii=False))
        if not report["ok"]:
            raise SystemExit(2)
        return
    manifest = package_sources(source, args.output, lock)
    print(json.dumps({"packaged_files": manifest["packaged_files"],
                      "excluded": [item["path"] for item in manifest["excluded"]]},
                     indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
