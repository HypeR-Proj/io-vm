#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Run owner-side block mechanisms against disposable 512/4096-byte disks."""

import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("archive", ROOT / "scripts/assemble-io-vm.py")
ARCHIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARCHIVE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--test-binary", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hyper-block-owner-") as temporary:
        work = Path(temporary)
        overlay = work / "overlay"
        (overlay / "usr/bin").mkdir(parents=True)
        (overlay / "etc").mkdir()
        binary = overlay / "usr/bin/block-owner-test"
        shutil.copyfile(args.test_binary, binary)
        binary.chmod(0o755)
        (overlay / "etc/hyper-clients.conf").write_text(
            "hyper.clients.v3\n0 - -\n1 - -\n2 - -\n3 - -\n4 - -\n")
        (overlay / "init").write_text("""#!/bin/sh
set -eu
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t configfs configfs /sys/kernel/config
: > /dev/hyper-io-notification-1
: > /dev/hyper-io-notification-2
: > /dev/hyper-io-notification-3
: > /dev/hyper-io-notification-4
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if [ -b /dev/sda ] && [ -b /dev/sdb ]; then break; fi
    sleep 1
done
status=0
/usr/bin/block-owner-test || status=$?
echo "BLOCK-OWNER-TEST-EXIT:$status"
poweroff -f
""")
        (overlay / "init").chmod(0o755)
        boot = ARCHIVE.assemble(argparse.Namespace(
            base=args.base, manifest_sha256=args.manifest_sha256, overlay=overlay,
            output=work / "boot", architecture="aarch64", kernel_limit=32 * ARCHIVE.MIB,
            compressed_limit=8 * ARCHIVE.MIB, expanded_limit=32 * ARCHIVE.MIB))
        for name, length, records in (
                ("first", 16 << 20, ((0, b"owner-disk"), (64 * 512, b"range-one"),
                                    (192 * 512, b"range-two"))),
                ("second", 32 << 20, ((4 * 4096, b"second-disk-range"),))):
            with (work / f"{name}.img").open("wb") as disk:
                disk.truncate(length)
                for offset, data in records:
                    disk.seek(offset)
                    disk.write(data)
        command = [args.qemu, "-machine", "virt,gic-version=3", "-cpu", "cortex-a72",
                   "-m", "256", "-nographic", "-no-reboot", "-nic", "none",
                   "-kernel", str(work / "boot" / boot["kernel"]),
                   "-initrd", str(work / "boot" / boot["initramfs"]),
                   "-append", "console=ttyAMA0 rdinit=/init panic=-1",
                   "-device", "virtio-scsi-device,id=scsi"]
        for index, (name, sector) in enumerate((("first", 512), ("second", 4096))):
            command += ["-drive", f"if=none,id={name},file={work}/{name}.img,format=raw",
                        "-device", f"scsi-hd,drive={name},bus=scsi.0,scsi-id={index},"
                        f"logical_block_size={sector},physical_block_size={sector}"]
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=60, check=False)
            output = result.stdout
            returncode = result.returncode
        except subprocess.TimeoutExpired as error:
            # subprocess.run has killed and joined its QEMU child before returning.
            output = error.stdout or b""
            returncode = 1
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_bytes(output)
        if returncode or b"Kernel panic" in output or b"BLOCK-OWNER-TEST-EXIT:0" not in output:
            raise SystemExit(f"block-owner test failed; see {args.log}")
    print("Block owner: multiple disks, 4K geometry, bounded reads and range isolation passed")


if __name__ == "__main__":
    main()
