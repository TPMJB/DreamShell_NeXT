#!/usr/bin/env python3
"""Readable NeXT attribution metadata; no runtime enforcement or behavior hooks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROJECT = 'K-UI'
MAINTAINER = 'TPMJB'
REPOSITORY = 'https://github.com/TPMJB/DreamShell_NeXT'
NEXT_NOTICE = f'Required Notice: {PROJECT} enhancements and branding by {MAINTAINER} ({REPOSITORY}).'
UPSTREAM_NOTICES = (
    'Required Notice: Copyright (C) 2004-present Ruslan Rostovtsev (SWAT) (http://www.dc-swat.ru)',
    'Required Notice: Commercial licensing: contact Ruslan Rostovtsev (SWAT) via http://www.dc-swat.ru',
)
CORE_NAMES = ('DS_CORE.BIN', 'DEBUG_DS_CORE.BIN', 'EMU_DS_CORE.BIN')
RECORD = 'DS/doc/next-build.json'
BRANDING_FILES = ('resources/boot-disc-badge.mr', 'romdisk/logo.kmg.gz')


def git_identity(root):
    """Source archives remain buildable, with their revision explicitly unknown."""
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    try:
        if Path(git('rev-parse', '--show-toplevel')).resolve() != root.resolve():
            return 'unknown', None
        commit = git('rev-parse', 'HEAD')
        if not re.fullmatch(r'[a-f0-9]{40,64}', commit):
            return 'unknown', None
        modified = bool(git('status', '--porcelain', '--untracked-files=no'))
        return commit, modified
    except (OSError, subprocess.CalledProcessError):
        return 'unknown', None


def build_record(root=ROOT):
    commit, modified = git_identity(root)
    return {
        'schema': 1,
        'project': PROJECT,
        'maintainer': MAINTAINER,
        'repository': REPOSITORY,
        'version': (root / 'VERSION').read_text().strip(),
        'source_commit': commit,
        'tracked_changes': modified,
        'attribution': NEXT_NOTICE,
        'notice_sha256': hashlib.sha256((root / 'NOTICE').read_bytes()).hexdigest(),
    }


def encode_record(record):
    return (json.dumps(record, sort_keys=True, indent=2) + '\n').encode('utf-8')


def generate(root=ROOT):
    data = encode_record(build_record(root))
    target = root / 'romdisk/next-build.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
    return data


def validate_archive(archive, root, commit, version):
    """Explicit checks for the official K-UI packager, never called by the core."""
    try:
        baseline = dict(line.split('  ', 1)[::-1] for line in
                        (root / 'resources/next-branding.sha256').read_text().splitlines())
    except (OSError, ValueError) as error:
        raise ValueError('Official K-UI branding manifest is missing or malformed') from error
    if set(baseline) != set(BRANDING_FILES):
        raise ValueError('Official K-UI branding manifest must identify the boot badge and core logo')
    for name in BRANDING_FILES:
        try:
            digest = hashlib.sha256((root / name).read_bytes()).hexdigest()
        except OSError as error:
            raise ValueError(f'Official K-UI branding asset is missing: {name}') from error
        if digest != baseline[name]:
            raise ValueError(f'Official K-UI branding changed: {name}; review the artwork and its manifest')
    notice = (root / 'NOTICE').read_bytes()
    lines = notice.decode('utf-8').splitlines()
    for required in (*UPSTREAM_NOTICES, NEXT_NOTICE):
        if required not in lines:
            raise ValueError(f'Official K-UI release source is missing attribution: {required}')
    for name in ('NOTICE', 'LICENSE'):
        try:
            packaged = archive.read('DS/doc/' + name)
        except KeyError as error:
            raise ValueError(f'Official K-UI release is missing DS/doc/{name}') from error
        if packaged != (root / name).read_bytes():
            raise ValueError(f'Packaged {name} differs from source; refresh the build documentation')
    try:
        data = archive.read(RECORD)
    except KeyError as error:
        raise ValueError(f'Official K-UI release is missing {RECORD}; rebuild the core') from error
    try:
        record = json.loads(data)
    except (ValueError, UnicodeError) as error:
        raise ValueError('NeXT build attribution is not valid JSON') from error
    expected = {
        'schema': 1, 'project': PROJECT, 'maintainer': MAINTAINER,
        'repository': REPOSITORY, 'source_commit': commit, 'version': version,
        'attribution': NEXT_NOTICE,
        'notice_sha256': hashlib.sha256(notice).hexdigest(),
    }
    if not isinstance(record, dict):
        raise ValueError('NeXT build attribution must be a JSON object')
    for key, value in expected.items():
        if record.get(key) != value:
            raise ValueError(f'NeXT build attribution mismatch: {key}; rebuild from the intended source')
    if type(record.get('tracked_changes')) is not bool:
        raise ValueError('Official K-UI build attribution requires Git source status')
    for name in CORE_NAMES:
        path = 'DS/' + name
        if data not in archive.read(path):
            raise ValueError(f'{path} is missing matching K-UI build attribution; rebuild this core')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    generate(args.root)


if __name__ == '__main__':
    main()
