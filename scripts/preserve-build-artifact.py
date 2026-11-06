#!/usr/bin/env python3
"""Invalidate generated build files by preserving them, never deleting them."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sys

BUILD = Path('/mnt/d/漏洞/UNIXNU/src/UniXNU/out/yunluo/tree/build').resolve()
QUARANTINE = BUILD / '_quarantine' / 'k70u-xnu-20261104'

def preserve(path):
    path = Path(path)
    if not path.exists() and not path.is_symlink():
        return
    resolved = path.resolve()
    if BUILD not in resolved.parents or QUARANTINE in resolved.parents:
        raise SystemExit(f'Unsafe build invalidation path: {resolved}')
    relative = resolved.relative_to(BUILD)
    target = QUARANTINE / datetime.now().strftime('%Y%m%d-%H%M%S-%f') / relative
    target.parent.mkdir(parents=True, exist_ok=False)
    record = {'from': str(path.absolute()), 'resolved': str(resolved), 'to': str(target)}
    if path.is_file():
        record['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    shutil.move(str(path), target)
    with (QUARANTINE / 'moves.jsonl').open('a') as stream:
        stream.write(json.dumps(record) + '\n')

if __name__ == '__main__':
    for argument in sys.argv[1:]:
        if not argument.startswith('-'):
            preserve(argument)
