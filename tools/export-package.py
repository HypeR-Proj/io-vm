#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Export verified boot files and their corresponding build materials."""

import argparse
import json
from pathlib import Path
import shutil

from publish import prepare


def export(build, platform, output):
    boot = Path(json.loads((build / f'{platform}-boot.json').read_text())['directory'])
    base = Path(json.loads((build / f'{platform}-base.json').read_text())['directory'])
    sources = list((build / 'source-artifacts').glob('*.tar.xz'))
    if len(sources) != 1:
        raise ValueError('export requires one exact source generation; use a fresh build output')
    metadata = json.loads((base / 'manifest.json').read_text())
    shutil.copytree(boot, output)
    materials = output / 'materials'
    materials.mkdir()
    verified = prepare(boot, base, sources[0], materials, metadata['source_revision'])
    for name in ('Image', 'initramfs.cpio.gz'):
        (materials / name).unlink()
    (materials / 'manifest.json').write_text(json.dumps(verified, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output', type=Path, default=Path('target/io-vm'))
    parser.add_argument('--platform', choices=['qemu', 'rpi5'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    export(args.build_output, args.platform, args.output)
