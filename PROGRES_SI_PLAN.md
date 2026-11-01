# Local Edge Gallery — progres și pașii următori

Actualizat: **19 octombrie 2026**. Acest document descrie starea locală a proiectului la această dată. Versiunile, rezultatele și hash-urile de mai jos sunt un instantaneu al lucrului realizat, nu o promisiune pentru orice model sau telefon.

## 1. Scopul proiectului și deciziile luate

Am pornit de la aplicația open source Google AI Edge Gallery și construim un fork Android numit **Local Edge Gallery**, cu suport pentru PrismML Bonsai și alte modele locale compatibile.

- Director de lucru: `/data/data/com.termux/files/home/ai-edge-gallery`.
- Branch curent: `feat/bonsai-local-models`.
- Identificator Android: `com.jdscript.edgegallery`; aplicația poate coexista cu versiunea Google.
- Dezvoltarea și compilarea se fac **local, pe telefon, în Termux**.
- Modificările noi rămân locale până la o cerere explicită de publicare/push. Au existat publicări inițiale înaintea acestei decizii; acestea nu reprezintă toate modificările locale actuale.
- Fiecare livrare înlocuiește același installer: `/storage/emulated/0/Download/Local-Edge-Gallery-Bonsai.apk`.
- Modelele și fișierele originale ale utilizatorului se păstrează. Curățarea privește copiile installerului create de proiect.
- **Bonsai Image este amânat**, conform deciziei utilizatorului. Prioritatea actuală este chatul local și optimizarea inferenței.
- Optimizarea urmărită acoperă **toate modelele compatibile cu motoarele aplicației**, cu politici adaptate fiecărui motor.

Regulile de lucru se găsesc în [AGENTS.md](AGENTS.md).

## 2. Ce am realizat

### 2.1. Integrarea Bonsai și GGUF

- Am integrat un motor nativ C++ prin JNI, folosind forkul PrismML/llama.cpp fixat la commitul `e3de715222b2667c54a267441159a5729f11155e` (`prism-b10777-e3de715`).
- Modelele GGUF compatibile pot fi folosite pentru chat text în **AI Chat** și **Prompt Lab**.
- Am adăugat importul din fișier local, link direct și repository Hugging Face, inclusiv descoperirea fișierelor GGUF.
- Importul folosește fișiere temporare și finalizare atomică pentru a evita înregistrarea unui model copiat incomplet. Originalul utilizatorului este păstrat.
- Catalogul include Bonsai **1.7B Q1_0**, aproximativ 248 MB, și **4B Q1_0**, aproximativ 572 MB, cu revizii și hash-uri fixate.
- Șablonul de conversație se citește din metadatele modelului. Sunt conectate istoricul, instrucțiunea de sistem, parametrii de generare, răspunsurile progresive și oprirea generării.
- Transferul UTF-8 prin JNI și decodarea incrementală tratează și caracterele împărțite între fragmente, inclusiv diacritice și emoji.
- Gestionarea instanțelor native permite anularea și așteaptă încheierea operațiilor înainte de eliberarea resurselor.

**Limita actuală:** compatibilitatea depinde de arhitectura și cuantizarea acceptate de motor. Importul modelelor GGUF împărțite în mai multe fișiere nu este implementat. Imaginile/audio și instrumentele pentru agenți nu sunt conectate la adaptorul GGUF.

### 2.2. Compilarea completă pe telefon

Am pregătit comanda unică de compilare:

```sh
cd /data/data/com.termux/files/home/ai-edge-gallery
python scripts/build-local.py
```

Fluxul local:

1. Compilează biblioteca GGUF ARM64 cu Clang din Termux și antetele/bibliotecile Android NDK.
2. Folosește toolchain-ul proiectului, fără modificarea NDK-ului comun.
3. Pregătește AAPT2 modern prin QEMU local și biblioteci private; descărcările uneltelor au hash-uri fixate.
4. Construiește APK-ul prin Gradle local.
5. Verifică semnătura APK-ului și integritatea copiei înainte de înlocuirea installerului din Download.

