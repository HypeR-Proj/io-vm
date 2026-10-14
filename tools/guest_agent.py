# SPDX-FileCopyrightText: 2026 roolrz
# SPDX-License-Identifier: Apache-2.0

"""Build the standalone QEMU guest agent against a private static dependency set."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


COMPONENTS = ("libffi", "pcre2", "zlib", "glib", "qemu")


def build(sources, output, cross, jobs, run):
    identity = hashlib.sha256(json.dumps({
        "sources": {name: (sources[name] / ".hyper-source-sha256").read_text()
                    for name in COMPONENTS if name != "qemu"},
        "wheels": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in sorted((sources["qemu"] / "python/wheels").glob("*.whl"))},
        "compiler": subprocess.check_output([cross + "gcc", "--version"], text=True),
        "cross": cross,
        "recipe": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }, sort_keys=True).encode()).hexdigest()
    root = output / "build" / "guest-agent" / identity
    qemu_identity = (sources["qemu"] / ".hyper-source-sha256").read_text().strip()
    qemu = root / ("qemu-" + qemu_identity)
    binary = qemu / "qga/qemu-ga"
    stamp = qemu / "complete"
    if stamp.is_file() and binary.is_file():
        return binary
    root.mkdir(parents=True, exist_ok=True)
    prefix = root / "sysroot"
    prefix.mkdir(exist_ok=True)
    env = dict(os.environ, CC=cross + "gcc", AR=cross + "ar", RANLIB=cross + "ranlib",
               CFLAGS="-O2 -g0", PKG_CONFIG_LIBDIR=str(prefix / "lib/pkgconfig"),
               PKG_CONFIG_PATH="", LC_ALL="C")
    host = subprocess.check_output([cross + "gcc", "-dumpmachine"], text=True).strip()
    if host not in ("aarch64-linux-gnu", "aarch64-unknown-linux-gnu"):
        raise ValueError("guest agent requires an AArch64 Linux compiler")
    for name, options in (
            ("libffi", ["--disable-shared", "--enable-static", "--disable-docs"]),
            ("pcre2", ["--disable-shared", "--enable-static", "--disable-pcre2grep",
                       "--disable-pcre2test", "--disable-docs"]),
            ("zlib", ["--static"])):
        directory = root / name
        directory.mkdir(exist_ok=True)
        if not (directory / "Makefile").is_file():
            command = [str(sources[name] / "configure"), "--prefix=" + str(prefix)]
            if name != "zlib":
                command += ["--host=" + host]
            run(command + options, cwd=directory, env=env)
        run(["make", "-j" + str(jobs)], cwd=directory, env=env)
        run(["make", "install"], cwd=directory, env=env)

    # Use the exact Meson/pycotap wheels already shipped in the pinned QEMU
    # archive. No unpinned PyPI download or host-wide Python installation.
    python_env = root / "python"
    if not (python_env / "bin/python").is_file():
        run([sys.executable, "-m", "venv", str(python_env)])
    run([str(python_env / "bin/pip"), "install", "--no-index", "--find-links",
         str(sources["qemu"] / "python/wheels"), "meson==1.10.0", "pycotap==1.3.1"])
    env["PATH"] = str(python_env / "bin") + os.pathsep + env["PATH"]
    cross_file = root / "aarch64.ini"
    cross_file.write_text(
        "[binaries]\n" +
        f"c = '{cross}gcc'\nar = '{cross}ar'\nstrip = '{cross}strip'\npkg-config = 'pkg-config'\n" +
        "[host_machine]\nsystem = 'linux'\ncpu_family = 'aarch64'\ncpu = 'aarch64'\nendian = 'little'\n" +
        "[properties]\nneeds_exe_wrapper = true\n")
    glib = root / "glib"
    if not (glib / "build.ninja").exists():
        run([str(python_env / "bin/meson"), "setup", str(glib), str(sources["glib"]),
             "--cross-file=" + str(cross_file), "--prefix=" + str(prefix), "--libdir=lib",
             "--default-library=static", "--buildtype=release", "--wrap-mode=nodownload",
             "-Dtests=false", "-Dinstalled_tests=false", "-Dintrospection=disabled",
             "-Dnls=disabled", "-Dlibmount=disabled", "-Dselinux=disabled", "-Dlibelf=disabled",
             "-Dsysprof=disabled", "-Ddocumentation=false", "-Dman-pages=disabled"], env=env)
    run(["ninja", "-C", str(glib), "-j", str(jobs)], env=env)
    run(["ninja", "-C", str(glib), "install"], env=env)
    qemu.mkdir(exist_ok=True)
    if not (qemu / "build.ninja").exists():
        run([str(sources["qemu"] / "configure"), "--target-list=", "--without-default-features",
             "--disable-system", "--disable-user", "--disable-tools", "--disable-docs",
             "--enable-guest-agent", "--static", "--disable-werror",
             "--python=" + str(python_env / "bin/python"), "--cross-prefix=" + cross],
            cwd=qemu, env=env)
    run(["ninja", "-C", str(qemu), "-j", str(jobs), "qga/qemu-ga"], env=env)
    headers = subprocess.check_output([cross + "readelf", "-l", "-d", str(binary)], text=True)
    if "INTERP" in headers or "(NEEDED)" in headers:
        raise ValueError("qemu-ga must be a self-contained static executable")
    stamp.write_text(identity + "\n")
    return binary
