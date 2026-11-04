# DevilutionX (Diablo) dla Amigi z kartą Warp: plan i stan prac

## Stan prac

| Krok | Stan |
|---|---|
| 0. Rozpoznanie, decyzje, repo (`upstream/`, `port/`, `tools/`) | ✅ 3.11.2026 |
| 1. Kompilator: obraz CI upstream `amigadev/crosstools:m68k-amigaos-gcc10` = **gcc 16.2.1b** (AmigaDev), CMake 4.4.5, C++23 OK | ✅ |
| 2. Build wzorcowy upstream (prawdziwe SDL2), `tools/build_ref.sh` | ❌ 3.11 zbudowany, na Amidze 1 od razu niszczy listę pamięci (niżej) |
| 3. Własny podzbiór SDL2 (`port/sdl`, prawdziwe nagłówki SDL2, kod z Dune) | ✅ 3.11 |
| 4. Pierwsze uruchomienie na Amidze 1: menu, film, nowa postać, Tristram (bez dźwięku) | ✅ 3.11 |
| 5. Pomiar i optymalizacja | ⏳ w toku (29 kl./s w Tristram) |
| 6. Klawisze P / Esc / F10, numer wersji | ⏳ |
| 7. Wydanie | ⏳ |

## Decyzje (3.11.2026, zaakceptowane przez użytkownika)

1. **Sposób D (port ze źródeł)**, baza: aktualny upstream `diasurgical/devilutionX` (master 1.6.0-dev,
   ostatnie wydanie 1.5.5). Czysta kopia w `upstream/devilutionX` (poza gitem).
2. **Natywny backend od razu:** własny podzbiór SDL2 na Amigę (`port/sdl`), bez biblioteki SDL2.
   Obraz: gra rysuje 640×480 w 8 bitach z paletą → własny ekran 8-bit CLUT (bez konwersji)
   albo okno na Workbenchu 16-bit; do VRAM tylko zmienione prostokąty.
   Dźwięk: własny miks na Paulę, bez resamplera Speex.
3. **Tylko Amiga.** Upstream na PC służy tylko jako wzorzec.
4. Testy na Amidze 1 (A1200 + Warp 1260, `amiagent.cfg`; Amiga 2 niedostępna 3.11.2026), katalog
   `WorkSD:Games/DiabloWarp`; dane z `WorkSD:Games/Diablo/diabdat.mpq` (instalacja użytkownika,
   tylko do odczytu), zapisy i ini w `DiabloWarp` (`--data-dir`, `--save-dir`, `--config-dir`).

## Platforma źródłowa (upstream master, 3.11.2026)

- C++23 (`CMAKE_CXX_STANDARD 23`), Lua + sol2, magic_enum, SDL2/SDL3/SDL1 do wyboru.
- `CMake/platforms/amiga.cmake`: `NONET` (bez wyjątków i RTTI), SDL2 budowane z kodu,
  `DEVILUTIONX_DISPLAY_PIXELFORMAT SDL_PIXELFORMAT_INDEX8`, bez `--gc-sections` (psuło tablice
  magic_enum na m68k).
- CI: `-DM68K_CPU=68040 -DM68K_FPU=hard -DM68K_COMMON="-s -fbbb=- -ffast-math"`.
- Obraz (`Source/engine/dx.cpp`): 8-bitowy `PalSurface` → blit na powierzchnię okna →
  `SDL_UpdateWindowSurfaceRects(DirtyRects)` albo renderer i tekstura. Dla nas ścieżka bez
  renderera (powierzchnia okna 8-bit, zmienione prostokąty).

## Stary port z Aminetu (2020)

`AmigaPortOrginal/` (poza gitem): DevilutionX z 22.02.2020, SDL 1.2, 640×480×8, kod AMMX
(Vampire). `debug.log`: tryb 640x480 bpp=8, bez skalowania.

## Build wzorcowy upstream na Amidze 1 (3.11.2026)

`tools/build_ref.sh` (obraz CI, gcc 16.2, 68040 + FPU, SDL2 z upstream), program 6,6 MB + `assets`
(12 MB) w `WorkSD:Games/DiabloWarp/ref`, start `run-ref` (`Stack 1000000`, `-n -f --verbose`,
`--data-dir WorkSD:Games/Diablo/`). Zaraz po starcie: **Software Failure 0x0100000C** „Sanity check
on memory list failed during AvailMem(MEMF_LARGEST)” (ROM, PC $F821AE, zgłosił task „RAM”) –
uszkodzona lista wolnej pamięci Exec, czyli zapis poza przydzielonym blokiem albo zwolnienie złego
bloku. AmiAgent przestał odpowiadać, potrzebny reset ręczny. Zdjęcie: `pic_err/IMG_3786.JPG`.
Nie debugujemy (SDL2 z upstream i tak zastępuje nasz backend); w naszej wersji od początku tryb
testowy przydziału pamięci ze strażnikami granic bloków.

