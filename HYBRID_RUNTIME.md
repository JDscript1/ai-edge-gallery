# Runtime hibrid llama.cpp pentru telefon

Acest document detaliază adaptorul PrismML/llama.cpp. Planul comun pentru toate motoarele și modelele compatibile este în [RUNTIME_OPTIMIZATION.md](RUNTIME_OPTIMIZATION.md).

## Metoda identificată

Păstrăm motorul PrismML/llama.cpp fixat la `e3de715222b2667c54a267441159a5729f11155e` și adăugăm backendul Vulkan lângă CPU. Distribuim ponderile și calculele între cele două backenduri, păstrând cache-ul KV și starea recurentă în RAM accesibilă CPU.

**Stare la 19 octombrie 2026:** backendul Vulkan rămâne compilat, dar aplicația nu îl mai folosește pentru GGUF. Driverul Adreno 740 al telefonului nu compilează pipeline-urile llama.cpp pentru flash-attention și `quantize_q8_1_x4`, iar offload-ul parțial sau complet este fie instabil, fie mai lent decât CPU. Din `1.0.20-local.7`, calea CPU folosește KV Q8_0 și acceptă un context de până la 16384 de tokeni. Măsurătorile sunt în secțiunea „Situația APK-ului actual”.

CPU și GPU folosesc aceeași RAM fizică pe acest dispozitiv. „Model pe GPU” desemnează bufferul și executorul Vulkan, fără o rezervă de VRAM separată care să mărească RAM-ul disponibil. Stocarea modelului prin `mmap` și memoria temporară de calcul rămân categorii distincte.

## Ce este confirmat local

Diagnostic: `python scripts/probe-vulkan.py`. Folosește antetele NDK și `libvulkan.so` din Android, fără loaderul Mesa din Termux. Nu execută inferență sau un benchmark.

Rezultatul este salvat în `.local-build/android-vulkan-memory.json`.

| Proprietate | Rezultat la 19 octombrie 2026 |
|---|---|
| Dispozitiv | Nubia NX729J, SM8550, Android API 35 |
| GPU de sistem | Adreno (TM) 740 |
| Vulkan | 1.3.128 |
| Heap principal | 11.828.170.752 octeți, comun cu memoria sistemului |
| Memorie vizibilă CPU și locală GPU | Disponibilă, inclusiv variante coerente și cache-uite |
| Import de pointeri existenți, `VK_EXT_external_memory_host` | Indisponibil |
| Buget din driver, `VK_EXT_memory_budget` | Indisponibil |
| Memorie shared per grup de lucru compute | 32 KiB; aceasta este memorie temporară pentru shader |
| Heap de 256 MiB | Protejat; exclus din bugetul modelului |

Dimensiunea heap-ului principal reprezintă capacitatea raportată, nu memoria liberă sau un buget de alocare pentru aplicație. Tipurile de memorie `LAZILY_ALLOCATED` și `PROTECTED` nu se folosesc pentru ponderi sau KV.

Memoria unificată permite buffere mapate accesibile CPU/GPU. Absența importului de pointeri în acest driver înseamnă că accesul fără copie la pointerii unui fișier deja mapat nu poate fi presupus; trebuie urmărite copiile și memoria maximă în faza de încărcare.

## Situația APK-ului actual

Backendul Vulkan este inclus în `1.0.20-local.5` lângă CPU; modul `GPU` trimitea toate straturile (`n_gpu_layers = -1`). Măsurătorile directe pe telefon din 19 octombrie 2026 au arătat:

| Configurație | Rezultat |
|---|---|
| Bonsai-27B Q1_0, flash-attention implicit | `vk::Device::createComputePipeline: ErrorUnknown` pentru `flash_attn_f32_f16_aligned` |
| SmolLM2-135M Q4_K_M, flash-attention oprit | `createComputePipeline: ErrorUnknown` pentru `quantize_q8_1_x4` |
| Bonsai-27B Q1_0, flash-attention oprit, 8 straturi | Rulează; 357,90 MiB în bufferul Vulkan |
| Bonsai-27B Q1_0, 10 straturi (459,45 MiB) | `vk::Queue::submit: ErrorDeviceLost`; 12/16/32/64 eșuează la fel |
| Bonsai-1.7B Q1_0, toate straturile pe GPU | Rulează; turul 1 12,5 s față de 1,7 s pe CPU, turul 2 9,2 s față de 2,8 s |
| Bonsai-27B Q1_0, 9 straturi | Întrerupt de resetarea driverului, care a doborât sesiunea Termux |

În `1.0.20-local.6`, modelele Bonsai, GGUF din catalog și importurile GGUF au doar `CPU`. Adaptorul deschide întotdeauna cu `n_gpu_layers = 0` și corectează o etichetă GPU rămasă dintr-o configurație veche. Motorul oprește flash-attention ori de câte ori se cer straturi GPU, ca lucrările viitoare să nu repete eroarea de compilare. APK-ul rămâne la circa 232 MB din cauza shaderelor incluse în `libgallery_gguf.so` (circa 64,8 MB).

Logul local `.local-build/art-probe/bonsai-27b-q1.log`, pentru Bonsai 27B Q1_0 la context 512, raportează:

| Buffer | Dimensiune |
|---|---:|
| Ponderi CPU mapate | 3616,77 MiB |
| KV | 32,00 MiB |
| Stare recurentă | 149,62 MiB |
| Calcul CPU | 63,13 MiB |

Acestea sunt bufferele raportate de motor la acea configurație, nu consumul total al aplicației sau al telefonului. Modelul Qwen3.5 din acest GGUF are atât atenție, cât și straturi recurente; optimizarea trebuie să includă ambele tipuri de stare.

## Împărțirea propusă

