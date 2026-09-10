"""Run local extended regressions against prepared native sources and runtime Lua.

Run from Linux/WSL after preparing sources/runtime and building both Lua VMs.
Outputs are generated under build/checks/extended. SQL fixture executables are
compiled to the paths used by run_feature_database_checks.py, but NEVER run
here. Database acceptance requires a separate explicit command:
    python3 -B tools/run_feature_database_checks.py market
    python3 -B tools/run_feature_database_checks.py storage

--target-audit adds the optional target-EXE anchors and packet replay. It needs
pefile/capstone; --target-audit-python may select a Python that has those modules.
No resource builders, services, accounts or current application DB are touched.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
OUTPUT = PROJECT / 'build/checks/extended'
SQL_OUTPUTS = {
    'market': PROJECT / 'evidence/services-market-db-test',
    'storage': PROJECT / 'evidence/storage-db-test',
}
SOURCES = (
    'tools/run_extended_checks.py', 'tests/extended_checks_runner.py',
    'tools/build_client_lua_reference.py', 'tools/client_lua_reference.c',
    'tools/prepare_client_titles.py', 'tests/client_titles.lua',
    'native/client-title-names.json', 'native/client-title-compat.lua',
    'native/client-title-text.lua',
    'native/client-storage-menu.lua', 'native/client-storage-entry.lua', 'native/client-pet-growth.lua',
    'tests/pet_effect_state.lua', 'tests/client_pet_effects.lua',
    'tests/npc_personal_storage.lua', 'tests/client_storage_entry.lua',
    'tests/pet_growth.lua', 'tests/client_pet_growth.lua',
    'native/reference-pet-collector.lua', 'tests/reference_pet_collector.lua',
    'native/reference-pet-ranking-collector.lua', 'tests/reference_pet_ranking_collector.lua',
    'tools/import_reference_pet_observations.py', 'tests/reference_pet_observations.py',
    'tools/prepare_external_pets.py', 'tests/external_pet_species.py',
    'native/external-pet-species.json', 'native/external-pet-skills.json',
    'native/external-pet-service.lua', 'tests/external_pet_service.lua',
    'native/include/app_pet_skill_options.h', 'tests/pet_skill_profiles.py',
    'native/include/app_pet_skill_wire.h', 'tests/pet_skill_wire.py',
    'native/lua/app_events.lua', 'tools/prepare_activities.py', 'tests/activity_rewards.lua',
    'native/client-activity-levels.lua', 'client/lua/win/activity/3.xy',
    'native/lua/app_level140_pill.lua', 'tools/prepare_level140_pill.py', 'tests/level140_pill.lua',
    'native/lua/app_ride_models.lua', 'native/client-ride-models.lua', 'tools/prepare_ride_models.py',
    'tests/ride_models.lua', 'tests/client_ride_models.lua',
    'runtime/gmsv/data/ablua/familyridefunction.lua', 'client/compat/petwnd-original.cf',
    'tests/task_menu.lua', 'tests/task_progress.py', 'native/overrides/gmsv/lssproto_serv.c', 'client/lua/win/1003.xy',
    'runtime/gmsv/data/ablua/npc/querymission/querymission.lua',
    'native/app_market.c', 'native/include/app_market.h',
    'native/app_storage.c', 'native/include/app_storage.h',
    'native/app_attributes.c', 'native/app_attributes.h',
    'native/overrides/gmsv/char/char_item.c', 'native/overrides/gmsv/item/item.c',
    'tests/service_market.c', 'tests/service_market_client.lua',
    'tests/service_menus.lua', 'tests/app_storage.c', 'tests/storage_database.c',
    'tests/attributes_menu.c', 'tests/attributes_menu.lua',
    'tests/attributes_inventory.c', 'tests/attributes_item_fields.c',
    'tests/attributes_ordinary.lua', 'tests/attributes_packets.py',
    'runtime/gmsv/data/ablua/samenu.lua', 'runtime/gmsv/data/ablua/chartitle.lua',
    'runtime/gmsv/data/ablua/trade.lua',
    'runtime/gmsv/data/ablua/npc/petatterrect/petatterrect.lua',
    'runtime/gmsv/data/ablua/npc/pool/poolitem.lua',
    'runtime/gmsv/data/enemybase1.txt', 'runtime/gmsv/setup.cf',
    'runtime/gmsv/data/ablua/dispatchmessage.lua', 'runtime/gmsv/data/ablua/item/skin.lua',
    'runtime/gmsv/data/ablua/npc/shop/vippoint.lua', 'runtime/gmsv/data/itemset6.csv',
    'runtime/gmsv/data/ablua/npc/newFB/NewFB.lua', 'runtime/gmsv/data/ablua/npc/YamaKing/MoWang.lua',
    'runtime/gmsv/data/ablua/npc/hunting/Hunting.lua',
    'client/lua/title.xy', 'client/lua/win/1017.xy', 'client/lua/win/1119.xy',
    'client/lua/win/1123.xy', 'client/lua/win/1131.xy',
    'client/lua/pcwnd.cf', 'client/compat/pcwnd-original.cf', 'client/lua/cons.cf',
    'client/lua/menu.xy', 'client/lua/win/1199.xy', 'client/lua/win/1027.xy',
    'client/lua/petwnd.cf', 'client/lua/effectdata.cf', 'client/lua/itemwnd.cf',
)


@dataclass(frozen=True)
class Step:
    name: str
    kind: str
    command: list[str]


def child_environment() -> dict[str, str]:
    environment = os.environ.copy()
    for key in list(environment):
        if key.startswith(('STONE_MARKET_TEST_', 'STONE_STORAGE_TEST_', 'MYSQL_', 'MARIADB_')):
            del environment[key]
    environment.update(
        STONE_CHECK_OUTPUT_DIR=str(OUTPUT),
        ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
        UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
        PYTHONOPTIMIZE='0',
    )
    return environment


def python_path(path: Path, interpreter: str) -> str:
    """WSL can explicitly use a Windows Python for the optional audit only."""
    if sys.platform.startswith('linux') and interpreter.lower().endswith('.exe'):
        return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
    return str(path)


def build_steps(cc: str, target_audit: bool = False, audit_python: str | None = None) -> list[Step]:
    common = [cc, '-D_GNU_SOURCE', '-DLUA_COMPAT_MODULE', '-Wno-int-conversion',
              '-Wno-incompatible-pointer-types', '-Wno-multichar', '-Inative',
              '-Inative/include', '-Inative/overrides/gmsv',
              '-Inative/overrides/gmsv/include', '-Inative/overrides/gmsv/lua',
              '-I/usr/include/mariadb']
    sanitize = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-fno-sanitize-recover=all']
    strict = ['-Werror=implicit-function-declaration', '-Werror=return-type']
    sections = ['-ffunction-sections', '-fdata-sections']
    steps = [Step('runner-isolation', 'test', [sys.executable, '-B', 'tests/extended_checks_runner.py'])]
    for module in ('market', 'storage', 'attributes'):
        steps.append(Step(f'{module}-syntax', 'compile', common + strict + ['-fsyntax-only', f'native/app_{module}.c']))

    def native_test(name: str, source: str, extra: list[str] | None = None) -> None:
        executable = OUTPUT / name
        steps.append(Step(f'{name}-build', 'compile', common + sanitize + (extra or []) +
                          [source, '-Wl,--gc-sections', '-o', str(executable)]))
        steps.append(Step(name, 'test', [str(executable)]))

    def lua_test(name: str, source: str) -> None:
        steps.append(Step(name, 'test', [str(PROJECT / 'build/lua_reference'), source]))

    native_test('market-native', 'tests/service_market.c')
    lua_test('market-client', 'tests/service_market_client.lua')
    lua_test('service-menus', 'tests/service_menus.lua')
    lua_test('activity-rewards', 'tests/activity_rewards.lua')
    lua_test('level140-pill', 'tests/level140_pill.lua')
    lua_test('ride-models', 'tests/ride_models.lua')
    steps.append(Step('client-ride-models', 'test',
        [str(PROJECT/'build/client_lua_reference'),'tests/client_ride_models.lua','client']))
    lua_test('task-menu', 'tests/task_menu.lua')
    steps.append(Step('task-progress-wire', 'test', [sys.executable, '-B', 'tests/task_progress.py']))
    native_test('attributes-menu-native', 'tests/attributes_menu.c', strict)
    lua_test('attributes-menu-lua', 'tests/attributes_menu.lua')
    steps.append(Step('client-native-titles', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/client_titles.lua', 'client']))
    steps.append(Step('pet-effect-state', 'test',
        [str(PROJECT / 'build/lua_reference'), 'tests/pet_effect_state.lua', str(OUTPUT / 'pet-effects-reply.txt')]))
    steps.append(Step('client-pet-effects', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/client_pet_effects.lua', 'client', str(OUTPUT)]))
    lua_test('npc-personal-storage', 'tests/npc_personal_storage.lua')
    steps.append(Step('client-storage-entry', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/client_storage_entry.lua', 'client']))
    steps.append(Step('pet-growth', 'test',
        [str(PROJECT / 'build/lua_reference'), 'tests/pet_growth.lua', str(OUTPUT)]))
    steps.append(Step('client-pet-growth', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/client_pet_growth.lua', 'client', str(OUTPUT)]))
    steps.append(Step('native-pet-skill-wire', 'test',
        [sys.executable, '-B', 'tests/pet_skill_wire.py']))
    steps.append(Step('reference-pet-collector', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/reference_pet_collector.lua']))
    steps.append(Step('reference-pet-observations', 'test',
        [sys.executable, '-B', 'tests/reference_pet_observations.py']))
    steps.append(Step('reference-pet-ranking-collector', 'test',
        [str(PROJECT / 'build/client_lua_reference'), 'tests/reference_pet_ranking_collector.lua']))
    steps.append(Step('external-pet-species', 'test',
        [sys.executable, '-B', 'tests/external_pet_species.py']))
    steps.append(Step('external-pet-pack-current', 'test',
        [sys.executable, '-B', 'tools/prepare_external_pets.py', '--verify']))
    lua_test('external-pet-service', 'tests/external_pet_service.lua')
    steps.append(Step('native-pet-skill-profiles', 'test',
        [sys.executable, '-B', 'tests/pet_skill_profiles.py']))
    helper = OUTPUT / 'attributes-si-helper.h'
    native_test('ordinary-inventory', 'tests/attributes_inventory.c', [f'-DATTR_HELPER="{helper}"'])
    lua_test('ordinary-points', 'tests/attributes_ordinary.lua')
    native_test('ordinary-item-fields', 'tests/attributes_item_fields.c', strict + sections)
    native_test('storage-native', 'tests/app_storage.c')

    # These are compiler invocations only. No DB driver is ever a test step.
    steps.append(Step('market-sql-build', 'database-compile', common + sanitize +
                      ['-DSTONE_MARKET_REAL_DB', 'tests/service_market.c', '-lmariadb', '-o', str(SQL_OUTPUTS['market'])]))
    steps.append(Step('storage-sql-build', 'database-compile', common + sanitize + sections +
                      ['tests/storage_database.c', '-Wl,--gc-sections', '-lmariadb', '-o', str(SQL_OUTPUTS['storage'])]))
    if target_audit:
        interpreter = audit_python or sys.executable
        steps.append(Step('target-packet-audit', 'test', [interpreter, '-X', 'utf8', '-B',
            python_path(PROJECT / 'tests/attributes_packets.py', interpreter),
            '--fixture', python_path(OUTPUT / 'ordinary-item-fields.log', interpreter),
            '--output-dir', python_path(OUTPUT, interpreter)]))
    return steps


def prepare_inputs() -> None:
    required = [PROJECT / p for p in SOURCES] + [
        PROJECT / 'build/lua_reference', PROJECT / 'build/client_lua_reference']
    missing = [str(p.relative_to(PROJECT)) for p in required if not p.is_file()]
    if missing:
        raise RuntimeError('Missing prepared inputs: ' + ', '.join(missing) +
                           '. Prepare sources/runtime and build lua_reference and client_lua_reference first; '
                           'this runner does not prepare or deploy.')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (PROJECT / 'evidence').mkdir(exist_ok=True)
    source = (PROJECT / 'native/overrides/gmsv/char/char_item.c').read_text(encoding='utf-8')
    signature = 'static BOOL CHAR_sendSIToCli('
    if source.count(signature) != 1:
        raise RuntimeError('Cannot identify the current native CHAR_sendSIToCli definition')
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    # Extract the current implementation, never a staged old "after" header.
    (OUTPUT / 'attributes-si-helper.h').write_text(source[start:end], encoding='utf-8')


def execute(steps: list[Step]) -> dict:
    report = {'status': 'running', 'database_executed': False, 'services_started': False,
              'output_directory': str(OUTPUT.relative_to(PROJECT)), 'steps': []}
    environment = child_environment()
    begin = time.monotonic()
    try:
        for step in steps:
            # Fail closed if a future edit accidentally schedules a DB driver.
            if Path(step.command[0]).name in {p.name for p in SQL_OUTPUTS.values()} or any(
                    Path(arg).name == 'run_feature_database_checks.py' for arg in step.command):
                raise RuntimeError('Database execution must use the separate explicit database checker')
            started = time.monotonic()
            completed = subprocess.run(step.command, cwd=PROJECT, env=environment,
                                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=55)
            output = completed.stdout + completed.stderr
            log = OUTPUT / (step.name + '.log')
            log.write_text(output, encoding='utf-8')
            item = {'name': step.name, 'kind': step.kind, 'exit_code': completed.returncode,
                    'seconds': round(time.monotonic() - started, 3),
                    'command': step.command, 'log': str(log.relative_to(PROJECT))}
            if step.name == 'storage-native':
                item['groups'] = [line.removeprefix('storage: ') for line in output.splitlines() if line.startswith('storage: ')]
            report['steps'].append(item)
            if completed.returncode:
                print(f'FAIL {step.name}: {log.relative_to(PROJECT)}', flush=True)
                print(output[-3500:], flush=True)
                raise RuntimeError(f'{step.name} exited {completed.returncode}')
            print(f'PASS {step.name}', flush=True)
        report['status'] = 'passed'
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['status'] = 'failed'
        report['error'] = str(error)
    finally:
        report['seconds'] = round(time.monotonic() - begin, 3)
        report['source_hashes'] = {p: hashlib.sha256((PROJECT / p).read_bytes()).hexdigest() for p in SOURCES}
        report['database_compiled'] = {name: {'path': str(path.relative_to(PROJECT)),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for name, path in SQL_OUTPUTS.items()
            if any(step['name'] == f'{name}-sql-build' and step['exit_code'] == 0 for step in report['steps'])}
        (OUTPUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cc', default='cc', help='Native Linux compiler executable (default: cc)')
    parser.add_argument('--list', action='store_true', help='Print planned commands without compiling or running tests')
    parser.add_argument('--target-audit', action='store_true', help='Also check target EXE anchors/packet replay; requires pefile and capstone')
    parser.add_argument('--target-audit-python', help='Python executable for --target-audit, optionally Windows Python from WSL')
    args = parser.parse_args()
    if args.target_audit_python and not args.target_audit:
        parser.error('--target-audit-python requires --target-audit')
    if not sys.platform.startswith('linux'):
        parser.error('Run this checker from the prepared workspace in Linux/WSL')
    if not shutil.which(args.cc):
        parser.error(f'Compiler unavailable: {args.cc}')
    steps = build_steps(args.cc, args.target_audit, args.target_audit_python)
    if args.list:
        for step in steps:
            print(f'{step.name} [{step.kind}]: {shlex.join(step.command)}')
        return 0
    try:
        prepare_inputs()
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    report = execute(steps)
    if report['status'] != 'passed':
        print(f"Failed: {report.get('error')}; see {OUTPUT / 'report.json'}")
        return 1
    groups = next(step['groups'] for step in report['steps'] if step['name'] == 'storage-native')
    print(f"{len(report['steps'])} checks passed; storage {len(groups)} groups. SQL drivers compiled only. "
          f"Report: {OUTPUT.relative_to(PROJECT) / 'report.json'}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