## Narzędzia (Amiga 1, `WorkSD:Games/DiabloWarp`)

| Co | Jak |
|---|---|
| build | `sh tools/build.sh [clean]` → `build/amiga/devilutionx` (+ `.map` dla profilera) |
| zmiany w kodzie gry | `port/apply_port.py` (podmiany tekstu z kontrolą) + `port/overlay/` |
| start | `start "Execute WorkSD:Games/DiabloWarp/run-warp"` (Stack 1000000, `DIABLO_STATS`, log `RAM:diablo.log`) |
| wejście testowe | `sh tools/cmd.sh "0 click 320 213" "2500 key return" ...` → `RAM:diablo-cmd`; też `profon`, `profoff`, `stats`, `mark` |
| zrzut ekranu 8-bit | `sh tools/shot.sh plik.png` (`clutshot` z Dune – `SHOT` AmiAgenta ma złe kolory na CLUT) |
| profiler | `profon`/`profoff` → `sh tools/prof.sh` (symbole z mapy linkera, `tools/map2syms.py`; po LTO plik Amigi nie ma symboli) |
| statystyki | `ENV:DIABLO_STATS`: co 5 s kl./s, czas klatki, czas wysyłania, KB/klatkę (log zablokowany, dopóki gra działa) |
| wejście do Tristram | menu → `0 click 320 213`, `2500 key return`, `2500 key return` (postać „warp”, Normal) |

## Wydajność (Amiga 1: A1200 + Warp 1260, 68060; Tristram, postój przy starcie)

| Zmiana | kl./s | klatka | wysyłanie | Uwagi |
|---|---|---|---|---|
| pierwszy build (ekran 8-bit CLUT, porównanie z kopią) | 6,6 | 149 ms | 9,9 ms | profil: `BlitPixelsWithLightmap` 44% |
| `Per-pixel Lighting=0` (oświetlenie płytkami jak w oryginale) | 24,0 | 41 ms | 9,6 ms | renderer płytek ~50%, porównanie 24% |
| wysyłanie bez porównania: prostokąty gry → VRAM przez MOVE16 | 26,6 | ~37 ms | ~6 ms | `DIABLO_PRESENT=DIFF` = stary sposób |
| `overlapped_memset.hpp`: kopie 2/4/8 B jako `move.w/l` (gcc robił 4× `move.b` przez stos) | **29,3** | 34 ms | 5,9 ms | binarka −70 KB |
| to samo, marsz po Tristram (przewijanie) | 25–32 | 31–39 ms | 5,9 ms | pojedyncze zacięcia ~300 ms – do zbadania |

Wnioski: Diablo przerysowuje cały widok co klatkę, więc porównanie z kopią nie oszczędza zapisu.
Dalej: renderer płytek (`dun_render.cpp`, sekcja `.text.hot`), domyślnie `Per-pixel Lighting=0` na Amidze.

### Loch (3.11.2026, Amiga 1, postać AR z zapisu użytkownika, poziom 2, walka ze szkieletami, postój)

Scena: `sh tools/scene_dungeon.sh run-prof` (zapis `save/single_1.sv`, kopia `single_1.sv.bak`;
`ENV:DIABLO_GOD` – postać nie traci życia).

| Zmiana | kl./s | klatka | Uwagi |
|---|---|---|---|
| pętle pikseli płytek w C (`DIABLO_BLIT_C`) | 14,3 | 69 ms | profil: `BlitPixelsWithMap` 39%, `BlitPixelsBlendedWithMap` 22% |
| te pętle w asm 68060 (`amiga_blit.c`, 4 piksele naraz) | **15,1** | 65 ms | `DIABLO_VERIFY_BLIT`: 6,4 mln wywołań, 0 różnic |

Pętle są ograniczone pamięcią, nie procesorem: ~23 ms na ~300 tys. pikseli (~7 taktów/piksel),
zapis do bufora w Fast RAM (alokacja linii 16 B przy zapisie + zapis zwrotny) i odczyt grafiki
płytek przy ~70 MB/s. Szybszy kod nie pomoże; trzeba rysować mniej pikseli.

### Ile obrazu naprawdę się zmienia (`ENV:DIABLO_CELLS`, komórki 32×32, co 10. klatka)

| Scena | zmienione komórki widoku po przesunięciu kamery | bez przesunięcia |
|---|---|---|
| Tristram, postój | 0–1% | 0–1% |
| Tristram, marsz | 5–17% | 33–91% |
| loch (poziom 3), postój | 5% | 5% |
| loch (poziom 3), marsz | ~29% średnio (w klatkach z ruchem ~50%; mało próbek) | 53% |

