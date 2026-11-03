# CLAUDE.md — DevilutionX (Diablo) na Amigę 68k

@C:/Workprojects/AmigaGAMES/PORTING_PLAN.md
@C:/Workprojects/AmigaGAMES/AmiAgent/AMIAGENT.md

## Decyzje (3.11.2026, zaakceptowane przez użytkownika)
- Sposób D (port ze źródeł), baza: **aktualny upstream** diasurgical/devilutionX
  (czysta kopia w `upstream/devilutionX`, poza gitem).
- **Natywny backend od razu:** własny podzbiór SDL2 dla Amigi (`port/sdl`, na bazie
  mini-SDL2 z `../Dune/port/sdl`), bez prawdziwej biblioteki SDL2.
- **Tylko Amiga.** Upstream na PC służy tylko jako wzorzec.
- Testy: **Amiga 1** (A1200 + Warp 1260, 192.168.5.115, domyślny `C:/amiga-dev/amiagent.cfg`;
  3.11.2026 Amiga 2 niedostępna), katalog `WorkSD:Games/DiabloWarp`. Dane czytane z
  `WorkSD:Games/Diablo/diabdat.mpq` (`--data-dir`). **`WorkSD:Games/Diablo` to instalacja
  użytkownika (stary port, jego zapis `single_0.sv`) – niczego tam nie zapisywać.**
- Stan prac i liczby: `docs/PORTING.md`, bieżące notatki: `notes/progress.md`.
- Stary port z Aminetu (2020) leży w `AmigaPortOrginal/` (poza gitem); jest tam też
  `diabdat.mpq` (517 MB, wersja CD). Danych gry nigdy nie commitować.

## Cel projektu
Uruchomić i dopracować DevilutionX (otwartoźródłowy, odtworzony silnik Diablo 1)
na mojej Amidze 68k z kartą RTG. Interesuje nas wydajność na 68060 bez Vampire.

## Sprzęt docelowy
- Amiga 4000, Elbox Mediator (busboard PCI)
- CPU: Warp 7060 (68060). Brak Vampire, więc **brak AMMX**
- Grafika: RTG (Picasso96)
- AmigaOS 3.2.2.1
- Dodatkowo: karta PowerPC na PCI (opcja WarpOS, niesprawdzona)
- TODO: karta graficzna, rozdzielczość i głębia kolorów, ilość Fast RAM

## Stan wiedzy o istniejących portach
- **Port 68k (Aminet):** `game/actio/DevilutionX.lha`, wersja 1.0 z 22.02.2020.
  Autorzy: Artur Jarosik, Marlon Beijer. Optymalizacje ASM/AMMX: Samuel Devulder.
  Readme zaleca Vampire do płynnej gry. Nie obsługuje Hellfire ani Battle.net.
- **Fork Amiga:** https://github.com/AmigaPorts/devilutionX
- **Upstream:** https://github.com/diasurgical/devilutionX. Ma oficjalny workflow
  CI `amiga-m68k.yml` (GitHub Actions), więc nowsze buildy 68k są budowane z
  aktualnego kodu.
- Upstream v1.5.2 zawiera poprawkę Amigi: błędne renderowanie obiektów za
  przezroczystymi elementami.
- Istnieją też: port pod WarpOS (PPC) oraz build dla AmigaOS 4.1
  (DevilutionX 1.4.1, SDL 1.2, z obsługą Hellfire).

## Uruchamianie
- `-x` uruchamia w oknie
- `-n` pomija filmy z logo
- Dane gry nie są dołączone. Wymagany `DIABDAT.MPQ` (płyta CD lub GOG) w katalogu
  z plikiem wykonywalnym. Do testów można użyć wersji shareware (link w readme
  z Aminetu).

## Ważne zasady
- Kod z AMMX nie zadziała na 68060. Każda ścieżka AMMX musi mieć zwykły
  fallback 68k (lub własną optymalizację pod 060).
- Testy na prawdziwej Amidze i/lub w UAE.
- Nie zgadywać komend budowania: zapisywać je w sekcji niżej, dopiero gdy
  zostaną sprawdzone.

## Budowanie
TODO: toolchain (np. bebbo m68k-amigaos-gcc / Docker), wersja SDL, komendy cmake,
flagi CPU (`-m68060`), sposób przenoszenia plików na Amigę.

## Do uzupełnienia
TODO: reszta informacji od użytkownika.
