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
