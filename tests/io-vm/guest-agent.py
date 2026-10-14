#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Qualify the real QGA daemon after late virtio-serial discovery and reconnect."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time


ALLOWED = {'guest-sync', 'guest-sync-delimited', 'guest-ping', 'guest-info',
           'guest-get-cpustats', 'guest-hyper-get-memory-stats'}


class Stream:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(15)
        self.socket.connect(str(path))
        self.pending = bytearray()
        self.sequence = 0

    def close(self):
        self.socket.close()

    def receive(self):
        while b'\n' not in self.pending:
            data = self.socket.recv(4096)
            if not data:
                raise RuntimeError('agent/QMP connection closed')
            self.pending.extend(data)
            if len(self.pending) > 65536:
                raise RuntimeError('agent/QMP reply too large')
        line, _, self.pending = self.pending.partition(b'\n')
        return json.loads(line.lstrip(b'\xff'))

    def rpc(self, name, arguments=None, *, sync=False, reject=False):
        self.sequence += 1
        message = {'execute': name, 'id': self.sequence}
        if sync:
            arguments = {'id': self.sequence}
        if arguments is not None:
            message['arguments'] = arguments
        self.socket.sendall((b'\xff' if sync else b'') + json.dumps(message).encode() + b'\n')
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            reply = self.receive()
            if reply.get('id') != self.sequence:
                continue
            if reject:
                if reply.get('error', {}).get('class') not in ('CommandNotFound', 'CommandDisabled'):
                    raise RuntimeError(f'unsafe RPC was not rejected: {reply}')
                return
            if 'error' in reply:
                raise RuntimeError(reply['error'])
            result = reply['return']
            if sync and result != self.sequence:
                raise RuntimeError('synchronization identity mismatch')
            return result
        raise TimeoutError('agent/QMP response deadline')


def sample(agent):
    agent.rpc('guest-sync-delimited', sync=True)
    info = agent.rpc('guest-info')
    enabled = {item['name'] for item in info['supported_commands'] if item['enabled']}
    if enabled != ALLOWED:
        raise RuntimeError(f'unexpected RPC policy: {enabled}')
    memory = agent.rpc('guest-hyper-get-memory-stats')
    total = memory['total-bytes']
    if not 0 < total <= 128 * 1024 * 1024 or not 0 < memory['available-bytes'] <= total:
        raise RuntimeError(f'invalid guest RAM accounting: {memory}')
    for field in ('free-bytes', 'buffers-bytes', 'cached-bytes', 'reclaimable-slab-bytes'):
        if not 0 <= memory[field] <= total:
            raise RuntimeError(f'invalid guest RAM field: {memory}')
    before = agent.rpc('guest-get-cpustats')
    time.sleep(1)
    after = agent.rpc('guest-get-cpustats')
    if len(before) != 2 or len(after) != 2:
        raise RuntimeError('agent did not report both guest CPUs')
    for old, new in zip(before, after):
        if old['type'] != 'linux' or old['cpu'] != new['cpu']:
            raise RuntimeError('unexpected CPU identity')
        if new['idle'] + new['user'] + new['system'] <= old['idle'] + old['user'] + old['system']:
            raise RuntimeError('CPU counters did not advance')
    for name, arguments in (
            ('guest-exec', {'path': '/bin/true'}),
            ('guest-file-open', {'path': '/etc/hyper-io-version', 'mode': 'w'}),
            ('guest-shutdown', {})):
        agent.rpc(name, arguments, reject=True)
    agent.rpc('guest-ping')
    return {'version': info['version'], 'memory': memory, 'cpus': after}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-index', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    args = parser.parse_args()
    boot = Path(json.loads(args.boot_index.read_text())['directory'])
    manifest = json.loads((boot / 'boot-artifacts.json').read_text())
    args.log.parent.mkdir(parents=True, exist_ok=True)
    # Short paths also fit AF_UNIX's small sockaddr on macOS.
    with tempfile.TemporaryDirectory(prefix='hyper-qga-') as temporary, args.log.open('wb') as log:
        directory = Path(temporary)
        command = [args.qemu, '-machine', 'virt,gic-version=3', '-accel', 'tcg',
                   '-cpu', 'cortex-a76', '-smp', '2', '-m', '128', '-nodefaults',
                   '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
                   '-qmp', f'unix:{directory}/qmp,server=on,wait=off',
                   '-device', 'pcie-root-port,id=agent-root,chassis=1',
                   '-chardev', f'socket,id=agent,path={directory}/agent,server=on,wait=off',
                   '-kernel', str(boot / manifest['kernel']),
                   '-initrd', str(boot / manifest['initramfs']),
                   '-append', 'console=ttyAMA0 rdinit=/init panic=-1 hyper.role=io hyper.mode=bringup']
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        qmp = None
        agent = None
        try:
            deadline = time.monotonic() + 90
            while b'HypeR I/O: bring-up ready;' not in args.log.read_bytes():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f'agent qualification boot failed: {args.log}')
                time.sleep(0.1)
            qmp = Stream(directory / 'qmp')
            qmp.receive()
            qmp.rpc('qmp_capabilities')
            # No serial controller exists until after Linux userspace is ready.
            qmp.rpc('device_add', {'driver': 'virtio-serial-pci', 'id': 'agent-serial',
                                  'bus': 'agent-root'})
            qmp.rpc('device_add', {'driver': 'virtserialport', 'id': 'agent-port',
                                  'bus': 'agent-serial.0', 'nr': 1, 'chardev': 'agent',
                                  'name': 'org.qemu.guest_agent.0'})
            evidence = []
            for _ in range(2):
                agent = Stream(directory / 'agent')
                evidence.append(sample(agent))
                agent.close()
                agent = None
                time.sleep(2)
            args.log.with_suffix('.json').write_text(json.dumps(evidence, indent=2) + '\n')
            print('QGA late port discovery, CPU/RAM sampling, RPC restriction and reconnect: PASS')
        finally:
            if agent is not None:
                agent.close()
            if qmp is not None:
                qmp.close()
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    main()
