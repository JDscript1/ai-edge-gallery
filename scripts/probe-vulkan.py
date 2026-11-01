#!/usr/bin/env python3
"""Query GPU memory through the Android system Vulkan driver, without inference."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get("ANDROID_HOME", str(Path.home() / "android-sdk")))
ndk = sdk / "ndk/27.1.12297006/toolchains/llvm/prebuilt/linux-x86_64"
sysroot = ndk / "sysroot"
directory = ROOT / ".local-build"
directory.mkdir(exist_ok=True)
binary = directory / "vulkan-memory-probe"
subprocess.run([
    "clang", "--target=aarch64-linux-android31", "--sysroot=" + str(sysroot),
    "-fno-termux-rpath", "-L" + str(sysroot / "usr/lib/aarch64-linux-android/31"),
    "-L" + str(ndk / "lib/clang/18/lib/linux/aarch64"),
    str(ROOT / "scripts/vulkan-memory-probe.c"), "-lvulkan", "-ldl", "-lm", "-o", str(binary),
], check=True)
env = dict(os.environ)
env.pop("LD_PRELOAD", None)
env.pop("LD_LIBRARY_PATH", None)
report = json.loads(subprocess.check_output([str(binary)], env=env, text=True))
report["device"] = {
    key: subprocess.check_output(["getprop", key], text=True).strip()
    for key in ("ro.product.model", "ro.soc.model", "ro.build.version.sdk")
}
output = directory / "android-vulkan-memory.json"
output.write_text(json.dumps(report, indent=2) + "\n")
print(output.read_text())
