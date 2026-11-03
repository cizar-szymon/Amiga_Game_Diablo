# Renderer komórkowy z blitterem – plan (3.11.2026)

## Dlaczego

- Gra co klatkę rysuje cały widok 640×352 od zera (`scrollrt.cpp` `DrawGame`): podłoga, potem
  rzędami płytek ściany, przedmioty, potwory, pociski, potem napisy, paski życia, kursor.
- W lochu ~61% klatki to dwie pętle pikseli płytek, ograniczone pamięcią (~7 taktów/piksel,
  Fast RAM ~70 MB/s). Asm dał tylko 6% (69 → 65 ms). Trzeba rysować mniej pikseli.
- Pomiar (`DIABLO_CELLS`, komórki 32×32, z przesunięciem kamery): Tristram postój 0–1%,
  marsz 5–17%; loch postój 5%, marsz ~50% w klatkach z ruchem (mało próbek).

## Pomysł

1. **Przesunięcie kamery znane dokładnie**, nie zgadywane: współrzędne świata lewego górnego
   rogu widoku = rzut izometryczny pierwszej płytki (`ViewPosition` + `tileShift`) minus `offset`
   z `CalcFirstTilePosition` (z przesunięciem przy chodzeniu). Różnica między klatkami = (dx, dy).
2. **Przebieg „zapis” sceny (bez pikseli):** ten sam `DrawGame`, ale prymitywy rysujące
   (`RenderTileFrame`, rodzina `ClxDraw*`, `world_draw_black_tile`, …) tylko liczą swój prostokąt
   na ekranie i dopisują swoje parametry (wskaźnik grafiki, klatka, tablica światła, maska, pozycja
   w świecie) do skrótu (hash) każdej komórki **świata** 32×32, którą pokrywają, w kolejności rysowania.
   Komórki są zakotwiczone w świecie, nie na ekranie, więc przy przesunięciu kamery komórka
   klatki poprzedniej i bieżącej to ta sama komórka.
3. **Komórka brudna** = inny skrót niż w poprzedniej klatce, albo nowo odsłonięta przy krawędzi.
   Działa bez wiedzy o tym, co się rusza: zmiana światła, animacja płomienia, potwór, pocisk –
   wszystko zmienia skrót. Prymityw, którego nie podłączymy, wyłapie tryb kontrolny (niżej).
4. **Nakładki po scenie** (napisy przedmiotów, pasek życia potwora, liczby obrażeń, kursor,
   wiadomości, górne części flaszek): ich prostokąty z tej i poprzedniej klatki też brudzą komórki,
   bo pod nimi trzeba odtworzyć scenę.
5. **Rysowanie tylko brudnych prostokątów:** brudne komórki łączone w kilka–kilkanaście
   prostokątów; dla każdego `DrawGame` z powierzchnią przyciętą do prostokąta (prymitywy już
   odrzucają to, co poza obszarem – koszt pętli po płytkach bez pikseli jest mały). Każdy
   prostokąt rysowany od podłogi w górę, więc nie potrzebuje starej zawartości (także
   przezroczyste ściany, które czytają tło, czytają tło narysowane w tej samej klatce).
6. **Pełne rysowanie (jak dziś)**, gdy: zmiana poziomu / wczytanie, automapa, otwarty panel
   boczny (ekwipunek, postać, zadania, księga, sklep, skrzynia), zmiana palety (ściemnianie),
   śmierć / pauza / menu gry, opcja Zoom, `RedrawEverything()`, za dużo brudnych komórek
   (np. > 60%: wtedy taniej narysować całość).

## Etapy (każdy kończy się pomiarem na Amidze i wpisem w PORTING.md)

### Etap 1 – przyrostowo w Fast RAM (bez blittera)
- Bufor klatki w Fast RAM zostaje pełny (gra go czyta: kursor zapamiętuje tło, przezroczyste
  okienka, czerwone tło śmierci).