Configurația documentată: JDK 21, SDK/target 37, minSdk 31, ARM64, build-tools `36.0.0`, NDK `27.1.12297006`, protoc `33.1` și runtime Protobuf `4.33.1`. Versiunile și detaliile sunt în [BONSAI.md](BONSAI.md).

Comanda implicită compilează aplicația. Scriptul are și `--native-only` pentru biblioteca nativă, respectiv `--tests` pentru verificările unitare atunci când sunt cerute. Testele unitare au fost rulate separat pe arborele curent la 19 octombrie 2026: `BUILD SUCCESSFUL` în 40 de secunde, fără modificarea APK-ului.

### 2.3. Eroarea de încărcare GGUF raportată din aplicație

Utilizatorul a raportat erori la încărcarea unui model din memoria internă și a trimis capturi. Am identificat o problemă de legare a bibliotecii native: ordinea directoarelor NDK permitea includerea accidentală a unor componente Bionic statice, cu TLS incompatibil cu încărcarea din Android.

Remediile implementate:

- Bibliotecile partajate pentru API 31 sunt căutate înaintea bibliotecilor generice din NDK.
- Buildul respinge biblioteca dacă lipsește dependența dinamică `libc.so`, dacă apare `STATIC_TLS` sau dacă sunt incluse căi RPATH/RUNPATH ale mediului de compilare.
- Un program local verifică deschiderea bibliotecii cu loaderul Android înainte de construirea APK-ului.
- Kotlin păstrează eroarea inițială de încărcare; încercările ulterioare nu o mai maschează printr-un `NoClassDefFoundError`.

După livrarea remedierii, **utilizatorul a confirmat că aplicația funcționează**. Această confirmare precedă livrarea ulterioară cu bifa Thinking.

### 2.4. Modelele 27B și diferența dintre fișiere

Am separat două fișiere locale care nu au aceeași compatibilitate:

| Fișier identificat | Rezultat înregistrat |
|---|---|
| `Bonsai-27B-Q1_0.gguf`, aproximativ 3,80 GB | Încărcare și răspuns simplu prin biblioteca din APK, executată cu Android ART; context 512 în diagnosticul respectiv. |
| `Ternary-Bonsai-27B-Q2_0.gguf`, aproximativ 7,17 GB | Incompatibilitate de layout al tensorilor cu versiunea fixată a motorului. Raportul identifică 498 de dimensiuni incompatibile. Acest fișier nu este validat pentru inferență. |

Captura discutată indica modelul Q1_0. Denumirea informală „ternary” nu este suficientă pentru identificarea formatului. Schimbarea CPU/GPU nu repară o incompatibilitate a fișierului cu motorul.

Dovezile locale sunt în `.local-build/art-probe/bonsai-27b-q1.log` și `.local-build/bonsai-27b-quantization-check.json`. Fișierele originale ale modelelor au fost păstrate.

### 2.5. Bifa Thinking ON/OFF

- Am adăugat o bifă deasupra casetei de mesaj în AI Chat, pentru modelele care declară suport pentru schimbarea modului.
- Setarea se transmite explicit la următorul răspuns, fără reîncărcarea modelului.
- Bifa este dezactivată în timpul generării sau operațiilor de reinițializare.
- Adaptorul GGUF verifică suportul șablonului și transmite `enable_thinking` către motor.
- Fluxurile de răspuns și Thinking sunt decodate separat și afișate folosind panoul de gândire existent.
- Tokenizarea promptului ține cont de mod; Thinking și răspunsul final împart limita de generare.
- Bifa se salvează per model în DataStore (`enable_thinking_by_model`, cheia fiind numele modelului) și se reaplică la pornire pe modelele din catalog și pe cele importate. Celelalte setări de generare (top-k, top-p, temperatură) rămân în memorie și se resetează la repornire.

**Starea verificării:** implementarea, inclusiv persistența, este inclusă în APK-ul `1.0.20-local.4` compilat cu succes la 19 octombrie 2026. Testele unitare JVM au trecut în aceeași rulare (`--tests`, `BUILD SUCCESSFUL`). Utilizatorul a confirmat pe 19 octombrie 2026, pe telefon, că modul Thinking funcționează în această versiune. Persistența bifei după repornire și oprirea generării în timpul Thinking nu au fost încă verificate separat. În `1.0.20-local.5`, motorul permitea și selectarea CPU/GPU (secțiunea 2.6); `1.0.20-local.6` dezactivează GPU pentru GGUF după măsurătorile de pe telefon.

