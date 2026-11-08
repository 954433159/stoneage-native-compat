"""Prepare source-only native copies from the original user-provided projects."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

if __package__:
    from .project_paths import REFERENCE_SOURCE
else:
    from project_paths import REFERENCE_SOURCE

PROJECT = Path(__file__).resolve().parents[1]
ORIGINAL = REFERENCE_SOURCE
COMPONENTS = {
    "gmsv": ORIGINAL / "sq_server_game/175Gmsv/GameServer",
    "saac": ORIGINAL / "sq_server_acc/175Server",
}
NAMESPACE = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def normalize_source(raw: bytes) -> tuple[bytes, str]:
    try:
        raw.decode("utf-8")
        return raw, "utf-8"
    except UnicodeDecodeError:
        decoded = raw.decode("gb18030")
        if decoded.encode("gb18030") != raw:
            raise RuntimeError("Source encoding does not round-trip exactly")
        return decoded.encode("utf-8"), "gb18030"


def main() -> None:
    manifest = {"format": 1, "components": {}}
    patches = json.loads((PROJECT / "native/patches.json").read_text(encoding="utf-8"))
    overrides = {}
    for patch in patches:
        component, relative = patch["file"].split("/", 1)
        key = (component, relative)
        content = overrides.get(key, normalize_source((COMPONENTS[component] / relative).read_bytes())[0])
        before, after = patch["before"].encode(), patch["after"].encode()
        if content.count(before) != 1:
            raise RuntimeError(f"Patch context is not unique: {patch['file']}")
        overrides[key] = content.replace(before, after)
    for (component, relative), content in overrides.items():
        target = PROJECT / "native/overrides" / component / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != content:
            target.write_bytes(content)
    cmake = [
        "cmake_minimum_required(VERSION 3.16)",
        "project(stoneage_clean C)",
        "set(CMAKE_C_STANDARD 99)",
        "find_package(PkgConfig REQUIRED)",
        "pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET libmariadb)",
        "find_package(ZLIB REQUIRED)",
    ]
    for name, original in COMPONENTS.items():
        project_file = next(original.glob("*.vcxproj"))
        tree = ET.parse(project_file)
        entries = tree.findall(".//m:ClCompile[@Include]", NAMESPACE)
        if any(list(entry) for entry in entries):
            raise RuntimeError(f"Per-file build configuration requires review: {project_file}")
        compile_files = sorted(entry.attrib["Include"].replace("\\", "/") for entry in entries)
        if any(not (original / path).is_file() for path in compile_files):
            raise RuntimeError(f"Missing original compilation input in {name}")
        destination = PROJECT / "vendor" / name
        files = {}
        build_files = {}
        for source in sorted(original.rglob("*")):
            if not source.is_file() or source.suffix.lower() not in {".c", ".h", ".hpp", ".s", ".inc", ".dat"}:
                continue
            relative = source.relative_to(original)
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                if target.read_bytes() != source.read_bytes():
                    raise RuntimeError(f"Refusing to overwrite modified prepared source: {target}")
            else:
                shutil.copyfile(source, target)
            files[relative.as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
            normalized, encoding = normalize_source(source.read_bytes())
            prepared = overrides.get((name, relative.as_posix()), normalized)
            build_target = PROJECT / "native/overrides" / name / relative
            build_target.parent.mkdir(parents=True, exist_ok=True)
            if not build_target.exists() or build_target.read_bytes() != prepared:
                build_target.write_bytes(prepared)
            build_files[relative.as_posix()] = {"original_encoding": encoding,
                                                "sha256": hashlib.sha256(prepared).hexdigest()}
        manifest["components"][name] = {
            "source": str(original),
            "project_file": str(project_file),
            "project_sha256": hashlib.sha256(project_file.read_bytes()).hexdigest(),
            "compile_files": compile_files,
            "files": files,
            "build_files": build_files,
        }
        cmake.append(f"add_executable({name}")
        for path in compile_files:
            base = "overrides"
            cmake.append(f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{base}/{name}/{path}"')
        cmake.append(")")
        if name == "gmsv":
            cmake.append('target_sources(gmsv PRIVATE "wire_charset.c" "bind_address.c" "app_role.c" "app_control.c" "app_mail.c" "app_storage.c" "app_attributes.c" "app_market.c" "app_features.c")')
        cmake.append(f'target_include_directories({name} PRIVATE "." "include" "overrides/{name}" "overrides/{name}/include" "overrides/{name}/lua")')
        cmake.append(f"target_compile_definitions({name} PRIVATE _GNU_SOURCE LUA_COMPAT_MODULE)")
        cmake.append(f"target_compile_options({name} PRIVATE -fcommon -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types)")
        cmake.append(f"target_link_libraries({name} PRIVATE PkgConfig::MARIADB ZLIB::ZLIB m dl pthread rt)")
        if name == "gmsv":
            lua_files = [path for path in compile_files if path.startswith("lua/")]
            cmake.append('add_executable(lua_reference "overrides/gmsv/lua/lua.c"')
            for path in lua_files:
                base = "overrides"
                cmake.append(f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{base}/{name}/{path}"')
            cmake.append(")")
            cmake.append('target_include_directories(lua_reference PRIVATE "overrides/gmsv/lua")')
            cmake.append("target_compile_definitions(lua_reference PRIVATE _GNU_SOURCE LUA_COMPAT_MODULE)")
            cmake.append("target_link_libraries(lua_reference PRIVATE m dl)")
        print(f"{name}: {len(files)} source/header files, {len(compile_files)} compilation units")
    (PROJECT / "evidence").mkdir(exist_ok=True)
    (PROJECT / "native").mkdir(exist_ok=True)
    (PROJECT / "evidence/source-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (PROJECT / "evidence/native-changes.json").write_text(json.dumps(patches, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (PROJECT / "native/CMakeLists.txt").write_text("\n".join(cmake) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
