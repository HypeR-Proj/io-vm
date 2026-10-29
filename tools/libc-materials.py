#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Collect sources for the installed Ubuntu AArch64 cross libc."""

import argparse
from email.parser import Parser
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.request

ARCHIVE = 'https://archive.ubuntu.com/ubuntu/pool/main/'


def download(url, destination, checksum=None, size=None):
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if checksum is not None and (digest != checksum or (size is not None and len(data) != size)):
        raise ValueError(f'source checksum mismatch: {url}')
    destination.write_bytes(data)
    return {'name': destination.name, 'url': url, 'sha256': digest, 'bytes': len(data)}


def source_files(descriptor):
    if descriptor.startswith('-----BEGIN PGP SIGNED MESSAGE-----'):
        descriptor = descriptor.split('\n\n', 1)[1].split('-----BEGIN PGP SIGNATURE-----', 1)[0]
    fields = Parser().parsestr(descriptor)
    entries = []
    for line in fields.get('Checksums-Sha256', '').splitlines():
        if not line.strip():
            continue
        checksum, size, name = line.split()
        if not re.fullmatch(r'[0-9a-f]{64}', checksum) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+~-]*', name):
            raise ValueError('invalid source descriptor entry')
        entries.append((name, checksum, int(size)))
    if not entries:
        raise ValueError('source descriptor has no SHA256 file list')
    return entries


def collect(output):
    packages = {}
    for package in ('libc6-arm64-cross', 'libc6-dev-arm64-cross'):
        fields = subprocess.check_output(['dpkg-query', '-W',
                  '-f=${Version}\t${source:Package}\t${source:Version}', package], text=True).split('\t')
        version, source, source_version = fields
        if source != 'cross-toolchain-base' or not re.fullmatch(r'[0-9A-Za-z.+~-]+', version + source_version):
            raise ValueError('unsupported cross libc packaging; record its actual sources before publication')
        packages[package] = {'version': version, 'source': source, 'source_version': source_version}
    development = packages['libc6-dev-arm64-cross']
    glibc = re.sub(r'cross[0-9]+$', '', development['version'])
    if glibc == development['version']:
        raise ValueError('cannot identify the upstream glibc source version')
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for name, version, directory in (
            ('glibc', glibc, 'g/glibc/'),
            ('cross-toolchain-base', development['source_version'], 'c/cross-toolchain-base/')):
        filename = f'{name}_{version}.dsc'
        descriptor = output / filename
        files.append(download(ARCHIVE + directory + filename, descriptor))
        for filename, checksum, size in source_files(descriptor.read_text()):
            files.append(download(ARCHIVE + directory + filename, output / filename, checksum, size))
    for name, record in packages.items():
        filename = f"{name}_{record['version']}_all.deb"
        files.append(download(ARCHIVE + 'c/cross-toolchain-base/' + filename, output / filename))
    notices = json.loads(Path(__file__).with_name('runtime-notices.json').read_text())
    for record in notices['files']:
        files.append(download(record['url'], output / record['name'], record['sha256']))
    manifest = {'version': 1, 'packages': packages, 'glibc': glibc, 'files': files}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    collect(parser.parse_args().output)
