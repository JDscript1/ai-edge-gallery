# Project workflow

- Develop and compile this application locally on the Android phone in Termux.
- Use `python scripts/build-local.py` to build the native ARM64 library and APK.
- Keep changes local unless the user explicitly asks to publish or push them.
- Do not use GitHub Actions as a build fallback.
- Report local build results separately from APK interface/device validation.
- Deliver to `/storage/emulated/0/Download/Local-Edge-Gallery-Bonsai.apk`, replacing the previous installer after the new copy is verified. Remove obsolete versioned installers for this app; do not accumulate APK copies.
