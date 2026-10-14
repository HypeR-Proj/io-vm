#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Execute appliance init against private console fixtures without booting Linux."""

import importlib.util
import os
from pathlib import Path
import pty
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('bringup', ROOT / 'tests/io-vm/bringup.py')
BRINGUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRINGUP)
VERSION = 'HypeR I/O VM version test (qemu; built 1970-01-01T00:00:00Z)\n'


class ConsoleInitTests(unittest.TestCase):
    def run_init(self, option='', *, virtio=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'cmdline').write_text('hyper.role=io hyper.mode=bringup ' + option + '\n')
            (root / 'version').write_text(VERSION)
            (root / 'agent').write_text('#!/bin/sh\nexit 0\n')
            (root / 'agent').chmod(0o755)
            calls = root / 'calls'
            master, slave = pty.openpty()
            try:
                if virtio:
                    (root / 'hvc0').symlink_to(os.ttyname(slave))
                script = (ROOT / 'rootfs/init').read_text()
                for original, fixture in (
                        ('/proc/cmdline', 'cmdline'),
                        ('/etc/hyper-io-version', 'version'),
                        ('/etc/hyper-clients.conf', 'clients'),
                        ('/dev/hvc0', 'hvc0'),
                        ('/usr/bin/hyper-guest-agent', 'agent')):
                    script = script.replace(original, str(root / fixture))
                # Init's real parsing, tty redirection, banner and bring-up
                # path run unchanged; only privileged commands are replaced.
                commands = (
                    'mount() { :; }\n'
                    f'poweroff() {{ echo poweroff >> {shlex.quote(str(calls))}; }}\n'
                    'uname() { echo TEST-STDOUT; echo TEST-STDERR >&2; }\n'
                    f'sleep() {{ echo "sleep $1" >> {shlex.quote(str(calls))}; '
                    'if [ "$1" = 3600 ]; then exit 77; fi; }\n')
                result = subprocess.run(['/bin/sh', '-c', commands + script],
                                        stdin=subprocess.DEVNULL, text=True,
                                        capture_output=True, timeout=5)
                os.set_blocking(master, False)
                output = bytearray()
                while True:
                    try:
                        output.extend(os.read(master, 4096))
                    except BlockingIOError:
                        break
                return result, output.decode(), calls.read_text() if calls.exists() else ''
            finally:
                os.close(master)
                os.close(slave)

    def test_default_and_explicit_uart_keep_existing_stdio(self):
        for option in ('', 'hyper.console=uart'):
            with self.subTest(option=option):
                result, terminal, calls = self.run_init(option)
                self.assertEqual(result.returncode, 77, result.stderr)
                self.assertIn(VERSION, result.stdout)
                self.assertIn(BRINGUP.READY, result.stdout)
                self.assertIn('TEST-STDERR', result.stderr)
                self.assertEqual(terminal, '')
                self.assertEqual(calls, 'sleep 3600\n')

    def test_virtio_redirects_banner_and_service_stdio_before_bringup(self):
        result, terminal, calls = self.run_init('hyper.console=virtio', virtio=True)
        self.assertEqual(result.returncode, 77, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, '')
        self.assertIn(VERSION.rstrip(), terminal)
        self.assertIn('TEST-STDOUT', terminal)
        self.assertIn('TEST-STDERR', terminal)
        self.assertIn(BRINGUP.READY, terminal)
        self.assertEqual(calls, 'sleep 3600\n')

    def test_missing_virtio_device_fails_after_bounded_wait_without_fallback(self):
        result, terminal, calls = self.run_init('hyper.console=virtio')
        self.assertEqual(result.returncode, 1)
        self.assertIn('requested virtio console is unavailable', result.stderr)
        self.assertNotIn(VERSION, result.stdout)
        self.assertNotIn(BRINGUP.READY, result.stdout)
        self.assertEqual(terminal, '')
        self.assertEqual(calls, 'sleep 1\n' * 10 + 'poweroff\n')

    def test_unknown_transport_is_rejected_before_starting_services(self):
        result, terminal, calls = self.run_init('hyper.console=other', virtio=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('invalid hyper.console', result.stderr)
        self.assertNotIn(BRINGUP.READY, terminal)
        self.assertEqual(calls, 'poweroff\n')


class BringupOutputTests(unittest.TestCase):
    def test_success_requires_kernel_and_userspace_on_their_selected_channels(self):
        kernel = BRINGUP.KERNEL + '\n'
        userspace = VERSION + BRINGUP.READY + '\n'
        self.assertTrue(BRINGUP.ready('uart', kernel + userspace, ''))
        self.assertTrue(BRINGUP.ready('virtio', kernel, userspace))
        self.assertFalse(BRINGUP.ready('virtio', kernel, VERSION))
        for uart, terminal in (
                (kernel + userspace, ''),
                (kernel, kernel + userspace),
                ('', userspace),
                (kernel, BRINGUP.READY),
                (kernel + 'Kernel panic', userspace),
                (kernel, userspace + 'backend service ready')):
            with self.subTest(uart=uart, terminal=terminal), self.assertRaises(RuntimeError):
                BRINGUP.ready('virtio', uart, terminal)


if __name__ == '__main__':
    unittest.main()
