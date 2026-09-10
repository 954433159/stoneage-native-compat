"""Run focused native regressions against the actual prepared original sources."""
from pathlib import Path
import hashlib
import json
import subprocess

PROJECT = Path(__file__).resolve().parents[1]


def main():
    common = ['cc', '-ffunction-sections', '-fdata-sections', '-D_GNU_SOURCE',
              '-DLUA_COMPAT_MODULE', '-Wno-int-conversion', '-Wno-incompatible-pointer-types',
              '-Inative', '-Inative/include', '-Inative/overrides/gmsv', '-Inative/overrides/gmsv/include',
              '-Inative/overrides/gmsv/lua', '-I/usr/include/mariadb']
    passed = []
    # Compile the actual PETST receiver alone, allowing its native dependencies
    # to be replaced by stateful fixtures without copying its implementation.
    source = (PROJECT/'native/overrides/gmsv/callfromcli.c').read_text()
    begin = source.index('void lssproto_PETST_recv(')
    end = source.index('\n#ifdef _MIND_ICON', begin)
    guard = next(line for line in source.splitlines() if line.startswith('#define CHECKFDANDTIME'))
    (PROJECT/'build/pet_state_function.inc').write_text(guard + '\n' + source[begin:end])
    begin = source.index('void lssproto_NEWLOGOUT2_recv(')
    end = source.index('\n#endif', begin)
    (PROJECT/'build/app_logout_function.inc').write_text(source[begin:end])
    # Use the native unescaper when exercising SAAC's nested option record.
    util_source = (PROJECT/'native/overrides/gmsv/util.c').read_text()
    table_begin = util_source.index('typedef struct tagEscapeChar')
    table_end = util_source.index('char makeCharFromEscaped', table_begin)
    decode_begin = util_source.index('char* makeStringFromEscaped(')
    decode_end = util_source.index('\n#else', decode_begin)
    (PROJECT/'build/app_escape_function.inc').write_text(
        util_source[table_begin:table_end] + util_source[decode_begin:decode_end])
    talk_source = (PROJECT/'native/overrides/gmsv/char/char_talk.c').read_text()
    talk_begin = talk_source.index('static BOOL stone_app_team_body(')
    talk_end = talk_source.index('\n#ifdef _TELLCHANNEL', talk_begin)
    (PROJECT/'build/team_chat_function.inc').write_text(talk_source[talk_begin:talk_end])
    # Normalize after commands AND ':' channel switching. The body must not
    # start encounters or become a message to a configured family channel.
    call = talk_source.index('if (!stone_app_team_body(fd, index, messageeraseescape)) return;')
    assert call > talk_source.index('TalkAction(index, messageeraseescape);')
    route_begin = talk_source.index('    // Robin 0705 channel')
    route_end = talk_source.index('\n    {\n        typedef void(*TALKF)', route_begin)
    assert route_begin < call < route_end
    (PROJECT/'build/team_chat_route.inc').write_text(talk_source[route_begin:route_end])
    with (PROJECT/'evidence/native-regressions-build.txt').open('w') as build_log:
        for name in ('log_login', 'char_fields', 'wire_text', 'chat_packet', 'team_chat', 'inventory_fields', 'battle_fields', 'app_role', 'app_control', 'app_mail', 'pet_state'):
            command = common + ['-Werror=implicit-function-declaration' if name=='log_login'
                                else '-Wno-implicit-function-declaration', f'tests/{name}.c']
            if name == 'inventory_fields':
                # Match the production optimizer/fortify settings: overlapping
                # sprintf appends can appear to work in an unoptimized fixture.
                command.extend(['-O2', '-D_FORTIFY_SOURCE=2'])
            if name in ('wire_text', 'app_mail'):
                command.append('native/wire_charset.c')
            if name == 'app_control':
                # Exercise the real OffLine Lua implementation in the controller
                # fixture using the same interpreter objects as lua_reference.
                lua_objects = sorted((PROJECT/'build/CMakeFiles/lua_reference.dir/overrides/gmsv/lua').glob('*.c.o'))
                if not lua_objects:
                    raise RuntimeError('Build lua_reference before native controller checks')
                command.extend(str(path) for path in lua_objects if path.name != 'lua.c.o')
                command.extend(['-lmariadb', '-lm', '-ldl'])
            command += ['-Wl,--gc-sections', '-o', f'build/test_{name}']
            subprocess.run(command, cwd=PROJECT, check=True, stdout=build_log, stderr=subprocess.STDOUT, timeout=60)
            subprocess.run([str(PROJECT/f'build/test_{name}')], cwd=PROJECT, check=True, timeout=30)
            passed.append(name)
    for name in ('lua_compat', 'capture_init', 'login_platform', 'warp_optional', 'newplayer_window', 'encounter_menu', 'teleport_menu', 'quick_party_menu'):
        subprocess.run([str(PROJECT/'build/lua_reference'), f'tests/{name}.lua'], cwd=PROJECT, check=True, timeout=30)
        passed.append(name)
    subprocess.run(['python3', '-B', 'tests/test_npc_arg_path.py'], cwd=PROJECT, check=True, timeout=30)
    passed.append('npc_arg_path')
    subprocess.run(['python3', '-B', 'tests/test_native_greeting.py'], cwd=PROJECT, check=True, timeout=30)
    passed.append('native_greeting')
    subprocess.run(['python3', '-B', 'tests/test_status_initialization.py'], cwd=PROJECT, check=True, timeout=30)
    passed.append('status_initialization')
    for name in ('saac_save_buffers', 'saac_shutdown', 'gmsv_shutdown', 'loaded_pet_object', 'created_pet_object', 'pet_zero_fields', 'inventory_pages', 'dungeon_native_bridge', 'family_creation', 'family_metadata', 'family_list', 'trade_item_payload', 'trade_pet_payload'):
        subprocess.run(['python3', '-B', f'tests/test_{name}.py'], cwd=PROJECT, check=True, timeout=30)
        passed.append(name)
    result = {'passed': passed,
              'gmsv_sha256': hashlib.sha256((PROJECT/'build/gmsv').read_bytes()).hexdigest(),
              'saac_sha256': hashlib.sha256((PROJECT/'build/saac').read_bytes()).hexdigest()}
    (PROJECT/'evidence/native-regressions.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(passed)} native/source regressions passed.')


if __name__ == '__main__':
    main()