### 2.6. Backendul Vulkan pentru GGUF

- Buildul nativ include acum `GGML_VULKAN=ON` lângă CPU. Opțiunea `GALLERY_GGUF_VULKAN` este activă implicit pe Android.
- Termux furnizează doar uneltele de compilare: `glslc` 2026.2, Vulkan-Headers/Vulkan-Hpp 1.4.354 și SPIRV-Headers 1.4.350.1. Antetele Vulkan și SPIRV sunt copiate într-un director izolat al buildului, ca să nu înlocuiască antetele glibc din Termux peste cele Bionic din NDK.
- Linkarea folosește stub-ul `libvulkan.so` din NDK API 31; la rulare se folosește loaderul Android. Vulkan-Hpp apelează funcțiile Vulkan 1.3/1.4 prin `vkGetInstanceProcAddr`, deci nu sunt necesare simboluri noi la linkare.
- `1.0.20-local.5` a oferit `CPU` / `GPU`, cu GPU însemnând toate straturile (`n_gpu_layers = -1`). Măsurătorile de pe telefon din 19 octombrie 2026 au arătat că driverul Adreno nu poate rula această cale, așa că `1.0.20-local.6` o dezactivează: modelele Bonsai, modelele GGUF din catalog și importurile GGUF au doar `CPU`.
- Driverul Vulkan Adreno 740 refuză compilarea unor pipeline-uri llama.cpp: `flash_attn_f32_f16_aligned` eșuează cu `vk::Device::createComputePipeline: ErrorUnknown` pe Bonsai-27B, iar cu flash-attention oprit `quantize_q8_1_x4` eșuează pe SmolLM2 Q4_K_M. Eroarea apare la primul calcul, nu la deschiderea modelului.
- Cu flash-attention oprit, pe Bonsai-27B Q1_0: 8 straturi (7 repetate + stratul de ieșire, 357,90 MiB în bufferul Vulkan) rulează până la capăt; 10 straturi (459,45 MiB) și 12/16/32/64 eșuează cu `vk::Queue::submit: ErrorDeviceLost`. Rularea cu 9 straturi a fost întreruptă de resetarea driverului, care a doborât și sesiunea Termux.
- Pe Bonsai-1.7B Q1_0 toate straturile rulează pe GPU, dar sunt de circa 6 ori mai lente decât pe CPU: turul 1 a durat 12,5 s față de 1,7 s, iar turul 2, 9,2 s față de 2,8 s. Nu există o configurație GPU măsurată care să fie mai rapidă sau sigură.
- Backendul Vulkan rămâne compilat în motor, iar `flash_attn_type` este oprit automat când se cer straturi GPU. Bibliotecile conțin `libvulkan.so` în `DT_NEEDED`, fără RPATH/RUNPATH și fără TLS inițial-exec; verificarea loaderului din `build-local.py` trece.
- Costul rămâne: shaderele Vulkan sunt incluse în `libgallery_gguf.so` (circa 64,8 MB), iar APK-ul are circa 232 MB.

**Starea verificării:** `1.0.20-local.5` (versionCode 50) a trecut compilarea nativă, verificarea de legare, testele JVM și semnătura. Măsurătorile GPU de mai sus au rulat direct pe telefon, prin diagnosticul construit din aceleași biblioteci native. `1.0.20-local.6` (versionCode 51) a fost construit local la 19 octombrie 2026: biblioteca nativă a recompilat `gguf_engine.cpp`, `BUILD SUCCESSFUL` în 2 minute și 31 de secunde cu `--tests`, semnătura verificată, APK livrat la `Download/Local-Edge-Gallery-Bonsai.apk` (231.010.760 octeți, SHA-256 începe cu `41c3e357`). Dezactivarea GPU urmează să fie verificată în interfață pe telefon.

### 2.7. Context extins și cache KV Q8_0

