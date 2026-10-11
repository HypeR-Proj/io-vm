#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0
"""Boot the diskless appliance and check UART/virtio userspace log routing."""
import argparse
import json
from pathlib import Path
import subprocess
import time


READY = 'HypeR I/O: bring-up ready; no devices assigned, storage service disabled'
VERSION = 'HypeR I/O VM version '
KERNEL = 'Booting Linux on physical CPU'


def ready(console, uart, terminal):
    output = terminal if console == 'virtio' else uart
    if 'Kernel panic' in uart or 'Kernel panic' in terminal:
        raise RuntimeError('bring-up kernel panic')
    if console == 'virtio':
        if READY in uart or VERSION in uart:
            raise RuntimeError('userspace output escaped to the diagnostic UART')
        if KERNEL in terminal:
            raise RuntimeError('kernel output escaped to the virtio terminal')
    if READY not in output:
        return False
    if 'backend service ready' in uart or 'backend service ready' in terminal:
        raise RuntimeError('diskless bring-up advertised a storage service')
    if VERSION not in output or KERNEL not in uart:
        raise RuntimeError('bring-up is missing its version or kernel boot evidence')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-index', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--console', choices=('uart', 'virtio'), default='uart')
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    args = parser.parse_args()
    boot = Path(json.loads(args.boot_index.read_text())['directory'])
    manifest = json.loads((boot / 'boot-artifacts.json').read_text())
    args.log.parent.mkdir(parents=True, exist_ok=True)
    uart_log = (args.log.with_suffix('.uart.log') if args.console == 'virtio' else args.log)
    command = [
        args.qemu, '-machine', 'virt,gic-version=2', '-accel', 'tcg',
        '-cpu', 'cortex-a76', '-smp', '2', '-m', '128',
        '-nographic', '-no-reboot', '-nic', 'none',
        '-kernel', str(boot / manifest['kernel']),
        '-initrd', str(boot / manifest['initramfs']),
        '-append', 'console=ttyAMA0 rdinit=/init panic=-1 hyper.role=io hyper.mode=bringup'
        + f' hyper.console={args.console}']
    if args.console == 'virtio':
        # File chardev is independent of -nographic's UART. An accidental UART
        # fallback must not pass merely because both streams share stdout.
        args.log.write_bytes(b'')
        command += ['-device', 'virtio-serial-device',
                    '-chardev', f'file,id=terminal,path={args.log}',
                    '-device', 'virtconsole,chardev=terminal']
    with uart_log.open('wb') as log:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                uart = uart_log.read_text(errors='replace')
                terminal = args.log.read_text(errors='replace') if args.console == 'virtio' else ''
                if process.poll() is not None:
                    raise RuntimeError(f'bring-up terminated: {args.log}; UART: {uart_log}')
                if ready(args.console, uart, terminal):
                    print(f'Diskless appliance GICv2 {args.console} console: PASS')
                    return
                time.sleep(0.1)
            raise RuntimeError(f'bring-up timed out: {args.log}; UART: {uart_log}')
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

if __name__ == '__main__':
    main()
