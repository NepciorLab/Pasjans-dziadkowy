# Zmiany w silniku AI (przyspieszenie „myślenia”)

Pliki: src/game.h, src/main.cpp. Reszta bez zmian.

2. Opisy ruchów (`MoveHint::desc`) nie są już budowane w generatorze (`allCandidateMoves(seen, withDesc=false)`); nikt ich nie używał.
   Do tego: `hasTableOrFoundationMove()` liczone raz na wywołanie, szybsze `isGlobalLowestRank()` (z rozmiarów stosów).
3. Wyszukiwanie chodzi po jednej planszy w miejscu (`applyMoveInPlace`/`undoMoveInPlace`) zamiast kopiować `GameState`
   na każdy węzeł; hash planszy liczony raz na dziecko; poprawiony iterator po `rehash` w tablicy transpozycji.
   Wynik identyczny bit w bit z wersją poprzednią (sprawdzone sumą kontrolną 3 zestawów partii).
4. Anulowanie: `g_bestMoveAbort` (main.cpp) przerywa wyszukiwanie pozycji, która już się zdezaktualizowała
   (ruch, cofnięcie, zmiana ustawień). Wątek w tle działa z priorytetem below-normal.
5. Budżet węzłów jest teraz osobny dla każdego kandydata z korzenia (max(8000, min(30000, 90000/n))),
   zamiast jednej puli 60000 zjadanej przez pierwszych kandydatów (późniejsi dostawali pustą ocenę).
6. Kandydaci z korzenia liczeni równolegle (Win32 CreateThread, do 16 wątków) przy głębokości >= 6
   (czyli przy wolnej kolumnie w trybie wolnych miejsc). Wynik nie zależy od liczby wątków ani ich kolejności.
   Zmienna środowiskowa PASJANS_AI_THREADS=1..16 wymusza liczbę wątków (1 = sekwencyjnie).
   Uwaga: wspólna tablica transpozycji między kandydatami zniknęła, więc niektóre decyzje mogą się różnić od starej wersji.
7. Wyszukiwanie nowej pozycji startuje od razu po zastosowaniu ruchu (już wcześniej, w trakcie animacji) — teraz dodatkowo
   nie zajmuje rdzeni, gdy pozycja zdąży się zmienić.

Budowanie: `build.ps1` (Zig c++ jako kompilator mingw-w64, patrz nagłówek) albo oryginalne `build.sh`.
tools/bench.cpp: test wydajności/regresji silnika bez GUI (`bench.exe gry kroki free|king głębokość`).

# Licznik Samograju (main.cpp)
Na wstążce, na prawo od przycisku Ustawienia, trzy linie jedna pod drugą: Rozdań, Wygranych, Zwycięstw (procent).
Liczy rozdania rozegrane do końca (wygrana, brak ruchów lub utknięcie silnika) w bieżącym przebiegu Samograju / Samograju bez końca.
Zerowany przy każdym włączeniu trybu automatycznego; po zatrzymaniu zostaje widoczny do następnego włączenia.

# Tryb „Tylko król” – zatrzymywanie automatu (main.cpp)
Symulacja pętli auto-play (tools/sim.cpp, 300 partii): ok. 70% zatrzymań to prawdziwe ślepe zaułki (rezerwa pusta, brak legalnych ruchów – rozdanie przegrane),
reszta to sytuacje, gdy gra jeszcze widzi ruch, ale silnik odrzuca wszystkich kandydatów regułami anty-pętlowymi (najczęściej: jedynym ruchem jest cofnięcie
własnego ruchu sprzed kilku posunięć, np. odłożenie karty z powrotem na stos po ruchu „buforowym”). Poprawki:
- doAutoMove(): ostatnia deska ratunku – gdy wszyscy kandydaci odrzuceni, przyjmuje ruch, którego WYNIKOWA plansza nie wystąpiła w ostatnich 20 stanach
  (reguła „ruch odwrotny” jest tylko skrótem dla sprawdzenia całej planszy).