- Limita de context GGUF a crescut de la 8192 la 16384 de tokeni: verificarea din motor, adaptorul Kotlin, sliderul din AI Chat / Prompt Lab, dialogul de import și metadatele modelelor Bonsai.
- Pe CPU, cache-ul KV folosește Q8_0 pentru K și V. llama.cpp activează automat flash-attention pe CPU, deoarece un cache V cuantizat o cere; calea GPU rămâne pe F16 și păstrează flash-attention oprit.
- Starea recurentă a modelului hibrid Qwen3.5 nu se cuantizează prin această opțiune: la Bonsai-27B rămân 149,62 MiB în F32.
- Măsurători pe telefon cu diagnosticul `gallery-gguf-smoke`, legat din aceleași biblioteci native ca aplicația, pe modelele existente (fără descărcări):

| Model și context | KV F16 | KV Q8_0 | PSS vârf F16 | PSS vârf Q8_0 | Tur 1 | Tur 2 |
|---|---:|---:|---:|---:|---|---|
| Bonsai-27B Q1_0, 16384 | 1024 MiB | 544 MiB | 4,66 GiB | 4,42 GiB | 43,3 s → 35,9 s | 59,7 s → 59,8 s |
| Bonsai-1.7B Q1_0, 16384 | 1792 MiB | 952 MiB | 2,11 GiB | 1,30 GiB | 2,15 s → 2,07 s | 3,54 s → 3,45 s |

- Timpii includ prelucrarea unui prompt scurt și un răspuns scurt; ei compară variantele, nu reprezintă viteza unui context plin. PSS este vârful observat al procesului într-o singură rulare, iar paginile de ponderi mapate pot varia între rulări; dimensiunea KV raportată de motor este cifra de referință pentru memorie.
- Prelucrarea promptului rămâne costul principal pe CPU și crește cu Q8_0: la 8192 de context și un prompt de 3024 tokeni pe Bonsai-1.7B, turul 1 a durat 223,2 s cu F16 și 285,0 s cu Q8_0 (circa 28% mai lent). În aceeași configurație, KV a scăzut de la 896 la 476 MiB, iar PSS vârful de la 1,24 la 0,83 GiB. Reutilizarea prefixului, planificată în Etapa 4, rămâne optimizarea necesară pentru conversații lungi.
- KV rămâne în RAM. Mutarea lui activ pe stocare ar presupune paginare la fiecare pas de inferență; numai ponderile sunt mapate din fișier (`mmap`).

**Starea verificării:** măsurătorile de mai sus au rulat direct pe telefon la 19 octombrie 2026, cu modelele existente. APK-ul `1.0.20-local.7` (versionCode 52) a fost construit și livrat în aceeași zi; detaliile sunt în secțiunea 2.8. Verificarea în interfață, inclusiv sliderul de 16384 și comportamentul la conversații lungi, rămâne de făcut pe telefon.

### 2.8. Un singur installer livrat

Scriptul livrează la aceeași cale, printr-un fișier temporar verificat SHA-256 și înlocuire atomică. Elimină installer-ele vechi cu numele de versiune recunoscute ale proiectului și copiile APK de diagnostic cunoscute. Nu șterge arbitrar alte APK-uri sau modelele utilizatorului.

Ultimul build înregistrat:

| Proprietate | Valoare |
|---|---|
| Versiune | `1.0.20-local.8` |
| versionCode | `53` |
| Tip | Debug, ARM64, CPU + Vulkan compilat (Vulkan nefolosit la rulare) |
| APK în proiect | `Android/src/app/build/outputs/apk/debug/app-debug.apk` |
| Installer livrat | `/storage/emulated/0/Download/Local-Edge-Gallery-Bonsai.apk` |
| Dimensiune înregistrată | 231.027.560 octeți |
| SHA-256 înregistrat | `63fed5431e6557a7f03f68cb6eb9f18872f2c06800a5f3df36ad2e44ae6edef4` |
| Rezultat compilare | `BUILD SUCCESSFUL`, 2 minute și 13 secunde pentru pasul Gradle cu `--tests` |
| Semnătură | Verificată în fluxul de build |
| Copie în Download | SHA-256 identic cu APK-ul din proiect |
| Teste unitare | `BUILD SUCCESSFUL`, rulate în aceeași invocare `--tests` |
| Legare nativă | `libvulkan.so` în `DT_NEEDED`, fără RPATH/RUNPATH și fără `STATIC_TLS` |
| Interfață pentru ultimul APK | Nu este marcată ca verificată în raport |

