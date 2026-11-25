# Postęp (notatki robocze)

## 3.11.2026

- Repo `Diablo/` (main), upstream master w `upstream/devilutionX`, stary port w `AmigaPortOrginal/`.
- Kompilator: `amigadev/crosstools:m68k-amigaos-gcc10` = gcc 16.2.1b, CMake 4.4.5.
- Build wzorcowy (`tools/build_ref.sh`, wolumin Dockera `diablo-ref-build`) → `build/ref/devilutionx`.
  Na Amidze 1 od razu Software Failure 0x0100000C (lista pamięci Exec zniszczona) – patrz PORTING.md.
- Amiga 1: `WorkSD:Games/DiabloWarp/` (nasz), `ref/` = build wzorcowy, `run-ref` skrypt.
  `WorkSD:Games/Diablo/` = instalacja użytkownika, tylko odczyt (`diabdat.mpq`).
- Potrzebne symbole SDL: `notes/sdl_symbols_needed.txt` (157, z `nm -u` obiektów builda wzorcowego).

## Decyzje techniczne do backendu (moje, do potwierdzenia wynikami)

1. **Prawdziwe nagłówki SDL2** (z `_deps/sdl2-src/include` upstream), własna implementacja 157 funkcji.
   Gra używa setek stałych i makr SDL (SDLK_, SDL_SCANCODE_, SDL_PIXELFORMAT_, zdarzenia), więc
   własne nagłówki to dużo pracy i ryzyko niezgodności. Kod z `../Dune/port/sdl` przenosimy na
   prawdziwe struktury (`SDL_Surface`, `SDL_PixelFormat`, `SDL_Event`).
2. CMake: `-DDEVILUTIONX_SYSTEM_SDL2=ON -DSDL2_DIR=<port/sdl/cmake>`; tam `SDL2Config.cmake`
   z targetem `SDL2::SDL2` (IMPORTED STATIC, `libmsdl.a` budowana wcześniej).
3. Obraz: ścieżka bez renderera – `SDL_GetWindowSurface` (8-bit, Fast RAM) +
   `SDL_UpdateWindowSurfaceRects` → ekran 8-bit CLUT (`msdl_video_present8` z Dune).
   Renderer/tekstury tylko jako zaślepki.
4. Dźwięk: zamiast SDL_audiolib (Aulib, wątki, Speex, float) własna wersja
   `utils/soundsample.cpp` / `engine/sound.cpp` pod `#ifdef __AMIGA__` na mikser Paula.
   Pierwszy build z `NOSOUND`.
5. SDL_image (PNG: zrzuty, `utils/png.h`) – na start wyłączone / zaślepki.
6. Zmiany w kodzie gry: nakładka `port/overlay/` + łatki `port/patches/`, nakładane w Dockerze na
   czystą kopię upstream (jak w UFO), żeby różnice były czytelne.
7. Tryb testowy przydziału pamięci ze strażnikami (po awarii builda wzorcowego).

## Następne kroki

- `port/sdl`: przenieść na prawdziwe nagłówki, zaślepki dla brakujących funkcji, `libmsdl.a`.
- `tools/build.sh`: upstream + overlay + `NOSOUND` + nasz SDL2 → `build/amiga/devilutionx`.
- Pierwszy start na Amidze 1 (menu z `-n`), potem obraz 8-bit, wejścia, dźwięk.

## 14.11.2026 – Diablo: Hellfire

- Pliki Hellfire (GOG): `hellfire/` na PC (poza gitem, `*.mpq` w .gitignore); na Amidze 1
  w `WorkSD:Games/DiabloWarp/data/` (hellfire, hfmonk, hfmusic, hfvoice). Amiga 2 nie odpowiadała na ping.
- Przyczyna braku Hellfire: luźny mod `mods/hf` szukany tylko w PrefPath (`save/`) i BasePath (`data/`),
  a w paczce `mods/` leży obok programu. Łatka w `apply_port.py` (assets.cpp): też `PROGDIR:mods/<mod>/`.
  Objaw w logu: `Missing: mods/hf`, `Asset not found: lua\mods\hf\init.lua`, w ini `hf=1`, a logo DIABLO.
- Tool Type / argument `GAME=HELLFIRE|DIABLO` (→ `--hellfire` / `--diablo`), w ikonie jako podpowiedź.
- Sprawdzone na Amidze 1 (`tools/amiga/run-hellfire`): intro Hellfire, menu HELLFIRE, klasy z Mnichem.
  Nie sprawdzone: rozgrywka, Ul / Krypta, wydajność.
- Wysyłanie dużych plików przez AmiAgent: domyślny limit klienta 30 s za mały (plik dochodzi, klient
  zgłasza timeout); raz `hfmusic.mpq` stanął na 15 min, ponowienie poszło w 27 s.
