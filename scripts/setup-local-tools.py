#!/usr/bin/env python3
"""Prepare project-private AAPT2 tools; all compilation stays on the phone."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / ".local-build/tools"
# Native Termux QEMU runs Google's x86 AAPT2 locally. These packages are only
# extracted into this checkout, never installed over the shared Termux tools.
PACKAGES = [
    ("https://termux.net/debs/stable/aarch64/q/qemu-user-x86-64/qemu-user-x86-64_1%3a10.2.1_aarch64.deb",
     "30896ad808ede983622d467fd3a0925c9443cf57bf054f387d8d5464ebbd4bbc", "qemu"),
    ("https://termux.net/debs/stable/aarch64/l/libdw/libdw_0.193-1_aarch64.deb",
     "8b6e1b4efec1813b7ab4c20178ee7d34af755cffb5de9856828c20be48c07b41", "qemu"),
    ("https://termux.net/debs/stable/aarch64/l/libelf/libelf_0.193-1_aarch64.deb",
     "c369cafa0c5acf1640bb4bdcd924cd763e08287e1b03fab4a011dfb13e0caf70", "qemu"),
    ("https://termux.net/debs/stable/aarch64/a/argp/argp_1.5.0-1_aarch64.deb",
     "6936e11d171d549c00c684dc18a2134fde6a8c8a0db5603655f69a94e8f9552f", "qemu"),
    ("https://deb.debian.org/debian/pool/main/g/glibc/libc6_2.36-9+deb12u14_amd64.deb",
     "ba4f88f73dbc3ae9055f3c20f4523bfdbaf1ad13ff95e258924f77d20b4fbedf", "glibc"),
    ("https://deb.debian.org/debian/pool/main/g/gcc-12/libgcc-s1_12.2.0-14+deb12u1_amd64.deb",
     "3016e62cb4b7cd8038822870601f5ed131befe942774d0f745622cc77d8a88f7", "glibc"),
]


def prepare(sdk):
    sdk = Path(sdk)
    aapt = sdk / "build-tools/36.0.0/aapt2"
    if not aapt.is_file():
        raise SystemExit('Install SDK build-tools first: sdkmanager "build-tools;36.0.0"')
    if not shutil.which("dpkg-deb"):
        raise SystemExit("Missing Termux tool: dpkg-deb")
    downloads = TOOLS / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    for url, digest, destination in PACKAGES:
        archive = downloads / url.rsplit("/", 1)[1]
        if not archive.is_file():
            print("Downloading local tool:", archive.name, flush=True)
            pending = archive.with_suffix(".part")
            with urllib.request.urlopen(url, timeout=90) as source, pending.open("wb") as target:
                shutil.copyfileobj(source, target)
            if hashlib.sha256(pending.read_bytes()).hexdigest() != digest:
                raise SystemExit("Checksum mismatch: " + str(pending))
            pending.replace(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise SystemExit("Checksum mismatch: " + str(archive))
        marker = TOOLS / destination / ("." + archive.name + ".sha256")
        if not marker.is_file() or marker.read_text().strip() != digest:
            subprocess.run(["dpkg-deb", "-x", str(archive), str(TOOLS / destination)], check=True)
            marker.write_text(digest + "\n")
    loader = TOOLS / "glibc/lib64/ld-linux-x86-64.so.2"
    if loader.is_symlink() and os.readlink(loader).startswith("/"):
        loader.unlink()
        loader.symlink_to("../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2")
    wrapper = TOOLS / "aapt2"
    prefix = os.environ.get("PREFIX", "/data/data/com.termux/files/usr")
    # QEMU's host libraries and the guest GLIBC have separate search paths.
    wrapper.write_text(
        "#!" + sys.executable + "\n"
        "import os, sys\n"
        "from pathlib import Path\n"
        "tools = Path(__file__).resolve().parent\n"
        "prefix = " + repr(prefix) + "\n"
        "qemu = tools / 'qemu' / prefix.lstrip('/') / 'bin/qemu-x86_64'\n"
        "env = dict(os.environ)\n"
        "env['LD_LIBRARY_PATH'] = str(tools / 'qemu' / prefix.lstrip('/') / 'lib') + ':' + prefix + '/lib'\n"
        "args = [str(qemu), '-L', str(tools / 'glibc'), '-U', 'LD_PRELOAD',\n"
        "        '-E', 'LD_LIBRARY_PATH=/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu',\n"
        "        " + repr(str(aapt)) + ", *sys.argv[1:]]\n"
        "os.execve(str(qemu), args, env)\n"
    )
    wrapper.chmod(0o755)
    subprocess.run([str(wrapper), "version"], check=True)
    return wrapper


if __name__ == "__main__":
    print(prepare(os.environ.get("ANDROID_HOME", str(Path.home() / "android-sdk"))))