Surse locale: `.local-build/apk-report.json`, `.local-build/local8-build.log` (buildul complet) și `.local-build/vkdiag/run-*.log` (măsurătorile F16/Q8_0). Datele trebuie actualizate la o livrare viitoare; documentarea de față nu produce un APK nou.

### 2.9. Catalogul de modele și importul în `1.0.20-local.8`

Sufixul de fork din `versionName` (`-local.7`) intra în numele fișierului de allowlist, așa că aplicația cerea `1_0_20_local_7.json` și primea 404. Fără un cache valid pe disc, catalogul rămânea doar cu modelele Bonsai de rezervă, iar recomandările Gemma 4 și Gemma 3 dispăreau. Din `1.0.20-local.8`, numele se formează din partea de dinainte de `-` (`1_0_20.json`); catalogul upstream are 11 modele, inclusiv `Gemma-4-E2B-it`, `Gemma-4-E4B-it`, `Gemma-3n-E2B-it`, `Gemma-3n-E4B-it` și `Gemma3-1B-IT`.

Butonul de adăugare exista doar ca FAB în managerul global de modele (meniul lateral → Models); lista de modele a unei sarcini, folosită la intrarea în AI Chat, nu avea niciun punct de intrare. În `1.0.20-local.8`:

- lista de modele a sarcinii are un buton **Add model** sub modele, care deschide managerul global cu foaia de import deja afișată;
- managerul global are și un `+` în bara de sus, pe lângă FAB-ul existent;
- foaia de import oferă „From local model file” pentru un fișier `.gguf` local și importul prin link Hugging Face;
- modelele importate apar în continuare în secțiunea „Imported models”, atât în lista sarcinii, cât și în managerul global.

## 3. Ce știm despre telefon și resurse

Dispozitiv identificat local: **Nubia NX729J**, SoC **SM8550 / Snapdragon 8 Gen 2**, GPU **Adreno 740**, Android API 35. `/proc/meminfo` raportează `MemTotal: 11550948 kB`, aproximativ 11 GiB accesibili sistemului, corespunzător clasei comerciale de 12 GB. Cei 4–5 GB discutați inițial nu reprezintă RAM-ul total raportat al acestui telefon; memoria disponibilă variază în timp.

Am pregătit diagnosticul `scripts/probe-vulkan.py`, care folosește loaderul Vulkan din Android. Rezultatul existent indică:

- Vulkan `1.3.128`, GPU de sistem Adreno 740.
- Un heap principal de 11.828.170.752 octeți, cu tipuri de memorie accesibile atât CPU, cât și GPU.
- RAM fizică partajată: memoria alocată pentru GPU intră în același buget al telefonului.
- Lipsa extensiilor `VK_EXT_memory_budget` și `VK_EXT_external_memory_host` pe acest driver.
- 32 KiB memorie shared per grup compute, pentru lucrul shaderelor; aceasta nu este un spațiu general pentru întregul KV.

Diagnosticul enumeră capabilități. Nu demonstrează că inferența GPU este integrată, mai rapidă sau corectă pentru un model. Dimensiunea heap-ului nu este RAM liberă. Nu confundăm driverul Android cu un eventual driver Mesa din Termux.

Pentru Bonsai 27B Q1_0 la context 512, logul CPU înregistrează aproximativ 3616,77 MiB ponderi mapate, 32 MiB KV, 149,62 MiB stare recurentă și 63,13 MiB buffer de calcul. Aceste valori nu reprezintă consumul total al aplicației și nu se extrapolează automat la alte contexte sau modele.

## 4. Situația actuală a motoarelor

