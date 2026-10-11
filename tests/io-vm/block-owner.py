#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Run owner-side block mechanisms against disposable 512/4096-byte disks."""

import argparse
import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("archive", ROOT / "scripts/assemble-io-vm.py")
ARCHIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARCHIVE)


def regular(data, mode=0o644):
    return (0o100000 | mode, 0, 0, 0, 0, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--initramfs", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    entries = ARCHIVE.parse_newc(gzip.decompress(args.initramfs.read_bytes()))
    entries["usr/bin/block-owner-test"] = regular(args.binary.read_bytes(), 0o755)
    entries["etc/hyper-clients.conf"] = regular(
        b"hyper.clients.v3\n0 - -\n1 - -\n2 - -\n3 - -\n4 - -\n")
    entries["init"] = regular(b"""#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t configfs configfs /sys/kernel/config
: > /dev/hyper-io-notification-1
: > /dev/hyper-io-notification-2
: > /dev/hyper-io-notification-3
: > /dev/hyper-io-notification-4
/usr/bin/block-owner-test
status=$?
echo "BLOCK-OWNER-TEST-EXIT:$status"
poweroff -f
""", 0o755)
    with tempfile.TemporaryDirectory(prefix="hyper-block-owner-") as temporary:
        work = Path(temporary)
        initramfs = work / "initramfs.cpio.gz"
        initramfs.write_bytes(gzip.compress(ARCHIVE.encode_newc(entries, 32 << 20), mtime=0))
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
                   "-kernel", str(args.kernel.resolve()), "-initrd", str(initramfs),
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
        except subprocess.TimeoutExpired as error:
            # subprocess.run has killed and joined its QEMU child before returning.
            output = error.stdout or b""
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_bytes(output)
        if b"BLOCK-OWNER-TEST-EXIT:0" not in output:
            raise SystemExit(f"block-owner test failed; see {args.log}")
    print("Block owner: multiple disks, 4K geometry, bounded reads and range isolation passed")


if __name__ == "__main__":
    main()