Wniosek: renderer komórkowy (niezmienione komórki kopiowane blitterem karty w VRAM z przesunięciem
kamery, CPU rysuje tylko zmienione) to jedyny duży zapas – do decyzji użytkownika.

### Renderer komórkowy, etap 1 (4.11.2026, Amiga 1) – `engine/render/amiga_cells.*`, docs/RENDERER_PLAN.md

Poprawność (`DIABLO_VERIFY_RENDER`, każda klatka porównana z pełnym rysowaniem): loch (postój,
walka, marsz) i Tristram (postój, marsz w obu kierunkach) – **0 różnic**, 0 zapisów poza obrazem,
lista pamięci cała (`Avail` po wyjściu). Skrypty: `tools/verify_render.sh`, `tools/verify_town.sh`.

Błędy upstream znalezione po drodze (niewidoczne przy pełnym rysowaniu, bo tam przycina tylko brzeg
ekranu):
- płytki z maską (przezroczyste ściany): przycięcie z lewej / prawej / od góry przesuwa granicę maski;
- trójkąty i trapezy płytek: przycięcie z obu stron w poziomie albo z góry i z dołu naraz pisze
  poza obszar – **poza bufor, niszczy pamięć** (Software Failure 0x81000005 w FreeMem, potem
  0x0100000C przy Avail). Takie płytki rysuję w całości do bufora 64×64 i kopiuję potrzebną część.
- Bufory sceny, kontrolny i pomocniczy mają marginesy 64 px z wzorem (raport `GUARD` w logu);
  bufor okna w msdl ma 64 wiersze zapasu nad i pod obrazem.

| Scena | pełne rysowanie (`DIABLO_FULL`) | komórkowy | zapis / przesunięcie / rysowanie / kopia ms | brudne |
|---|---|---|---|---|
| loch, walka w miejscu | 11,5 kl./s (86 ms) | **20,7 kl./s (48 ms)** | 6,4 / 0,3 / 22 / 7,5 | 26% |
| Tristram, postój (przy mieszkańcach) | 18,2 (54 ms) | **27,1 (36 ms)** | 6,9 / 0,3 / 13,6 / 7,3 | 17% |
| Tristram, marsz | 17,6 (56 ms) | 18,5 (53 ms) | 8,1 / 5,5 / 24 / 7,3 | 32% |

(Przed rendererem komórkowym: loch 15,1 kl./s / 65 ms.) Stały narzut ~20 ms: zapis 7, kopia sceny 7,
wysyłka 6 – etap 2 (VRAM, blitter) usuwa kopię i zmniejsza wysyłkę; zapis do przyspieszenia.

### Renderer komórkowy, etap 2 (4.11.2026, Amiga 1)

- 2a: nakładki (teksty, paski, kursor, okienka, blity SDL na okno) zgłaszają prostokąty;
  `DIABLO_VERIFY_PRESENT` (model karty w Fast RAM + odczyt VRAM): 0 różnic w mieście i lochu.
- 2b: widok na karcie przesuwa blitter (`BltBitMap`), do VRAM tylko brudne prostokąty sceny i
  nakładek; panel przez porównanie z kopią. Do karty 296 → 28–63 KB/klatkę. `DIABLO_PRESENT=FULL` – stary sposób.
- 2c: przy stojącej kamerze do bufora gry kopiowane tylko brudne prostokąty i miejsca nakładek
  (kopia 7,3 → 1,2–2,2 ms).
- 2d: scena w oknie na płótnie +256 px z każdej strony; ruch kamery = przesunięcie okna
  (5,5 ms `memmove` → 0), kopia tylko przy dojściu do brzegu płótna.
- Okresowe sprawdzanie marginesów w zwykłej grze dawało zacięcia 70 ms co ~2 s – tylko w trybie kontrolnym.

| Scena | przed rendererem | etap 1 | etap 2 (2a–2d) |
|---|---|---|---|
| loch, walka w miejscu | 15,1 kl./s (65 ms) | 20,7 (48 ms) | **24,7 (40 ms)** |
| Tristram, postój (przy mieszkańcach) | – | 27,1 (36 ms) | **35,4 (28 ms)** |

Rozkład w lochu (ms/klatkę): zapis 6,5, rysowanie 21,7 (26% brudnych), kopia 1,8, wysyłka 4,6.
Testy: `sh tools/verify_all.sh <etykieta>` (wszystkie kontrole + Avail), pomiary `tools/scene_dungeon.sh`,
`tools/scene_town.sh`.

### Marsz (4.11.2026, Amiga 1, `tools/scene_walk.sh`: przycisk trzymany w 4 kierunkach po 5 s)