- Dwie ikony: `devilutionx` (GAME=DIABLO) i projekt `Hellfire` (Default Tool devilutionx, GAME=HELLFIRE);
  `amiga_wb.c` czyta Tool Types programu i ikony projektu, ostatnie GAME= wygrywa. Bez wymuszonego trybu
  `--hellfire` zostawia w diablo.ini `hf=1` (mod włączony na stałe) – stąd „zawsze Hellfire” u użytkownika.
  Sprawdzone przez `wbrun` na Amidze 1: Hellfire-test → ekran Hellfire, devilutionx-test → menu DIABLO.
- Filmy (pomiar w logu `video ...`, Amiga 1, 060): Diablo logo 320×156, 30 ms dekodowania na klatkę 66 ms,
  229/239 pokazanych; Hellfire logo 320×240, ~22 KB danych na klatkę, 97–103 ms dekodowania → upstream
  pokazał 1/128 (pomija spóźnione klatki i ich dźwięk). Pliki w MPQ nieskompresowane (flagi 0x80000000),
  więc koszt to sam dekoder Smacker. Teraz na Amidze bez pomijania: 129/128 pokazanych, 15,6 s zamiast
  8,5 s (zwolnione ~1,8×). Dalej: profil dekodera (libsmacker), cel < 66 ms na klatkę.
- Dekoder filmów (libsmackerdec w `port/overlay/3rdParty`): czytnik bitów z bufora + tablica kodów Huffmana.
  Hellfire logo 97 → 37 ms/kl., Diablo 30 → 13 ms, sumy kontrolne bez zmian; długie intro Hellfire
  539 kl. w 36,3 s (nominalnie 35,9). Szczegóły w PORTING.md. Dźwięku filmów nie sprawdzałem.
- Dźwięk filmów (nie było go wcale – NOSOUND): strumień 2 miksera Pauli, zapas 4096 ramek, dźwięk jako zegar,
  pomijanie klatek spóźnionych o > 2 klatki. Intro Hellfire 36,0 s, 0 niedopełnień, 411/540 klatek;
  logo Hellfire 98/128; Diablo 240/240. Pierścienie strumieni zwalniane przy wyjściu (był wyciek).
  Odsłuch przez użytkownika: ok (25.11.2026).
- 0.48 wydane (commit f2b172b), zainstalowane na Amidze 1 w WorkSD:Games/DiabloWarp (ikony devilutionx, Hellfire).
  Usunięte na prośbę użytkownika: stary port WorkSD:Games/Diablo (+ Diablo.info; zapis i ini w
  private/amiga1-old-Diablo), Test048, ikony testowe. Skrypty run-* czytają dane z DiabloWarp/data, GAME=DIABLO.

## 24.11.2026 – automapa (Amiga 1)

- Zgłoszenie: otwarta mapa spowalnia grę (też przy samym chodzeniu). Szczegóły i liczby: PORTING.md, „Automapa”.
- Lista postaci na Amidze 1: QQQ, QQQ (postacie użytkownika, `single_2/3.sv`), AR (y=323), WARP (y=349);
  wszystkie skrypty `tools/scene_*.sh`, `verify_*.sh` klikają teraz 323 (AR) / 349 (WARP).
- Błąd w moim teście: dwa pierwsze przebiegi kliknęły 270 i grały postacią użytkownika z `single_3.sv`
  (~30 s chodzenia, `DIABLO_GOD`); gra zapisała ją przy wyjściu. Kopii sprzed testu nie było.
  `single_0/1.sv` i `diablo.ini` przywrócone z `private/amiga1-save-20261124`.
- Awaria `trap 5` (dzielenie przez zero) w nowych wierszach statystyk mapy przy zamkniętej mapie –
  poprawione (klamry), Amiga 1 zresetowana przez użytkownika.

## 25.11.2026 – 0.50

- Napisy automapy z pamięci podręcznej, zwolnienie zasobów `SDL_Delay` (80 B), wynik zgłoszenia w logu;
  test sprzątania: 307 352 B na uruchomienie (sterownik Warpa), Chip 0. Szczegóły: PORTING.md, „Po wydaniu 0.49”.
- Użytkownik: 0.50 pobrane przez WARP HUB z RP4 (pełna paczka) działa, gra z otwartą mapą działa dobrze.
- Użytkownik: 0.50 zainstalowane przez WARP HUB także na Amidze 2 i sprawdzone; odsłuch dźwięku (muzyka, mowa, efekty na Pauli) – ok.
- Postęp ładowania (napis z procentami): użytkownik uznał obecny pasek DevilutionX za wystarczający – bez zmian.

## 25.11.2026 – 0.51

- F10: okno zgłoszenia w ramce (port/overlay/Source/amiga_report_ui.cpp) zamiast paska rozmowy; gra wstrzymana bez ramki Pause.
  Użytkownik sprawdził na Amidze 2: działa dobrze. Szczegóły: PORTING.md, „F10: okno zgłoszenia w ramce”.
- Testowe zgłoszenie nr 21 usunięte z serwera gier; pełna paczka 0.51 wysłana na RP4 (po przywróceniu sieci).
