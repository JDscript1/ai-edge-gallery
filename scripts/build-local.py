#!/usr/bin/env python3
"""Build the Android APK locally in Termux, including its ARM64 GGUF runtime."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import re

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def deliver(apk):
    run(["apksigner", "verify", str(apk)])
    destination = Path("/storage/emulated/0/Download/Local-Edge-Gallery-Bonsai.apk")
    destination.parent.mkdir(parents=True, exist_ok=True)
    digest = sha256(apk)
    staging = destination.with_suffix(".apk.partial")
    try:
        shutil.copyfile(apk, staging)
        if sha256(staging) != digest:
            raise SystemExit("APK copy checksum mismatch")
        staging.replace(destination)
        # The rename can keep the previous installer's timestamp on this FUSE
        # mount, making file managers show a stale date. Refresh it explicitly.
        os.utime(destination, None)
    finally:
        staging.unlink(missing_ok=True)
    # Only installers and diagnostic APK copies created by this project.
    for old in destination.parent.glob("Local-Edge-Gallery-Bonsai-v*.apk"):
        if re.fullmatch(r"Local-Edge-Gallery-Bonsai-v\d+(?:-[0-9a-f]{8})?\.apk", old.name):
            old.unlink()
    for name in ("local.1.apk", "local.2.apk", "local.3.apk"):
        (ROOT / ".local-build/art-probe" / name).unlink(missing_ok=True)
    apk.with_suffix(".apk.sha256").write_text(digest + "  " + apk.name + "\n")
    (ROOT / ".local-build/apk-report.json").write_text(json.dumps({
        "apk": str(apk), "download_copy": str(destination), "sha256": digest,
        "size_bytes": apk.stat().st_size, "signature_verified": True,
        "ui_verified": False,
    }, indent=2) + "\n")
    print("Download:", destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--tests", action="store_true")
    parser.add_argument("--info", action="store_true", help="Show Gradle dependency and task details")
    args = parser.parse_args()
    prefix = Path(os.environ.get("PREFIX", "/data/data/com.termux/files/usr"))
    sdk = Path(os.environ.get("ANDROID_HOME", str(Path.home() / "android-sdk")))
    ndk = sdk / "ndk/27.1.12297006"
    sysroot = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot"
    for name in ("clang", "clang++", "llvm-strip", "readelf", "cmake", "ninja", "protoc", "java"):
        if not shutil.which(name):
            raise SystemExit("Missing Termux tool: " + name)
    if not (sysroot / "usr/include/jni.h").is_file():
        raise SystemExit("NDK r27 is missing: " + str(ndk))
    if not (ROOT / "third_party/llama.cpp/CMakeLists.txt").is_file():
        raise SystemExit("Initialize the dependency with git submodule update --init --recursive")
    protoc_version = subprocess.check_output(["protoc", "--version"], text=True).strip()
    if protoc_version != "libprotoc 33.1":
        raise SystemExit("Expected libprotoc 33.1 to match protobuf-java 4.33.1; found " + protoc_version)
    env = dict(os.environ, GALLERY_NDK=str(ndk), PREFIX=str(prefix))
    build = ROOT / ".local-build/android-arm64"
    run(["cmake", "-S", ROOT / "Android/src/app/src/main/cpp", "-B", build, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
         "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/termux-android.cmake")], env=env)
    run(["cmake", "--build", build, "--target", "gallery_gguf", "-j", max(1, args.jobs)], env=env)
    jni = ROOT / "Android/src/app/build/termux-native/jniLibs/arm64-v8a"
    jni.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / "libgallery_gguf.so", jni / "libgallery_gguf.so")
    shutil.copy2(sysroot / "usr/lib/aarch64-linux-android/libc++_shared.so", jni / "libc++_shared.so")
    run([prefix / "bin/llvm-strip", "--strip-unneeded", jni / "libgallery_gguf.so"])
    dynamic = subprocess.check_output(["readelf", "-d", str(jni / "libgallery_gguf.so")], text=True)
    if "Shared library: [libc.so]" not in dynamic or "STATIC_TLS" in dynamic:
        raise SystemExit("Invalid Android linkage: Bionic must be shared, without initial-exec TLS")
    if "RUNPATH" in dynamic or "RPATH" in dynamic:
        raise SystemExit("Build-machine library paths must not be packaged in the APK")
    loader = ROOT / ".local-build/native-loader-check"
    run(["clang", "-fno-termux-rpath", ROOT / "scripts/native-loader-check.c", "-ldl", "-o", loader])
    loader_env = dict(env, LD_LIBRARY_PATH=str(jni))
    loader_env.pop("LD_PRELOAD", None)
    run([loader, jni / "libgallery_gguf.so"], env=loader_env)
    if args.native_only:
        print("Native ARM64 library:", jni / "libgallery_gguf.so")
        return
    project = ROOT / "Android/src"
    spec = importlib.util.spec_from_file_location("local_tools", ROOT / "scripts/setup-local-tools.py")
    local_tools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(local_tools)
    aapt2 = local_tools.prepare(sdk)
    properties = project / "local.properties"
    if not properties.exists():
        properties.write_text("sdk.dir=" + str(sdk) + "\n")
    tasks = [":app:assembleDebug"]
    if args.tests:
        tasks.append(":app:testDebugUnitTest")
    details = ["--info"] if args.info else []
    run([project / "gradlew", *tasks, *details, "--console=plain", "--no-daemon", "--max-workers=2",
         "-PtermuxBuild=true", "-PtermuxProtoc=" + str(prefix / "bin/protoc"),
         "-Pandroid.aapt2FromMavenOverride=" + str(aapt2)], cwd=project, env=env)
    apk = project / "app/build/outputs/apk/debug/app-debug.apk"
    print("APK:", apk)
    deliver(apk)


if __name__ == "__main__":
    main()