- Tristram: pełne rysowanie 16–18 kl./s, komórkowe **21–26 kl./s** (w marszu kopia sceny do bufora gry 7,3 ms zostaje).
- Loch: przy marszu światło postaci przesuwa się co takt logiki (20/s) i brudne jest 85–95% komórek.
  Wtedy (lochy, kamera w ruchu, poprzednia klatka ≥ 80% brudna) rysowanie bezpośrednio do bufora gry,
  bez zapisu i kopii – jak przed rendererem; pierwsza klatka postoju odbudowuje skróty. Marsz w lochu 13–15 kl./s.
- Zacięcia w marszu: szukanie drogi (A*, do ~3000 pól, gdy cel za ścianą) – lista odwiedzonych pól z liniowym
  szukaniem w kubełkach; teraz indeks 256×256 i heurystyka w węźle: te same ścieżki
  (`tools/pathtest.sh`: 16 000 wyszukiwań, identyczny wynik), najdłuższa klatka 2,6 s → 0,47 s.

### Pomijanie płytek w przebiegu rysowania (4.11.2026)

Przebieg zapisu notuje dla każdej odwiedzonej płytki prostokąt jej prymitywów; przebieg rysowania
pomija płytki, które nie dotykają brudnego prostokąta (78–85% odwiedzin), razem z ich logiką.
`DIABLO_NOSKIP` wyłącza (A/B: loch 21,1 → 23,5 kl./s).

**Uwaga do pomiarów:** gra zapisuje stan przy wyjściu, więc wcześniejsze pomiary w lochu nie startowały
z tego samego stanu. Od teraz `tools/restore_saves.sh` przed każdym testem (kopie `single_0.sv.bak`,
`single_1.sv.bak`). Wyniki od stałego stanu (Amiga 1, ta sama binarka, `DIABLO_FULL` = pełne rysowanie
z narzutem zapisu):

| Scena | pełne | komórkowe |
|---|---|---|
| loch, walka w miejscu | 11,3 kl./s (88 ms) | **23,5 (42 ms)** |
| Tristram, postój | 17,8 (56 ms) | **40,1 (24 ms)** |
| Tristram, marsz | 17,3 (57 ms) | **29,2 (34 ms)** |

### Wyciek pamięci przy zamykaniu ekranu RTG (4.11.2026, Amiga 1, Warp 1260, AmigaOS 3.2.2.1)

Każde otwarcie i zamknięcie ekranu Picasso96 gubi w Fast RAM dokładnie tyle, ile ma bitmapa ekranu:
640×480×8 → 307 352 B, 640×480×16 → 615 592 B (niezależnie od `P96SA_Exclusive`, okna, palety,
blokady bitmapy). Test: `tools/amiga/screentest.c` (`screentest rundy krok głębia exclusive`), wynik
powtarzalny. To nie gra – dotyczy każdego programu z własnym ekranem RTG. Do zgłoszenia autorowi
oprogramowania RTG Warpa.

### Dalsze kroki wydajności (4.11.2026, noc, Amiga 1, stały stan gry)

| Zmiana | loch, walka | Tristram, postój | Tristram, marsz (`scene_walk`) |
|---|---|---|---|
| panel porównywany tylko w kolumnach zgłoszonych przez grę | 24,7 | 43,6 | – |
| przesunięcia zamiast dzieleń, `-fno-threadsafe-statics` | 25,2 | 44,3 | – |
| kopie bufora płytek przez `memcpy` (było: bajt po bajcie z `MULS.L` na bajt) | 26,4 | 46,4 | – |
| bez czyszczenia brudnych prostokątów (każdy piksel pokrywa jakaś płytka; kontrola: 0 różnic) | 27,3 | 47,4 | 30–35 |
| **komórki przecięte brzegiem widoku używane ponownie** (wcześniej zawsze brudne: pasy 15+17 wierszy co klatkę) | **42,2 (23 ms)** | **82,8 (12 ms)** | **38–52** |

- Komórki 16×16 sprawdzone: wolniej (więcej płytek przycinanych brzegiem prostokąta, droższy zapis).
- `RAM:cells-dump` (plik-flaga) → `RAM:cells.raw` z obrazem sceny i brudnymi prostokątami (`tools/vdiff.py`).
- Marsz w lochu (12–16 kl./s) zostaje słaby: światło postaci przesuwa się płynnie co takt (tak jak w oryginale,
  `ChangeLightOff`), więc prawie cały widok jest co klatkę inny. Ewentualna opcja „światło skokami co płytkę”
  zmieniłaby wygląd – do decyzji użytkownika.

### Noc 4.11.2026, ciąg dalszy (Amiga 1, stały stan gry, `tools/scene_*.sh`)

