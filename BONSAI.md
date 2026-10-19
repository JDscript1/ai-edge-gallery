# Local Edge Gallery — Bonsai și GGUF

Fork Android al [Google AI Edge Gallery](https://github.com/google-ai-edge/gallery).
Aplicația are ID-ul `com.jdscript.edgegallery` și se poate instala alături de versiunea Google.

## Utilizare

1. Deschide **AI Chat** sau **Prompt Lab**.
2. Alege **Bonsai-1.7B-Q1_0** din catalog și descarcă modelul (248 MB).
3. Pentru un model mai mare, catalogul include și **Bonsai-4B-Q1_0** (572 MB).
4. Alte modele se importă din managerul de modele: un fișier local `.gguf`, un link direct sau un repository Hugging Face.

Modelele Bonsai din catalog sunt publice și nu cer autentificare Hugging Face.
Modelele cu acces restricționat folosesc fluxul OAuth al aplicației originale, care necesită propria configurație Hugging Face din `DEVELOPMENT.md`.

## Memorie și compatibilitate

- Android 12+, ARM64. Inferența GGUF rulează local pe CPU, cu cel mult patru fire.
- Context inițial: 2048 de tokeni. Răspuns inițial: cel mult 512 tokeni. Ambele limite sunt configurabile separat.
- Pentru un telefon cu 4–5 GB RAM, începe cu Bonsai 1.7B. Dimensiunea fișierului nu reprezintă memoria totală folosită.
- Sunt disponibile chatul text, istoricul conversației, instrucțiunea de sistem, răspunsurile progresive și oprirea generării.
- Șabloanele de conversație Jinja se citesc din metadatele GGUF. Un fișier fără șablon de chat este respins explicit.
- Compatibilitatea altor modele depinde de arhitectura și cuantizarea acceptate de versiunea fixată a motorului. Se importă modele într-un singur fișier GGUF; importul seturilor împărțite în mai multe fișiere nu este implementat.
- Contextul nativ se reconstruiește la fiecare răspuns, folosind istoricul; conversațiile lungi vor avea un timp de pregătire mai mare. La depășirea contextului, aplicația cere o conversație nouă.
- Generarea de imagini, intrările imagine/audio și instrumentele pentru agenți nu sunt conectate la motorul GGUF.

## Compilare

```sh
git clone --recurse-submodules https://github.com/JDscript1/ai-edge-gallery.git
cd ai-edge-gallery
git switch feat/bonsai-local-models
git submodule update --init --recursive
cd Android/src
./gradlew :app:assembleDebug :app:testDebugUnitTest
```

Se folosesc JDK 21, Android SDK 37.0, NDK `27.1.12297006` și CMake `3.22.1`.
Workflow-ul **Build Bonsai Android** compilează APK-ul pe un runner GitHub și îl păstrează în artefactele rulării.
APK: `Android/src/app/build/outputs/apk/debug/app-debug.apk`.

Motorul folosește submodulul [PrismML-Eng/llama.cpp](https://github.com/PrismML-Eng/llama.cpp),
fixat la `e3de715222b2667c54a267441159a5729f11155e` (`prism-b10777-e3de715`).
Licența MIT a motorului este inclusă și în resursele APK-ului; aplicația păstrează licența Apache 2.0.

## Verificarea motorului din Termux

Acest executabil folosește același cod C++ de inferență ca aplicația:

```sh
cmake -S Android/src/app/src/main/cpp -B .local-build/gguf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGALLERY_GGUF_HOST_CHECK=ON
cmake --build .local-build/gguf --target gallery-gguf-smoke -j 4
.local-build/gguf/gallery-gguf-smoke /cale/catre/model.gguf
```

Verifică încărcarea, șablonul, tokenizarea, două mesaje și oprirea generării.
Testele Kotlin verifică păstrarea diacriticelor și emoji când octeții sunt împărțiți între tokeni.
Aceste verificări nu înlocuiesc verificarea APK-ului și a interfeței pe telefonul țintă.

## Modele fixate în catalog

| Model | Revizie Hugging Face | SHA-256 |
|---|---|---|
| [Bonsai 1.7B Q1_0](https://huggingface.co/prism-ml/Bonsai-1.7B-gguf) | `210a9e99f79cb184909d49595906526eb2b3dd9a` | `3d7c6c90dd98717a203adb22d5eacd2581850e40aa5327e144b97766cae5f7e3` |
| [Bonsai 4B Q1_0](https://huggingface.co/prism-ml/Bonsai-4B-gguf) | `78f2c2bacd0904ffaba24b4873ed975e5818354a` | `4524b3f997f0f06444e568d1f26e2efd69effa3218c7ad3047432fb171e42168` |
