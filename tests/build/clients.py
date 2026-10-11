# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0
"""Exercise the client parser and block range rules without device access."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
class Clients(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / 'parser'
        source = Path(cls.temp.name) / 'clients.c'
        source.write_text('#include "hyper_clients.h"\n'
                          'int main(int argc, char **argv) { struct hyper_client clients[HYPER_BLOCK_CLIENTS]; '
                          'unsigned count; return argc != 2 || hyper_clients_read(argv[1], clients, &count); }\n')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(source), str(ROOT / 'service/client-config.c'),
                        '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()
    def test_client_authorization(self):
        path = Path(self.temp.name) / 'clients.conf'
        for content, good in [
            ('hyper.clients.v3\n0 - -\n1 - -\n', True),
            ('hyper.clients.v3\n0 - -\n', True),
            ('hyper.clients.v3\n0 - -\n1 default 02:48:59:00:00:01\n', True),
            ('hyper.clients.v3\n0 - -\n0 - -\n', False),
            ('hyper.clients.v3\n1 - -\n', False),
            ('hyper.clients.v3\n0 config - -\n', False),
            ('hyper.clients.v3\n0 default 02:48:59:00:00:01\n', False),
            ('hyper.clients.v3\n0 - -\n128 - -\n', False),
            ('hyper.clients.v3\n0 - -\n-1 - -\n', False),
            ('hyper.clients.v3\n0 - -\n1 ../x 02:48:59:00:00:01\n', False),
            ('hyper.clients.v3\n0 - -\n1 default 03:48:59:00:00:01\n', False),
            ('hyper.clients.v3\n0 - -\n1 - 02:48:59:00:00:01\n', False),
            ('hyper.clients.v3\n0 - -\n1 default 02:48:59:00:00:01\n2 default 02:48:59:00:00:01\n', False),
            ('hyper.clients.v3\n0 - -\n1 - -', False),
        ]:
            with self.subTest(content=content):
                path.write_text(content)
                self.assertEqual(subprocess.run([str(self.binary), str(path)]).returncode == 0, good)
    def test_block_ranges(self):
        binary = Path(self.temp.name) / 'block-policy'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(ROOT / 'tests/build/block-policy.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    def test_network_manifest(self):
        source = Path(self.temp.name) / 'networks.c'
        source.write_text('#include "hyper_network.h"\n'
                          'int main(int argc, char **argv) { struct hyper_network n[HYPER_MAX_NETWORKS]; '
                          'unsigned count; return argc != 2 || hyper_networks_read(argv[1], n, &count); }\n')
        binary = Path(self.temp.name) / 'networks'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(source), str(ROOT / 'service/network-config.c'),
                        '-o', str(binary)], check=True)
        manifest = Path(self.temp.name) / 'networks.conf'
        for content, good in [
            ('hyper.networks.v1\n', True),
            ('hyper.networks.v1\ndefault hbr0 eth0\n', True),
            ('hyper.networks.v1\ndefault hbr0 hbr0\n', False),
            ('hyper.networks.v1\ndefault ../x eth0\n', False),
            ('hyper.networks.v1\ndefault hbr0 eth0 extra\n', False),
            ('hyper.networks.v1\ndefault hbr0 eth0', False),
            ('hyper.networks.v1\ndefault hbr0 eth0\nother hbr0 eth1\n', False),
            ('hyper.networks.v1\ndefault hbr0 eth0\nother hbr1 eth0\n', False),
            ('hyper.networks.v1\ndefault hbr0 eth0\nother eth0 eth1\n', False),
        ]:
            with self.subTest(content=content):
                manifest.write_text(content)
                self.assertEqual(subprocess.run([str(binary), str(manifest)]).returncode == 0, good)

    def test_session_state_machine(self):
        binary = Path(self.temp.name) / 'session'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(ROOT / 'tests/build/session.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__': unittest.main()