| Zmiana | loch, walka | loch, marsz (`scene_walk`, 4 kierunki) | Tristram, postój / marsz |
|---|---|---|---|
| stan wyjściowy (po „komórki przy brzegu”) | 42,2 | 15–20 | 82,8 / 38–52 |
| pętle asm przez wskaźnik ustawiany raz (bez opakowania C na każde wywołanie) | 43,7 | 16–21 | – |
| cała nieprzycięta płytka (kwadrat, połówki trójkątów) w jednym wywołaniu asm | 44,5 | 18–22 | – |
| FindPath: cel nieosiągalny rozpoznany wcześnie (te same ścieżki) | 44,5 | 20–37 | 82 / 30–50 |
| górne połówki trapezów w jednym wywołaniu | 44,4 | 20–37 | – |
| zapis płytek bez domknięcia rysującego (przebieg zapisu 7,7 → 6,7 ms) | – | – | **87 / 52–55** |
| wysyłanie szerokich obszarów do VRAM przez MOVE16 | **47,3 (21 ms)** | 20–40 | – |

- **FindPath:** przy trzymanym przycisku w stronę ściany / pustki cel bywa nieosiągalny, a A* przegląda wtedy
  ~1000–3000 pól (19 µs na pole). Po 128 polach: najpierw przeszukanie wstecz od celu (zamknięty cel ma mały
  obszar, budżet 512), potem przeszukanie wszerz od startu do głębokości, jaką A* może osiągnąć. Wszystko,
  co znajdzie A*, znajdzie też to przeszukanie, więc „nie ma” oznacza wynik 0 również dla A*. Czas szukania
  drogi w marszu po lochu 4,5–11 → 0,3–1,2 ms na klatkę. `tools/pathtest.sh` (długości 100 i 25, także ze
  sprawdzeniem już po 1 polu): ten sam skrót wyników.
- **Piksele w marszu po lochu** (liczniki w pętlach C, `ENV:DIABLO_BLIT_C`): ~200 tys. pikseli z tablicą
  światła + ~40 tys. przezroczystych na klatkę, czyli ~1× widok (mało nadrysowania). Pętle C są prawie tak
  szybkie jak asm: koszt to chybienia pamięci podręcznej (wiersze bufora gry, dane płytek), nie instrukcje.
- **Próbki profilera poza kodem gry** (teraz zapisywane, `tools/amiga/pcwho.c` szuka modułu po RomTagu):
  w marszu po Tristram ~20% czasu w jednej pętli `move.l (a0)+,(a1)+` – to CopyMem podmieniony przez
  warp.bootstrap; `memcpy` z libnix to zawsze `CopyMem`. Pomiar (`tools/amiga/copybench.c`): kopia
  640×352 Fast→Fast ~7 ms każdą metodą (CopyMem 7,0, pętla long 7,3, MOVE16 6,2 ms) – to przepustowość
  pamięci, nie sposób kopiowania. Kosztem jest sama kopia sceny do bufora gry przy ruchu kamery
  (Tristram, marsz: `copy 6,9 ms`).
- `BltBitMap` przesunięcia widoku 640×352 na sobie (`tools/amiga/shiftbench.c`): 0,35–0,45 ms w każdym
  kierunku – blitter Warpa nie jest wąskim gardłem.
- Statystyki (`DIABLO_STATS`) mają też wiersze: szukanie drogi (wywołania, pola, µs na klatkę, wczesne
  „brak drogi”), odczyty zegara na klatkę, a z `DIABLO_BLIT_C` liczbę pikseli pętli.
- **Własny `memcpy`** (`port/sdl/src/msdl_mem.c`, w miejsce libnix → `CopyMem`): w ustalonej walce 15% próbek
  było w pętli `CopyMem` (wiele krótkich kopii: wiersze płytek w buforze pomocniczym, częściowe kopie sceny,
  tekst). Test `tools/amiga/memcpytest.c` (długości 0–300 × przesunięcia 0–7 źródła i celu, strażniki):
  0 błędów (sprawdzone na Pi400/Amiberry; pierwsza wersja miała błąd w wyrównaniu i zawiesiła emulator –
  test zrobił swoje). Pomiar na Amidze 1 (po jej restarcie): walka 47,3 → **48,3** kl./s, Tristram postój 87,9,
  marsz 46–58, loch marsz 18–34. `memcpy` ma teraz 10,6% próbek, ale to już sama kopia (pamięć),
  nie wywołanie systemu. Kontrole obrazu: 0 różnic. (Wcześniej AmiAgent na Amidze 1 przestał odpowiadać
  o ~4:15, przed wgraniem tej wersji.)

## Dźwięk – rozpoznanie i test miksera (4.11.2026)