| Componentă | Amplasare / execuție |
|---|---|
| O parte dintre straturile modelului | Buffere Vulkan, calcule GPU |
| Restul straturilor și operațiile nesuportate | CPU |
| Cache KV și stare recurentă | GPU când `offload_kqv` este activ; altfel CPU, în RAM |
| GGUF și pagini recitibile | Fișier mapat din stocarea internă |
| Buffere temporare | Alocate de backendul care execută operația, urmărite separat |

Configurația implementată:

```cpp
// gpu_layers: 0 = CPU, -1 = toate straturile.
model_params.n_gpu_layers = gpu_layers;
model_params.load_mode = LLAMA_LOAD_MODE_MMAP;
context_params.offload_kqv = gpu_layers != 0;
context_params.op_offload = gpu_layers != 0;
if (gpu_layers == 0) {
    context_params.type_k = GGML_TYPE_Q8_0;  // KV la circa jumătate față de F16
    context_params.type_v = GGML_TYPE_Q8_0;  // flash-attention CPU se activează automat
} else {
    context_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
}
```

În versiunea fixată, cache-ul recurent și KV aleg buffere CPU când offload-ul lor este oprit. `offload_kqv` controlează și operațiile K/Q/V; un profil cu acest flag oprit poate introduce transferuri între operațiile GPU și CPU. O împărțire pe straturi nu execută automat toate straturile CPU și GPU simultan. Numărul parțial de straturi rămâne de evaluat; aplicația expune doar CPU sau GPU integral.

PrismML documentează kerneluri Vulkan pentru Q1_0. Codul local include această cuantizare și operații pentru arhitecturi recurente. Totuși, unele variante Gated Delta Net sunt explicit refuzate de backend și trebuie executate pe CPU. Prezența kernelurilor nu dovedește corectitudinea sau viteza pe driverul acestui telefon.

Ca optimizare ulterioară, API-ul are `tensor_buft_overrides`: se pot atribui selectiv tensorii FFN unui buffer GPU și păstra alți tensori pe CPU. Această politică se evaluează după profilul simplu pe straturi; nu este implementată în aplicație.

## Pașii concreți de integrare

1. **Făcut.** Pregătirea locală a `glslc` 2026.2, Vulkan-Headers/Vulkan-Hpp 1.4.354 și SPIRV-Headers 1.4.350.1. Compilatorul de shader rulează pe telefon. Bibliotecile private Termux rămân unelte de compilare; APK-ul folosește Vulkan din Android.
2. **Făcut.** `GGML_VULKAN=ON` în ținta nativă existentă, împreună cu CPU. Verificarea loaderului Android trece înainte de construirea APK-ului, inclusiv cu `libvulkan.so` în dependențe.
3. **Parțial.** Se folosește dispozitivul Vulkan implicit; aplicația nu distinge încă explicit un dispozitiv software de unul hardware.
4. **Oprit.** `1.0.20-local.5` a expus `CPU` / `GPU`, iar GPU însemna toate straturile. `1.0.20-local.6` nu mai oferă GPU pentru GGUF, din cauza rezultatelor de mai sus.
5. **De făcut.** Calcularea ponderilor și a cache-urilor înainte de încărcare, citirea memoriei disponibile prin API Android și păstrarea unei rezerve pentru sistem/aplicație. Driverul nu oferă aici un buget Vulkan dinamic; capacitatea heap-ului nu este folosită drept buget.
6. **Făcut ca diagnostic.** Comparația s-a făcut pe SmolLM2-135M Q4_K_M și Bonsai-1.7B Q1_0, cu flash-attention oprit. GPU-ul compilează primul pipeline pe 135M, dar eșuează pe `quantize_q8_1_x4`; pe 1.7B rulează complet și este de circa 6 ori mai lent decât CPU.
7. **Făcut.** Calea CPU este cea folosită implicit; o etichetă GPU rămasă dintr-o configurație veche este corectată la CPU la încărcare.

Cuantizarea KV la Q8_0 este implementată pe calea CPU din `1.0.20-local.7`. La 16384 de tokeni, cache-ul KV scade de la 1024 la 544 MiB pe Bonsai-27B Q1_0 și de la 1792 la 952 MiB pe Bonsai-1.7B Q1_0. La 8192 de context și un prompt de 3024 tokeni pe Bonsai-1.7B, prelucrarea promptului a durat 223,2 s cu F16 și 285,0 s cu Q8_0 (circa 28% mai lent), în timp ce PSS vârful a scăzut de la 1,24 la 0,83 GiB. llama.cpp activează flash-attention pe CPU deoarece V cuantizat îl cere; calea GPU rămâne pe F16, cu flash-attention oprit. Starea recurentă a modelelor Qwen3.5 nu este echivalentă cu KV și nu se cuantizează prin aceeași opțiune: la 27B rămân 149,62 MiB în F32. Mutarea KV activ pe stocare ar necesita paginare și ar introduce acces la stocare la inferență; implementarea îl păstrează în RAM.

## Surse

- [PrismML: backend și cuantizări](https://github.com/PrismML-Eng/Bonsai-demo/blob/main/BACKEND-SUPPORT.md)
- [API-ul exact al motorului fixat](https://github.com/PrismML-Eng/llama.cpp/blob/e3de715222b2667c54a267441159a5729f11155e/include/llama.h)
- [Khronos: memorie UMA, host-visible și device-local](https://docs.vulkan.org/guide/latest/memory_allocation.html)
- [Android NDK: reguli de proiectare Vulkan](https://developer.android.com/ndk/guides/graphics/design-notes)

Stadiu: backendul Vulkan este inclus în `1.0.20-local.5` și selectabil din configurația modelului. Inferența pe GPU nu a fost încă verificată sau măsurată pe telefon; performanța și stabilitatea rămân necunoscute până la acea verificare.
