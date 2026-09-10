"""Explicit local inputs for a portable checkout; source hashes are still checked."""
import json
import os
from pathlib import Path, PureWindowsPath
import re

PROJECT=Path(__file__).resolve().parents[1]
_configuration=PROJECT/'runtime/inputs.json'
_inputs=json.loads(_configuration.read_text(encoding='utf8')) if _configuration.exists() else {}


def host_path(value):
    text=str(value)
    if os.name=='nt':
        match=re.fullmatch(r'/mnt/([a-zA-Z])(?:/(.*))?',text)
        if match:return Path(match[1].upper()+':/'+(match[2] or ''))
    else:
        windows=PureWindowsPath(text)
        if windows.is_absolute() and re.fullmatch(r'[a-zA-Z]:',windows.drive):
            return Path('/mnt')/windows.drive[0].lower()/Path(*windows.parts[1:])
    return Path(value)


REFERENCE_SOURCE=host_path(os.environ.get('STONEAGE_REFERENCE_SOURCE') or
    _inputs.get('reference_source') or PROJECT.parent/'reference-source')
ORIGINAL_CLIENT=host_path(os.environ.get('STONEAGE_ORIGINAL_CLIENT') or
    _inputs.get('original_client') or PROJECT.parent/'original-client')