**Dane** (`diabdat.mpq`, nagłówki WAV wszystkich 824 plików z `effects.tsv` i listy muzyki):
efekty ~270 plików, prawie wszystkie 8-bit mono 22 050 Hz, razem ~7 MB (ładowane do pamięci); mowa
(`Stream`) ~530 plików, 8-bit (część 16-bit) 22 kHz, ~160 MB (czytane przy odtwarzaniu); muzyka lochów
16-bit mono 22 kHz 11–15 MB na utwór, miasto 16-bit stereo 25 MB. Pojedyncze wyjątki: 11 025 Hz
(`wallloop`) i 44 100 Hz (`snot03`).

**Plan:** zamiast SDL_audiolib (wątki, resampler, float) własna warstwa pod `engine/sound.h`
(`snd_play_snd` głośność + panorama, `sound_file_load`, `music_start`): Paula 2 kanały (lewy, prawy),
22 030 Hz (okres 161), pierścień buforów w Chip RAM wypełniany przez osobny proces (wzór: Settlers
`port/amiga_audio.c`); mikser programowy 8-bit; muzyka i mowa strumieniowo – główna pętla co klatkę
dokłada dane z MPQ do bufora ~1–2 s (czytnik MPQ nie jest dla dwóch zadań). Opcje `SOUND=PAULA|AHI`,
`NOSOUND`.

**Test miksera** (`tools/amiga/mixbench.c`, Amiga 1, 68060/100 MHz, bez gry; czas na sekundę dźwięku):

| Wersja | muzyka | + 2 głosy | + 4 | + 8 | + 16 |
|---|---|---|---|---|---|
| v1: sumy 16-bit L i P osobno, koniec próbki co próbkę, zapis bajtami do Chip RAM | 2,9% | 4,0% | 4,9% | 6,7% | 10,0% |
| v1, zapis do Fast RAM (dla porównania) | 0,7% | 1,7% | 2,5% | 4,3% | 7,6% |
| **v2:** L i P w jednym dodawaniu 32-bit (`acc = L·65536 + P`, tablica na głos), koniec próbki raz na odcinek, zapis do Chip RAM długimi słowami | **1,0%** | **1,7%** | **2,2%** | **3,0%** | **4,6%** |

Zapis bajtami do Chip RAM kosztował sam ~2,2% (44 tys. zapisów/s). Typowo 4–8 głosów: **2–3% CPU**
(w walce ~1–1,5 kl./s). Dalej możliwa pętla w asm.

### Dźwięk, krok 1: efekty (4.11.2026, Amiga 1)

- `port/sdl/src/amiga_sound.c`: proces „DevilutionX audio” (priorytet 10), Paula 2 kanały 22 030 Hz,
  4 bufory po 512 próbek w Chip RAM, 16 głosów, mikser v2 z `mixbench`. Głos po zajęciu wszystkich:
  najstarszy niezapętlony. `ENV:DIABLO_NOSOUND` wyłącza.
- `port/overlay/Source/utils/soundsample_amiga.cpp`: WAV → 8-bit ze znakiem mono 22 050 Hz przy wczytaniu,
  głośność i panorama według wzorów `soundsample.cpp`; `engine/sound_amiga.cpp` zamiast `sound.cpp`
  (bez SDL_audiolib); `effects.cpp` gry bez zmian. Build dalej z `NOSOUND` (bez wideo z dźwiękiem).
  Mowa na razie wczytywana w całości przy odtworzeniu; muzyka – krok 2.
- Walka w lochu: miksowanie **0,9–1,4% CPU, 0 przerw**; 47,2 kl./s z dźwiękiem, 48,5 bez
  (`tools/scene_dungeon_snd.sh`: z dźwiękiem poziom wczytuje się 20,6 s zamiast 16,3 s – efekty z MPQ –
  więc klawisze skryptu idą 4,3 s później, inaczej to inny moment walki). Odsłuch: do zrobienia.
- Zapisy testowe z Amigi 1 (`WorkSD:Games/DiabloWarp/save`, z kopiami `.bak`) leżą na PC w
  `private/amiga1-DiabloWarp/save` (przeprowadzka na Amigę 2).

### Dźwięk, krok 2: muzyka (4.11.2026, Amiga 2: A4000 + Warp 7060)

- Muzyka strumieniowo: `MusicPump` (`engine/sound_amiga.cpp`) wołany z `SDL_PumpEvents` czyta WAV z MPQ
  po kawałkach (do 8 KB) do pierścienia miksera (65 536 ramek, ~3 s, Fast RAM; jeden piszący – gra,
  jeden czytający – proces dźwięku). 16 bit → starszy bajt, stereo (intro, miasto) zostaje stereo;
  utwór w pętli. Głośność z opcji muzyki (wzór millibel jak w `soundsample.cpp`), `music_mute`.