| Motor | Stare în proiect | Direcția optimizării |
|---|---|---|
| PrismML/llama.cpp | Adaptor GGUF cu backend Vulkan inclus; selectabil `CPU` / `GPU`. Pe GPU: `n_gpu_layers = -1`, `offload_kqv = true`, `op_offload = true`; pe CPU valorile rămân oprite. Fallback automat la CPU dacă Vulkan nu se deschide. | Offload parțial pe număr de straturi, planificarea KV și a stării recurente, fire CPU, buffere, context și reutilizarea prefixului. |
| LiteRT-LM `0.18.0` | Motor existent al aplicației, cu selectarea backendului și backenduri pentru modalități. Suportul fiecărui model/backend trebuie stabilit separat. | Alegerea backendului compatibil CPU/GPU/NPU, fire CPU, context și encodere, în limitele API-ului public. |
| AICore / ML Kit Prompt | Adaptor existent, cu disponibilitate și limite controlate de serviciul Android. Disponibilitatea pentru acest fork/telefon nu este demonstrată aici. | Preferință FAST/FULL, warmup și gestionarea limitelor raportate de serviciu. Amplasarea internă CPU/GPU/KV nu este expusă aplicației. |

Versiunile, capabilitățile și arhitectura propusă sunt detaliate în [RUNTIME_OPTIMIZATION.md](RUNTIME_OPTIMIZATION.md).

## 5. Ce mai urmează să facem

### Etapa 1 — confirmarea funcțiilor din ultimul APK

- [ ] Verificare în interfață: încărcare model, conversație cu mai multe mesaje, streaming, oprire și schimbare de model.
- [x] Verificare Thinking ON/OFF pe modele compatibile, confirmată de utilizator pe telefon la 19 octombrie 2026 (`1.0.20-local.4`); oprirea generării în timpul Thinking nu a fost încă testată separat.
- [x] Persistența bifei Thinking per model în DataStore, reaplicată la pornire (implementată în `1.0.20-local.4`); verificarea pe telefon rămâne de făcut.
- [ ] Persistența celorlalte setări de generare (top-k, top-p, temperatură), dacă se dorește.
- [ ] Menținerea unei evidențe clare: build reușit, verificare nativă și verificare în interfață sunt rezultate distincte.

### Etapa 2 — gestionarul comun de resurse

- [ ] Introducerea unui planificator comun pentru toate căile de inițializare ale motoarelor.
- [ ] Declararea capabilităților reale prin adaptoare per motor, inclusiv opțiunile indisponibile sau necunoscute.
- [ ] Citirea RAM-ului disponibil, presiunii de memorie, consumului procesului și stării termice.
- [ ] Estimarea ponderilor, KV, stării recurente, bufferelor temporare și vârfului de încărcare, cu rezervă pentru sistem/UI.
- [ ] Alegerea contextului după buget, limitele motorului și limitele modelului; rezervarea spațiului pentru input formatat, modalități și răspuns/Thinking.
- [ ] Profile Echilibrat / Viteză / Context maxim / Calitate și salvarea locală a măsurătorilor pentru model, motor, telefon și driver.
- [ ] Raportarea backendului și contextului efectiv, unde motorul le expune. O estimare nu este afișată ca rezultat măsurat.

### Etapa 3 — GPU pentru GGUF

- [x] Pregătirea locală a dependențelor Vulkan și a compilatorului de shadere cu versiuni fixate (`glslc` 2026.2, Vulkan-Headers 1.4.354, SPIRV-Headers 1.4.350.1).
- [x] Compilarea backendului Vulkan împreună cu CPU; aplicația selectează dispozitivul Vulkan implicit al sistemului (un singur GPU pe acest telefon).
- [x] Expunerea unei politici GPU simple: toate straturile (`n_gpu_layers = -1`) sau niciunul, cu `offload_kqv` / `op_offload` active doar pe GPU.
- [x] Măsurarea pe telefon a politicii simple: 8 straturi parțiale rulează, 10 și peste doboară dispozitivul Vulkan; modelul complet de 1,7B rulează dar este de circa 6 ori mai lent decât CPU.
- [x] Dezactivarea GPU în aplicație pentru GGUF (`1.0.20-local.6`), cu backendul Vulkan păstrat compilat pentru drivere viitoare.
- [ ] Reevaluarea GPU pe un driver/dispozitiv care compilează pipeline-urile llama.cpp.

