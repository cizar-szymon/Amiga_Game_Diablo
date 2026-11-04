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
