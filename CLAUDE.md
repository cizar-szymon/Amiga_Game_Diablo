# CLAUDE.md — DevilutionX (Diablo) na Amigę 68k

@C:/Workprojects/AmigaGAMES/PORTING_PLAN.md
@C:/Workprojects/AmigaGAMES/AmiAgent/AMIAGENT.md

## Decyzje (3.11.2026, zaakceptowane przez użytkownika)
- Sposób D (port ze źródeł), baza: **aktualny upstream** diasurgical/devilutionX
  (czysta kopia w `upstream/devilutionX`, poza gitem).
- **Natywny backend od razu:** własny podzbiór SDL2 dla Amigi (`port/sdl`, na bazie
  mini-SDL2 z `../Dune/port/sdl`), bez prawdziwej biblioteki SDL2.
- **Tylko Amiga.** Upstream na PC służy tylko jako wzorzec.
- Od 4.11.2026 (rano) testy na **Amidze 2** (A4000 + Warp 7060): `export AMIAGENT_CONFIG=C:/amiga-dev/amiagent-2.cfg`
  przed skryptami z `tools/`; ten sam układ katalogów (stan z 4.11.2026: dane w `WorkSD:Games/Diablo`).
  Zapisy testowe: `private/amiga1-DiabloWarp/save` (kopie z Amigi 1).
- Testy (wcześniej): **Amiga 1** (A1200 + Warp 1260, 192.168.5.115, domyślny `C:/amiga-dev/amiagent.cfg`;
  3.11.2026 Amiga 2 niedostępna), katalog `WorkSD:Games/DiabloWarp`.
- Od 14.11.2026 na Amidze 1 stary port `WorkSD:Games/Diablo` **usunięty** (decyzja użytkownika; jego zapis
  i ini w `private/amiga1-old-Diablo`). Gra użytkownika to wydanie w `WorkSD:Games/DiabloWarp` (ikony
  devilutionx i Hellfire), dane w `WorkSD:Games/DiabloWarp/data` (diabdat, pl, hellfire, hf*.mpq).
  Skrypty `tools/amiga/run-*`: `GAME=DIABLO --data-dir WorkSD:Games/DiabloWarp/data/`, program
  `devilutionx-test`. Na Amidze 2 tak samo (14.11.2026): wydanie 0.48 w `WorkSD:Games/DiabloWarp`,
  dane z Hellfire w `data/`, stare `WorkSD:Games/Diablo` usunięte (zapis w `private/amiga2-old-Diablo`).
  AmiAgent na Amidze 2 potrafi zawiesić się przy kilku dużych `put` w jednym przebiegu – wgrywać po jednym.
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