Detaliile sunt în [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md). Selecția CPU/GPU a fost inclusă în `1.0.20-local.5`; `1.0.20-local.6` revine la CPU pentru GGUF, pe baza măsurătorilor de mai sus.

### Etapa 4 — reducerea timpului de pregătire și extinderea contextului

- [x] GGUF: cache KV Q8_0 și flash-attention CPU, cu limita de context ridicată la 16384 (`1.0.20-local.7`).
- [ ] GGUF: înlocuirea ștergerii cache-ului și a recalculării integrale a istoricului la fiecare răspuns cu reutilizarea prefixului valid.
- [ ] Tratarea schimbărilor de template, Thinking, instrucțiune de sistem, istoric și anulare, inclusiv pentru modele recurente.
- [ ] Folosirea conversației native LiteRT-LM fără resetări inutile și integrarea limitelor reale AICore în planificator.
- [ ] Evaluarea preciziilor KV acceptate, separat de cuantizarea ponderilor și de starea recurentă.
- [ ] Creșterea contextului numai în limite acceptate și măsurate; rezumarea/RAG, dacă vor fi adăugate, se raportează separat de fereastra activă.

### Etapa 5 — alegerea profilelor pe baza rezultatelor

- [ ] Compararea configurațiilor pe aceeași sarcină: timp până la primul token, procesare prompt, tokeni/secundă și corectitudinea răspunsurilor.
- [ ] Urmărirea memoriei maxime, copiilor CPU/GPU și comportamentului la încălzire.
- [ ] Separarea rulărilor reci/calde și a estimărilor de măsurători; consumul GPU/AICore necunoscut nu se înregistrează ca zero.
- [ ] Salvarea profilului potrivit pentru fiecare prioritate și invalidarea recomandărilor după schimbări relevante de model/motor/driver.

Viteza maximă, contextul maxim și calitatea pot necesita alegeri diferite. Mutarea pe GPU nu mărește automat calitatea. Nu schimbăm în tăcere modelul sau cuantizarea pentru a obține un scor mai bun.

### Compatibilități și funcții pentru mai târziu

- [ ] Soluție compatibilă pentru fișierul Ternary Bonsai 27B identificat, printr-un motor/format acceptat și verificat separat.
- [ ] Import GGUF în mai multe fișiere, dacă devine necesar.
- [ ] Intrări imagine/audio și instrumente pentru agenți în adaptorul GGUF, dacă vor intra în scopul următoarelor etape.
- [ ] Bonsai Image: rămâne amânat; nu este inclus în optimizările deja realizate.

## 6. Documente și dovezi pentru continuarea lucrului

| Document / director | Conținut |
|---|---|
| [BONSAI.md](BONSAI.md) | Utilizare, modele, compatibilitate și compilare locală. |
| [RUNTIME_OPTIMIZATION.md](RUNTIME_OPTIMIZATION.md) | Metoda comună pentru toate motoarele, buget/context, profile și puncte de integrare. |
| [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) | Cazul concret llama.cpp/Vulkan și diagnosticul telefonului. |
| [AGENTS.md](AGENTS.md) | Regulile proiectului: lucru local, publicare explicită și înlocuirea installerului. |
| `scripts/build-local.py`, `cmake/termux-android.cmake` | Compilare și livrare locală. |
| `.local-build/apk-report.json`, `.local-build/vulkan-apk-build2.log`, `.local-build/vulkan-build.log` | Ultimul build livrat, verificările APK-ului și compilarea bibliotecii Vulkan. |
| `.local-build/art-probe/` | Loguri de încărcare/inferență prin Android ART, anterioare bifei Thinking. |
| `.local-build/bonsai-27b-quantization-check.json` | Diagnosticul celor două fișiere 27B. |
| `.local-build/android-vulkan-memory.json` | Capabilitățile GPU/driverului Android. |

`.local-build/` este ignorat de Git; logurile și diagnosticele menționate sunt dovezi locale, fără garanția că vor exista într-o clonă nouă. Acest document păstrează concluziile utile pentru reluarea lucrului. La fiecare etapă încheiată, actualizăm starea, versiunea livrată și dovezile, fără a marca un plan drept funcție terminată.