- Samograj bez końca (Shift+1): gdy silnik nie ma już nic do zagrania, a gra nadal widzi jakiś ruch (martwe rozdanie), rozdanie jest zaliczane jako
  przegrane i rozdawane jest następne – zamiast zatrzymywać całą serię. (Rozdanie bez ani jednego ruchu nadal zatrzymuje serię, żeby uniknąć pętli.)
- Licznik rozdań/wygranych/procentu jest widoczny tylko w trybie automatycznym.

# Ikony i dźwięki wbudowane w exe (res/app.rc, sound.h, main.cpp)
- res/icon.ico wygenerowana z dostarczonej ikony (256/128/64/48/32/24/16 px) – ikona aplikacji i okna.
- Obrazki przycisków (res/icons/*.png) i dźwięki domyślne (res/sounds/*.wav) są zasobami RCDATA. Plik o tej samej nazwie obok exe nadal ma pierwszeństwo.

# Głębokość przewidywań
Wartość spoza zakresu nie resetuje się już do domyślnej: za duża jest ograniczana do maksimum (MAX_SEARCH_DEPTH = 20, było 12), za mała/nieczytelna dostaje domyślną. Zmiana w main.cpp (ustawienia + wczytywanie ini) i game.h.

# Ikona okna
res/app.rc: nazwa zasobu ikony musi być bez cudzysłowu (APPICON ICON ...) – kompilator zasobów Zig (zig rc) zapisywał ją wraz z cudzysłowami, przez co LoadImage nie znajdował ikony i okno miało ikonę systemową.

# LostNumbers / Solver (main.cpp, solver.h)
- LostNumbers.csv (format jak WonNumbers.csv): trafiają tu rozdania zakończone brakiem ruchów lub utknięciem automatu (nie: porzucone ręcznie „Nową grą”).
  Rozdanie, które później zostanie wygrane, znika z LostNumbers.csv / Unsolvable.csv.
- Przycisk „Solver” (między Samograj a Cofnij): lista rozdań z LostNumbers.csv (zaznaczanie wielu: Ctrl/Shift lub „Zaznacz wszystkie”),
  wczytanie pierwszego na stół, pytanie „Czy mam spróbować znaleźć rozwiązanie?”, potem okno postępu (czas etapu, próby, bieżąca próba – ruch i węzły,
  najlepszy dotychczasowy postęp: króle na miejscu / ułożone karty, prędkość) z przyciskiem „Przerwij”.
- Limit czasu 5 minut na etap; po nim pytanie „kontynuować?” z odliczaniem 10 s (brak odpowiedzi = dalej). Nierozwiązane trafiają do Unsolvable.csv.
- Rozwiązanie: Solved<numer>.dat obok exe (Akcje → Wczytaj grę z pliku; kolejne ruchy przyciskiem „Ponów”), numer przechodzi z LostNumbers.csv do WonNumbers.csv.
- solver.h: przeszukiwanie z nawrotami na znanym rozdaniu (zna kolejność rezerwy, czego automat celowo nie robi), restarty z rosnącym limitem węzłów
  (120 tys. → 2 mln, ×1,35), losowy szum w kolejności ruchów od 2. próby, co druga próba z limitem długości linii, tablica odwiedzonych pozycji,
  praca na kilku wątkach; znaleziona linia jest skracana (usuwanie objazdów) i weryfikowana zasadami gry.
  Zamiast rosnącej „głębokości przewidywań” (nieskutecznej, bo silnik automatu i tak ogranicza limit węzłów) rośnie limit węzłów i różnorodność prób.
- Format zapisu gry v12: liczniki cofnij/ponów 16-bitowe; MAX_UNDO_HISTORY = 1000 (game.h).
- tools/solvetest*.cpp: testy solvera bez GUI (np. solvetest_mt 15 free 100 20 6).

# Solver – poprawki
- Po wyborze rozdań szukanie rusza od razu (bez pytania „Czy mam spróbować…”); pierwsze wybrane rozdanie nadal ląduje na stole.
- Okno wyboru: pole „Ziarno” + wybór trybu + „Dodaj do listy” (Enter w polu też dodaje); rozdania z Unsolvable.csv są na liście na czerwono, pogrubione, z dopiskiem [nierozwiązywalne]; wpisane ręcznie na niebiesko.
- Pytanie po 5 minutach: tekst w jednym akapicie (zawija się sam, bez ucięcia); domyślna odpowiedź (przycisk domyślny i brak odpowiedzi po 10 s) = kontynuuj poszukiwania. „Nie, następne” przenosi rozdanie do Unsolvable.csv.
- SolverState.csv: zapamiętany stan poszukiwań (liczba prób, przeszukane pozycje, czas, najlepszy postęp) – zapis przy przerwaniu, na końcu etapu i przy odpuszczeniu rozdania; kolejne poszukiwania tego samego ziarna wznawiają numerację prób (nowe losowe kolejności i większe limity węzłów zamiast powtarzania wczesnych prób). Wpis znika po znalezieniu rozwiązania.
- Solver: wczytanie rozdania na stół jest ciche (newGame z silent=true), bez dźwięku nowego rozdania.

# Solver – lepsza skuteczność na trudnych rozdaniach (solver.h)
Diagnoza (nr 883788327, tryb „Dowolna karta”): silnik przeszukiwania w praktyce prawie nigdy się nie cofał — przy takim rozgałęzieniu ruchów
pełne wyczerpanie poddrzewa (jedyny warunek cofnięcia) niemal nigdy nie zachodziło, więc DFS był w istocie jednym bardzo długim zachłannym
marszem z restartami, a nie prawdziwym przeszukiwaniem z nawrotami. Zmiany:
- Cięcie za brak postępu (stallLimitForAttempt): jeśli od N ruchów żaden nie pobił najlepszego dotąd wyniku NA TEJ ŚCIEŻCE, gałąź jest
  traktowana jak ślepy zaułek (realne, częste cofanie). N rośnie z numerem próby, osobno dla prób „płytkich” (szybkie, typowe rozdania)
  i „głębokich” (od razu hojny limit – to one mają szansę na trudne rozdania).
  Miara postępu (solverPotential) liczy nie tylko karty na stosach, ale i długość poprawnie ułożonych spodów kolumn – żeby chwilowe
  zdjęcie karty ze stosu z powrotem na stół (czasem konieczne w trybie „Tylko król”) nie wyglądało jak regres.
- Ruch bufor (stos→kolumna) mniej karany (-50 zamiast -200) – bywa integralną częścią strategii, nie tylko ostatecznością.
- Próby budują teraz na najlepszej dotąd osiągniętej linii (Shared::bestPrefix, część prób – reszta nadal startuje od zera dla różnorodności)
  zamiast każda od nowa wyprowadzać ten sam początek. solveDeal()/Result mają teraz bestEffort/bestPotential – linię i jej ocenę nawet
  gdy rozdanie nie zostało rozwiązane; można ją przekazać jako resumePrefix przy kolejnym wywołaniu.
- Okno postępu: linia „Najlepszy dotychczasowy postęp” pokazuje też długość zapamiętanej linii, na której budują kolejne próby.
Efekt na nr 883788327 (150 s, 8 wątków): najlepszy osiągnięty wynik poprawił się z 35/104 ułożonych kart (3 króle) do 66/104 (5 królów)
– rozdanie bywa bardzo trudne (Solver mógł go nie rozwiązać nawet w tym czasie), ale realny postęp jest wyraźnie większy niż wcześniej.
Bez regresji na wcześniejszym zestawie testowym (tools/solvetest_mt.exe 15 free 100 20 6 → 13-14/15; 8 king 2 20 6 → 5-6/8, w granicach
wariancji między uruchomieniami wynikającej z nielosowej kolejności przydziału prób do wątków).
tools/diag.cpp: narzędzie diagnostyczne – uruchamia solver na jednym rozdaniu i wypisuje planszę/legalne ruchy w miejscu, gdzie utknął.

# Solver.log (main.cpp)
Po znalezieniu rozwiązania Solver dopisuje do Solver.log (obok exe) jedną linię: znacznik czasu, numer układu, łączny czas poszukiwania
(sumowany przez wszystkie etapy/wznowienia tego rozdania, nie tylko ostatni), liczba zakończonych prób i liczba przeszukanych pozycji.
Zwykły dopisywany plik tekstowy (ASCII, dopisywanie binarne jak WonNumbers.csv), nie CSV.
Sprawdzone na żywo w oknie gry (Solver → rozwiązanie układu nr 3) – poprawny wpis w Solver.log.

# Solver.log: liczba ruchów
Dopisywana teraz też liczba ruchów potrzebnych do zwycięstwa. Sprawdzone na żywo (Solver -> układ nr 3):
2026-09-27 16:20:52  Uklad nr 3  Czas poszukiwan: 0:00:00  Zakonczone proby: 11  Przeszukane pozycje: 1126880  Ruchy do zwyciestwa: 166

# Reguła 2 i 3 (game.h) — próba poprawy skuteczności automatu
Metoda: rozszerzone tools/sim.cpp (zmienna PERGAME=1 dopisuje wynik W/L każdej gry, RELAX=1 włącza tę samą "ostatnią deskę ratunku"
co main.cpp) odgrywa automatem serię rozdań (te same numery = te same rozdania w każdym wariancie) i porównuje WYNIK PAROWO
względem bazowej wersji – które konkretne rozdania zmieniły W<->L, nie tylko sumaryczny wynik. Bazowa próbka: 300 gier trybu
"Dowolna karta" (73,7% wygranych) + 150 gier trybu "Tylko król" (17,3%).

Reguła 2 (złagodzenie kary za ruch stos->kolumna, rule 14): test -15 (wolna) / -40 (król) zamiast -50/-120 dał w trybie
"Dowolna karta" wynik NETTO UJEMNY (2 rozdania L->W, 4 W->L) - w tym trybie puste kolumny to tani bufor, więc silnik i tak
rzadko musi się tego ruchu chwytać, a złagodzenie tylko marnuje ruchy. W trybie "Tylko król" (gdzie pustą kolumnę zajmuje
WYŁĄCZNIE król, więc ten ruch bywa jedynym sposobem na odblokowanie kolumny) to samo złagodzenie dało czysty zysk: 4 rozdania
L->W, ZERO W->L. Krótki przegląd wartości (-20/-40/-50/-60/-80) w trybie król: wszystkie w okolicy 29-31/150 (baza: 26/150);
wybrano -40 jako dobrze przetestowany, bezpieczny punkt. WDROŻONO: rule 14 jest teraz -50 (wolna, bez zmian) / -40 (król, było -120).

Reguła 3 (głębsze/szersze przeszukiwanie, korzystając z zapasu wydajności z wcześniejszej optymalizacji): trzy niezależne
warianty, wszystkie sprawdzone parowo na tej samej próbce 300 gier "Dowolna karta":
 - baza (głębokość 4, +4 przy pustej kolumnie -> 8, wiązka 4/6, budżet węzłów 60000/kandydata): 221/300.
 - głębokość bazowa 4->5 (więc 9 przy pustej kolumnie), budżet bez zmian: 217/300 (15 L->W, ALE 19 W->L - netto gorzej).
 - to samo przy budżecie węzłów x5 (300000) - WYNIK IDENTYCZNY co do gry (te same 15/19 flip) - czyli regresja NIE wynika
   z ucinania przeszukiwania budżetem, to realna cecha głębszego szukania tą heurystyką (błędy oceny kumulują się na
   dłuższym horyzoncie szybciej niż przybywa użytecznej informacji).
 - szerszy bonus tylko dla pustej kolumny (+4 -> +6, głębokość bazowa bez zmian): 221/300 identycznie jak baza, ale
   10 rozdań L->W i 10 W->L - czysty szum, zero realnej poprawy.
 - próba poszerzenia wiązki (4/6 -> 6/8) przy niezmienionym budżecie węzłów okazała się ~25x wolniejsza (przerwana) -
   niepraktyczne dla gry w czasie rzeczywistym bez odpowiedniego doważenia budżetu.
WNIOSEK: głębsze/szersze przeszukiwanie NIE poprawia skuteczności tego silnika - ocena oparta na sumie nagród za
poszczególne ruchy (scoreMove) nie ekstrapoluje się dobrze na dłuższy horyzont. NIE WDROŻONO żadnej zmiany reguły 3;
kod game.h wrócił do oryginalnych wartości (głębokość 4/+4, wiązka 4/6, budżet 60000). Prawdziwym ograniczeniem automatu
pozostaje brak nawrotów po realnym ruchu (patrz punkt 1 z wcześniejszej analizy - nie wdrożony w tej turze, nieproszony).
tools/sim.cpp: PERGAME=1 (dopisuje "PG <seed> W|L <ruchy>" do porównań parowych), RELAX=1 (ostatnia deska ratunku, jak main.cpp).

# Porządki w katalogu gry + nowe ikony (main.cpp, sound.h, res/icons)
- Rozwiązania solvera: teraz w podkatalogu Solved\ (Solved<numer>.dat), nie luzem obok exe.
- Dźwięki: domyślne wyszukiwanie własnego pliku .wav przeniesione do podkatalogu Sounds\ (embedowany dźwięk w exe nadal działa jako fallback, gdy pliku tam nie ma). Katalog tworzony automatycznie przy starcie.
- Ikony przycisków: NIE są już w ogóle szukane na dysku - zawsze wyłącznie z zasobów wbudowanych w exe (usunięto Bitmap::FromFile próbę i cały mechanizm nadpisywania plikiem obok exe). Prośba "iles w Icons" i prośba "nie szukaj na dysku" były sprzeczne dla ikon - potraktowałem drugą, bardziej szczegółową instrukcję jako nadrzędną.
- Zapisy gry (Zapisz jako.../Wczytaj z pliku...): dialogi zawsze proponują podkatalog Saves\ zamiast pamiętać ostatnio użyty katalog (usunięto mechanizm LastSaveDir w pasjans.ini).
- LostNumbers.csv/WonNumbers.csv/Unsolvable.csv/SolverState.csv/Solver.log/pasjans.ini/zapis automatyczny: pozostawione obok exe (nie były explicite wskazane do przeniesienia).
- Nowe ikony przycisków: Nowa (karta K), Podpowiedź (żarówka), Cofnij/Ponów (strzałki), Statystyki (wykres słupkowy), Ustawienia (klucz+koło zębate), Solver (strzałka w labiryncie) - podmienione na dostarczone obrazki, wbudowane w exe jak wyżej.

# Zabezpieczenie solvera przed szukaniem w nieskończoność (main.cpp)
Przeszukanie NIGDY nie wyczerpuje się samo (przestrzeń rozdania zbyt duża - zawsze kończy je czas/budżet węzłów, nie
udowodnienie "brak rozwiązania"), a domyślna odpowiedź na pytanie co 5 minut to "tak, szukaj dalej" - bez dodatkowego
zabezpieczenia solver bez nadzoru szukałby w nieskończoność. Dodano twardy limit: 2 godziny łącznie na jedno rozdanie
(licząc przez wszystkie wznowienia, jak SolverState.csv), po przekroczeniu automatyczne poddanie się bez pytania -
tak samo jak explicite "Nie, następne": Unsolvable.csv, zapamiętany postęp, osobna linia w logu okna wyników, przejście
do kolejnego rozdania w kolejce.

# AI_RULES.md - pełny opis reguł silnika automatu i solvera, ze wszystkimi aktualnymi wartościami liczbowymi.

# "Zagraj nieudany" (main.cpp)
Nowa pozycja w menu Akcje, pod "Zagraj wygrywający": lista rozdań z LostNumbers.csv (te same co widzi Solver),
pole "Filtruj" (filtrowanie na bieżąco po numerze i nazwie trybu, bez rozróżniania wielkości liter i polskich
znaków - "krol" znajdzie "Tylko król"), przycisk "Zagraj wybrany" (nieaktywny dopóki nic nie jest zaznaczone
na liście) i "Anuluj". Wybranie i zagranie wczytuje rozdanie tak samo jak "Zagraj wygrywający" (praktyka/przegląd,
nie liczy się do statystyk). Sprawdzone na żywo (filtrowanie, aktywacja przycisku, wczytanie właściwego układu).

# Wczytywanie zakończonego zapisu (Akcje → Wczytaj grę z pliku) (main.cpp)
Wcześniej Wczytaj grę z pliku odrzucało zapis będący już wygraną lub utkniętym rozdaniem (od razu newGame()),
więc np. zapisu wygranego ręcznie przez kogoś innego (żeby prześledzić jego ruchy Cofnij) nie dało się w ogóle
otworzyć. Dwie zmiany:
1. loadGameFrom() nie odrzuca już takiego zapisu — pokazuje krótki komunikat informacyjny ("...już zakończoną
   grą... Wczytuję go mimo to — Cofnij pozwoli prześledzić rozegrane ruchy.") i wczytuje go w całości; Cofnij
   cofa przez pełną historię ruchów zapisaną w pliku.
2. Sama ta zmiana nie wystarczała: loadGameFrom() ustawiała g_won/g_noMoves na prawdziwy stan planszy, ale nie
   ustawiała g_winCounted/g_noMovesReached/g_noMovesDialogShown — flag mówiących reszcie programu "to już
   obsłużone". Efekt: pierwsze kliknięcie na wczytanej planszy wywoływało onStateChanged(), który — nie
   wiedząc, że to wczytany podgląd, nie świeżo zakończona żywa partia — ponownie zapisywał "przegraną" do
   LostNumbers.csv i pokazywał modalne okno "Brak ruchów" z przyciskiem "Nowa gra", które natychmiast kasowało
   wczytany zapis nową rozdaną grą. Naprawione ustawieniem tych trzech flag od razu przy wczytaniu.

# Numer partii nie dawał tego samego układu na różnych komputerach/kompilacjach (game.h)
Przyczyna: GameState::newGame(seed) tasowało talię przez std::shuffle(deck, std::mt19937(seed)). std::mt19937
samo w sobie JEST w pełni ustandaryzowane (ten sam seed = ta sama sekwencja liczb wszędzie), ale std::shuffle
już nie — standard C++ nie precyzuje, jak dokładnie surowe liczby z generatora są zamieniane na losowy indeks
(robi to std::uniform_int_distribution, którego wewnętrzny algorytm też jest zależny od implementacji). Efekt:
libstdc++ (GCC/mingw), libc++ (Clang) i MSVC STL potrafią dać RÓŻNĄ permutację dla identycznego seeda i talii
w tej samej kolejności początkowej — dokładnie to zaobserwowali użytkownik i jego siostra, wpisując ten sam
numer partii na dwóch różnych kompilacjach.
Naprawa: własny, w pełni odtwarzalny Fisher-Yates (boundedRand()/deterministicShuffle() w game.h), korzystający
WYŁĄCZNIE z surowego wyjścia mt19937::operator()() i własnej (bezstronnej, przez odrzucanie) redukcji do
zakresu — bez std::shuffle, bez uniform_int_distribution. Wynik jest teraz przypięty raz na zawsze, niezależnie
od kompilatora, biblioteki standardowej czy komputera.
WAŻNE: to zmienia, jaki układ daje KAŻDY numer partii w porównaniu do exe sprzed tej zmiany — zapis gry (.dat)
nie jest tym dotknięty, bo przechowuje rozdane karty wprost, a nie odtwarza je z numeru.

# Publikacja na GitHub + automatyczna aktualizacja (main.cpp, src/update.h, README.md)
- Repozytorium przygotowane do publikacji na GitHub: README.md z opisem projektu i notką o współpracy z AI
  (Claude, Anthropic), krótkie nagłówki w plikach źródłowych (main.cpp, game.h, solver.h, update.h).
- Nowy plik src/update.h: sprawdzanie najnowszej wersji przez GitHub Releases API (WinHTTP, HTTPS,
  bez zewnętrznych bibliotek), ręczne wyciąganie tag_name/browser_download_url z JSON-a (bez pełnego
  parsera — kontrolujemy dokładny kształt odpowiedzi), porównanie wersji (vMAJOR.MINOR.PATCH).
- main.cpp: APP_VERSION (obecnie "1.0.0"); przy starcie (jeśli włączone) sprawdzanie w tle (osobny
  wątek, nie blokuje startu); w razie nowszej wersji pytanie z listą zmian i przyciskami Tak/Nie;
  po potwierdzeniu pobranie nowego pliku w tle i samodzielna podmiana exe (pomocniczy .bat czeka, aż
  program się zamknie, podmienia plik, uruchamia nową wersję, kasuje sam siebie — Windows nie pozwala
  nadpisać działającego pliku .exe bezpośrednio).
- Ustawienia → Ogólne: nowy przełącznik "Sprawdzaj aktualizacje przy starcie" (domyślnie włączony,
  zapisywany w pasjans.ini).
- Zweryfikowane na żywo na prawdziwym, publicznym repozytorium GitHub (git-for-windows/git) —
  poprawne pobranie najnowszego tagu, poprawne rozpoznanie "nowsza wersja", poprawne parsowanie numeru.
- src/update.h ma na razie wpisane GH_OWNER/GH_REPO jako TODO — do uzupełnienia po utworzeniu
  docelowego repozytorium (do tego czasu sprawdzanie po prostu nic nie znajduje, bez błędów).
- Repozytorium faktycznie opublikowane: github.com/NepciorLab/Pasjans-dziadkowy (publiczne, MIT),
  GH_OWNER/GH_REPO w src/update.h uzupełnione, wydanie v1.0.0 z dołączonym PasjansD.exe — mechanizm
  aktualizacji zweryfikowany na żywo na tym właśnie repozytorium (poprawne wykrycie własnego tagu,
  poprawne "to nie jest nowsza wersja", poprawne wykrycie starszej wersji jako wymagającej aktualizacji).

# Okno Solvera: ucięty tekst i mylące "8 z 8" (main.cpp, solver.h)
Dwa zgłoszone problemy w oknie "Najlepszy dotychczasowy postęp":
1. Tekst bywał wizualnie ucięty (np. kończył się na "…(próby"). Sama etykieta (SS_LEFT) zawija się
   automatycznie, ale kontrolka miała tylko 18px wysokości (jedna linia) — dłuższy wariant komunikatu
   (z dopiskiem o długości linii, na której budują próby) zawijał się do drugiej linii, która była po
   prostu niewidoczna. Naprawione: kontrolka ma teraz 36px (dwie linie), reszta okna Solvera przesunięta
   niżej, całe okno powiększone o te same 18px.
2. "Króle na miejscu 8 z 8" bywało widoczne, mimo że solver dalej pracował — to nie błąd w liczeniu
   (progressOf() w solver.h i checkWin() w game.h liczą identycznie), tylko brak informacji w UI: gdy
   któryś wątek znajdzie pełne rozwiązanie, solver celowo NIE kończy od razu — szuka jeszcze przez 30 s
   (POLISH_EXTRA_MS) krótszej wersji tej samej wygranej linii, chyba że od razu trafi na wystarczająco
   krótką (≤400 ruchów). "8 z 8" widziane w tym oknie ZAWSZE oznaczało już znalezione rozwiązanie —
   po prostu nic o tym nie mówiło. Dodane do solver::Progress: solutionFound/solutionLen/solutionFoundAt,
   ustawiane przez workerProc() w chwili znalezienia (lub skrócenia) rozwiązania; okno pokazuje teraz
   zamiast starego komunikatu: "Rozwiązanie znalezione! Długość: N ruchów. Szukam krótszej wersji
   jeszcze przez S s…". Zweryfikowane bez GUI (osobny program testowy wołający solver::solveDeal()
   bezpośrednio, odpytujący Progress z osobnego wątku) — solutionFound poprawnie ustawia się w locie,
   z poprawną bieżącą długością, zgodną z ostatecznym wynikiem.