- Walka w lochu (`scene_dungeon_snd.sh`): z efektami 47,6 kl./s, **z muzyką 45,9**; miksowanie 1,7% CPU,
  odczyt z karty SD ~2% (próbki „other tasks” 5 → 41 z 1109), 0 przerw, 0 niedopełnień pierścienia
  w grze (1 przy samym starcie intro).

### Paleta filmów (4.11.2026)

Zgłoszenie: film w menu (intro po 30 s bezczynności) przez pierwsze sekundy dobry, potem złe kolory.
Odtwarzacz (`storm_svid.cpp` `UpdatePalette`) zmienia kolory wprost w tablicy palety i ustawia tę samą
paletę ponownie; `SDL_SetSurfacePalette` w `msdl_surface.c` wychodziło wtedy od razu, bez nowej wersji,
a ekran ładuje paletę tylko przy zmianie wersji. Do tego dwie palety mogły mieć tę samą wersję. Teraz
jeden licznik wersji dla wszystkich palet i nowa wersja przy każdym `SDL_SetSurfacePalette` – zrzuty z
kilkunastu sekund intro mają dobre kolory. Filmy na ekranie 8-bit powiększone 2× (`storm_svid.cpp`: każdy piksel podwojony w poziomie, wiersz
powtórzony), na środku: intro 640×308 na całą szerokość.

### Dźwięk, krok 3: mowa strumieniowo (4.11.2026)

`utils/amiga_wavstream.hpp`: wspólny czytnik WAV z MPQ po kawałkach (muzyka – strumień 0, mowa –
strumień 1, każdy z własnym pierścieniem, głośnością i panoramą; inne częstotliwości niż 22 050 Hz
przez krok do najbliższej próbki). `SoundSample::SetChunkStream` czyta tylko nagłówek (długość dla
`GetSFXLength` – przewijanie tekstów zadań), plik otwiera się przy odtworzeniu; strumień mowy kończy się
sam, gdy plik się skończy i pierścień opróżni (`amiga_snd_stream_end`). Odsłuch: do zrobienia.

### Opcja „Light Steps” (światło skokami co płytkę) – 4.11.2026, Amiga 2

- Opcja gry w `[Graphics]` (menu ustawień): `Light Steps` = Off (0, domyślnie, jak oryginał) / On (1) /
  Auto (2); `Light Steps Below FPS` = 15/20/25/30 (domyślnie 20). Wł.: `UpdatePlayerLightOffset` nie
  przesuwa światła postaci w trakcie kroku – zostaje na płytce, z której postać wyszła, i przeskakuje przy
  dojściu (gra sama robi wtedy `ChangeLightXY` i zeruje przesunięcie). Auto: pomiar kl./s
  (`msdl_recent_fps10`, ostatnie 8 klatek) 0,5 s po początku marszu; poniżej progu – skoki do końca tego
  marszu (bez przełączania w tę i z powrotem).
- Renderer: tryb bezpośredni tylko gdy światło zmieniło się w ostatnich 120 ms (licznik
  `amiga_cells::lightChanges` z `ProcessLightList`); wcześniej zostawał do zatrzymania postaci.
- Pomiar marszu po lochu (4 kierunki): Off 24,1 / 20,3 / 28,6 / 18,4 kl./s, On 25,3 / 20,3 / 28,8 / 18,9 –
  **prawie bez zysku**. Klatek bezpośrednich 5–10 zamiast 40–74, ale renderer komórkowy w marszu przerysowuje
  30–45% komórek: zrzut (`RAM:cells-dump`) pokazuje brudny pas ~7 komórek (224 px) przy krawędzi, w którą
  wchodzi kamera (przy przesunięciu o 1–2 px), plus pełne przerysowanie po każdym przeskoku światła.
- Kontrola (`run-vall`, marsz, On): 1 klatka z 3 różnymi pikselami przy dolnej krawędzi widoku (229,337) –
  do zbadania razem z szerokim pasem przy krawędzi. Przy Off: bez zmian (0 różnic).

### Reguła krawędzi przy ruchu kamery (4.11.2026, Amiga 2)

Było: przy ruchu kamery wszystkie komórki przy krawędziach widoku w kierunku ruchu brudne (w izometrii ruch
jest po skosie, więc 4 krawędzie: ~25–28% widoku co klatkę). Jest: przebieg zapisu notuje komórki, których
dotyka prymityw przycinany przez grę niedokładnie przy krawędzi widoku (`RecordEdge`: ściany z maską
lewą/prawą przycięte którąkolwiek krawędzią, płytki przycięte w rogu); przy ruchu kamery brudne są tylko
komórki z takim prymitywem w tej lub poprzedniej klatce. Kontrola (`run-vall`, marsz w lochu ze
skokowym światłem i w mieście, ~70 klatek z ruchem kamery każda): 0 różnic.