- Przesunięcie: kopia bufora o (dx, dy) przez CPU (~5 ms dla 640×352; tylko gdy kamera się rusza).
- Rysowanie tylko brudnych prostokątów, potem wysyłka jak dziś (MOVE16 całego widoku, ~6 ms).
- **Tryb kontrolny `DIABLO_VERIFY_RENDER`:** po rysowaniu przyrostowym pełne `DrawGame` do
  drugiego bufora i porównanie widoku piksel w piksel; różnice → log (współrzędne, komórka,
  ostatnie prymitywy) + zrzut obu obrazów do `RAM:`. Cel: 0 różnic w Tristram i w lochu
  (postój, marsz, walka, zaklęcia, przedmioty na ziemi, otwieranie drzwi).
- Spodziewany wynik: postój w lochu ~65 → ~15–20 ms; marsz w lochu mniej (połowa komórek).

### Etap 2 – VRAM i blitter karty
- Przesunięcie widoku na karcie: `BltBitMap` VRAM→VRAM (~0,5 ms zamiast 5 ms kopii CPU),
  dwie bitmapy w VRAM (tylna i wyświetlana), bez rozrywania obrazu.
- Do VRAM idą tylko brudne prostokąty i nakładki (zamiast 288 KB co klatkę).
- Bufor w Fast RAM: przesuwany „za darmo” przez zmianę początku (większe płótno z marginesem,
  wskaźnik powierzchni przesuwany, kopia tylko przy dojściu do brzegu).
- Ten sam tryb kontrolny; dodatkowo porównanie odczytu VRAM z buforem (tylko w trybie testowym).

### Etap 3 – opcjonalnie
- Panel dolny wysyłany tylko przy zmianie; mniejsze komórki (16×16) jeśli pomiar pokaże zysk;
  sprytniejsze światło w marszu (dziś zmiana światła płytki brudzi jej całą wysokość ściany).

## Ryzyka

- Efekty uboczne w rysowaniu: `DrawGame` uruchamiany kilka razy na klatkę (zapis + prostokąty)
  musi być czysty. Do sprawdzenia: `UpdateMissilesRendererData` (raz na klatkę – wyciągnąć przed
  przebiegi), liczniki animacji, `ClxDraw` z pamięcią podręczną (`ClearClxDrawCache`), lua.
- Sprite'y wychodzące poza swoją płytkę (wysokie ściany, duże potwory) – obsłużone, bo skrót
  liczony jest po rzeczywistym prostokącie prymitywu, nie po płytce.
- Zmiany w kodzie gry: `scrollrt.cpp`, `dun_render.cpp`, `clx_render.cpp` (haczyki przebiegu
  „zapis”), przez `port/apply_port.py` – większe niż dotąd, ale zamknięte w `#ifdef __amigaos__`.
- Koszt przebiegu „zapis”: pętla po ~400 płytkach i ~kilkudziesięciu sprite'ach, bez pikseli –
  szacunkowo 1–3 ms; zmierzymy.

## Pliki

- `port/sdl/src/msdl_cells.c` – zostaje jako pomiar; nowy `port/overlay/Source/engine/render/amiga_cells.cpp/.hpp`
  (komórki świata, skróty, brudne prostokąty, tryb kontrolny).
- Zmiany: `scrollrt.cpp` (`DrawView`/`DrawGame`: przebiegi, pełne rysowanie w wymienionych
  przypadkach), `dun_render.cpp` i `clx_render.cpp` (haczyk na wejściu prymitywów).
- Etap 2: `port/sdl/src/msdl_video.c` (bitmapy VRAM, blit przesunięcia, wysyłka prostokątów).

## Decyzje użytkownika (3.11.2026)

- Najpierw etap 1 (Fast RAM + tryb kontrolny), potem etap 2 (VRAM i blitter).
- Przy otwartym ekwipunku, automapie, sklepie, księdze, menu gry: pełne rysowanie jak dziś.
- Commit stanu przed przebudową (lokalnie).