| Scena | brudne komórki | kl./s |
|---|---|---|
| Tristram, marsz | 27% → 7–9% | 49–55 → 49–53 |
| loch, marsz, Light Steps On | 22–46% → 20–44% | 20–29 → 21–29 |

Kl./s prawie bez zmian: w marszu dominuje stały koszt trybu komórkowego – przebieg zapisu 6–7 ms (wszystkie
płytki widoku co klatkę), kopia sceny do bufora gry 3–6 ms, wysłanie na kartę 2–5 ms, logika ~4 ms,
dźwięk ~2 ms (~20 ms zanim cokolwiek się narysuje); w lochu ze skokami dodatkowo 2 pełne klatki na każdy
przeskok światła (klatka bezpośrednia, potem pełna w komórkach).

### Porównanie ze starym portem z Aminetu (4.11.2026, Amiga 2: A4000 + Warp 7060)

Stary port (DevilutionX 1.0.0, 22.02.2020, SDL1, ekran 640×480×8) uruchomiony z kopii programu w
`WorkSD:Games/DiabloOld` (`DevilutionX -n -f --data-dir WorkSD:Games/Diablo/` – dane z instalacji
użytkownika tylko do odczytu, zapisy obok kopii), postać ANDY z tego samego zapisu w lochu (poziom 2).
Licznik `-f` na zrzutach: **12–13 kl./s** w lochu (postój i walka, obraz w całości co klatkę).
Nowy port na tej samej Amidze: walka w lochu 48 kl./s (z dźwiękiem 46–47), marsz po lochu 18–30 kl./s.
Uwaga: stara wersja trzyma zapisy w `ENVARC:diasurgical/devilution/` (nie w swoim katalogu ani w
`--data-dir`) i zapisuje przy każdym wyjściu, także po Ctrl-C – pierwsze dwa przebiegi wczytały i
nadpisały postać użytkownika ANDY (stan po teście skopiowany na PC: `private/amiga2-envarc-after-test`).
Pomiar właściwy (za zgodą użytkownika): zapis AR (`single_1.sv.bak` nowego portu) wgrany jako
`ENVARC:…/single_0.sv`, ANDY zachowany w `RAM:` i przywrócony po teście. Ta sama scena walki ze
szkieletami co `scene_dungeon.sh`: **13 kl./s** (stary port, bez trybu bez obrażeń – AR ginie po ~30 s)
wobec **48 kl./s** nowego portu (46–47 z dźwiękiem).

### Próba: mniejsze światło postaci (4.11.2026, odrzucona)

Opcja „Light Radius” (100/85/70% promienia światła postaci, tylko w `ProcessLightList`): przy 70% koło
światła wyraźnie mniejsze, ale marsz po lochu bez zysku (100%: 23,2 kl./s średnio z 4 odcinków, 70%:
25,7, 70% + Light Steps: 24,3 – w granicach rozrzutu). Płytki poza kołem nie są czarne (światło otoczenia
poziomu), rysuje je ta sama pętla z tablicą światła; przy świetle zmieniającym się co takt i tak działa
rysowanie bezpośrednie całego obrazu. Opcja usunięta (decyzja użytkownika). Płytki w VRAM: za mało pamięci
karty (17–27 MB na poziom z wersjami światła).

### Porównanie ze starym portem, 3 sceny, 100 MHz (4.11.2026, Amiga 2: A4000 + Warp 7060)

Zegar ustawiony `WarpTool CPUFreqHz=100000000` (wcześniej na Amidze 2 było 105 MHz – wszystkie
wcześniejsze dzisiejsze pomiary na niej są przy 105). Stary port sterowany myszą przez
`tools/amiga/mouse.c` (zdarzenia `input.device`: `move`, `click`, `hold X Y MS`), kl./s z licznika `-f`
na zrzutach (`tools/fpscrop.py`); zapisy starego portu w `ENVARC:diasurgical/devilution` przed testem
skopiowane (PC: `private/amiga2-envarc-before-compare`, Amiga: `RAM:envarc-bak`) i po teście przywrócone
(porównane bajt po bajcie).

| Scena | stary port (2020) | nowy port (z dźwiękiem) |
|---|---|---|
| Tristram, postój | 17 | **91** |
| Tristram, marsz (4 kierunki × 5 s) | 13–17 | **47–66** |
| loch, marsz (AR, 4 kierunki × 5 s) | 10–13 | **17–26** |
| loch, walka ze szkieletami (AR) | 11–13 | **46** |

Stary port w mieście: nowa postać (zapisu WARP nie wczyta), więc inne miejsce startu. W lochu ta sama
postać AR i ten sam zapis; stary port bez trybu bez obrażeń (AR ginie po ~30 s walki).

Krok B (bez kopii sceny do bufora gry przy ruchu kamery): **nie robimy** (decyzja użytkownika 4.11.2026).
