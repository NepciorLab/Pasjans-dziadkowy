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

# Auto-aktualizacja: potwierdzenie "Tak" zamykało program i nic dalej się nie działo (src/update.h)
Przyczyna: pomocniczy plik .bat (podmieniający exe po zamknięciu programu — patrz opis mechanizmu
aktualizacji wyżej) był zapisywany jako UTF-16 (_wfopen z "ccs=UNICODE"). cmd.exe w ogóle nie
rozpoznawał takiego pliku jako polecenia ("'plik.bat' is not recognized...") — potwierdzone wprost:
odtworzenie dokładnie tego samego pliku i uruchomienie go w cmd.exe dawało dokładnie ten błąd, więc
skrypt nigdy nic nie robił. Dodatkowo tryb tekstowy fopen podwajał już i tak ręcznie wpisane "\r\n"
na "\r\r\n" (widoczne w zrzucie hex: ...0D 00 0D 00 0A 00). Naprawa: plik .bat zapisywany teraz jako
zwykły ANSI (fprintf na strumieniu ze zwykłego _wfopen(..., L"w")), ścieżki zamieniane na kodowanie
systemowe (CP_ACP) przez nową funkcję toAcp(). Zweryfikowane end-to-end na prawdziwej funkcji
launchSelfUpdate() (nie na kopii) — podmiana pliku i przekazanie sterowania dalej działa poprawnie.

# Nowe rewersy kart (res/cards_png/B1.png, B2.png, res/cards.rc, src/renderer_d2d.h)
Rewersy wycięte z dostarczonego PNG: czerwony (talia 1) = B1, niebieski (talia 2) = B2, 300x420 z przezroczystymi
rogami, wbudowane w exe (CARD_B1/CARD_B2). Rewers pojawia się tylko na stosie rezerwy.
Karty (struct Card) nie niosą informacji, z której talii pochodzą, ale da się ją odtworzyć: GameState::newGame()
buduje talię "talia 1, potem talia 2" i tasuje deterministicznym deterministicShuffle(), więc tasując równoległą
tablicę numerów talii tym samym ziarnem dostajemy numer talii każdej karty (game.h: reserveDeckTagsForSeed()).
Rezerwa jest zawsze sufiksem początkowej listy (rozdanie zabiera z przodu, cofnięcie oddaje z powrotem), więc
rewers na ekranie = talia następnej karty do rozdania: czerwony dla talii 1, niebieski dla talii 2 (main.cpp:
reserveTopDeck(), numer partii z g_currentGameNumber, zapis gry bez zmian). Sprawdzone na 2000 partiach:
0 niezgodności z rzeczywistą rezerwą z GameState::newGame(), po ~50% kart z każdej talii.

# Pomoc: wersja i data budowy; nowa animacja rozdawania z rezerwy (main.cpp, renderer_d2d.h)
- Pomoc (H) pokazuje pod tytułem "Wersja X.Y.Z (zbudowana dd.mm.rrrr)": numer z APP_VERSION, data z __DATE__ z
  kompilacji main.cpp (buildDateText(); Zig/clang traktuje __DATE__ jako błąd, stąd lokalny pragma -Wdate-time).
- Rozdawanie z rezerwy: karty startują zakryte (rewers swojej talii, czerwony/niebieski), w locie unoszą się do
  ~120% rozmiaru (1 + 0.2*sin(pi*t), z powrotem 100% przy lądowaniu) i obracają się na drugą stronę (szerokość
  ~|cos|, awers bez odbicia lustrzanego), a w ostatnich 10% lotu leżą już w pełni odkryte (flip = min(1, t/0.9)).
  t liczone liniowo w czasie lotu. CardAnim::dealFlip/deck, RendererD2D::drawCardFlip(); dotyczy rozdania oraz
  ponowienia rozdania (Ponów), cofnięcie rozdania (CardAnim::reverse) ma własny przebieg: karta unosi się odkryta, zaczyna obracać po 15% lotu,
  od 40% jest już w pełni zakryta (w rewersie swojej talii) i resztę lotu pokonuje zakryta; skala 100→130→100%
  (powiększenie 30% w obu animacjach: rozdawania i cofania).
  Prędkość ruchu przy cofaniu jest odwrócona względem rozdawania: zamiast wyhamowania na końcu (ease-out, karta była
  w połowie czasu już blisko rezerwy i obracała się tuż nad stosem) jest przyspieszanie (ease-in, s = t^3, w
  CardAnim::ease() dla reverse) — karta powoli odrywa się od stołu, kończy obrót po przebyciu ok. 6% drogi
  (t=0.4), w połowie czasu jest dopiero w 13% drogi, a do rezerwy dolatuje szybko. Zweryfikowane offline: klatki animacji wyrenderowane
  tym samym rendererem do PNG (t=0..1, obie talie) — rewers → krawędź → awers, skala 100→120→100%.

# Podpowiedź: ruch na puste miejsce pokazywany animacją jak każdy inny (main.cpp, performHintNow())
Gdy najlepszym ruchem było przeniesienie karty na pustą kolumnę, podpowiedź zamiast animacji lotu karty pulsowała
wszystkimi pustymi kolumnami (g_emptyColPulsing). Gałąź z pulsowaniem usunięta — ruch na pustą kolumnę trafia teraz do
tej samej ścieżki co pozostałe: karta lecąca do slotu docelowego (kod animacji już obsługiwał kolumnę docelową
o długości 0). Dodatkowo lista podpowiedzi po takim ruchu nie urywa się już ("break"): pierwszy ruch na pustą kolumnę
jest wpisywany, kolejne ruchy na (inne) puste kolumny są pomijane jako ten sam ruch, a skanowanie trwa dalej — kolejne
naciśnięcia Podpowiedzi pokazują dalsze sensowne ruchy, tak jak w pozostałych przypadkach. Pulsowanie rezerwy
(podpowiedź rozdania) bez zmian. Wersja 1.0.5.

# Wersja 1.0.6: rozwiązania użytkownika, ustawienia etapu Solvera, cofanie rozdania (main.cpp, solver.h)
- Cofanie rozdania: obrót kończy się w 70% lotu (było 40%).
- Pasjans rozwiązany ręcznie przez użytkownika (nie przez Samograj) zapisuje się jak rozwiązanie solvera, jako
  Solved\SolvedUser<numer>.dat: rozdanie początkowe + każdy ruch jako krok "Ponów". Linię wygranej bierze się ze stosu
  cofania (stan przed każdym ruchem), więc plik powstaje tylko wtedy, gdy historia sięga do samego rozdania (nie ucięta
  przez MAX_UNDO_HISTORY, numer rozdania znany). Jeśli to rozdanie było już rozwiązane ręcznie, zostaje krótsza linia
  (plik powtórki rośnie z każdym ruchem, więc porównywany jest rozmiar). Wspólny zapis w writeReplayFile() — używa go
  też solver (saveSolvedFile); saveUserSolvedFile() wołane z onStateChanged() przy pierwszej wygranej.
- Okno Solvera: suwak "Sprawdź po upływie" (1–10 min, co minutę, domyślnie 5) i pole "Po upływie czasu przejdź do
  następnego". Po upływie etapu bez rozwiązania: pole zaznaczone → rozdanie trafia do Unsolvable.csv (postęp
  zapamiętany) i startuje następne; niezaznaczone → ten sam pasjans dostaje kolejny etap. Okno z pytaniem "Czy
  kontynuować poszukiwania?" (odliczanie 10 s) usunięte — decyduje pole. Suwak działa od razu także dla etapu w toku
  (solver::Progress::deadline, odczytywany przez wątki przy każdym sprawdzeniu; sprawdzone osobnym testem: etap 60 s
  skrócony w trakcie do ~2 s kończy się timedOut). Ustawienia zapamiętane w pasjans.ini ([Solver] StageMinutes, AutoNext).
  Limit 2 godzin na rozdanie bez zmian.

# Wersja 1.0.7: suwak i pole także w oknie wyboru, bez ikony w oknach Solvera, nowe czasy lotu kart (main.cpp)
- Suwak "Sprawdź po upływie" (1–10 min) i pole "Po upływie czasu przejdź do następnego" są teraz także w oknie
  WYBORU rozdań (wcześniej tylko w oknie postępu, które pojawia się dopiero po "Rozwiąż wybrane"). Oba okna edytują te
  same ustawienia (g_solverStageMin / g_solverAutoNext, wspólne kontrolki: solverMakeStageControls()).
- Okna Solvera (wybór, postęp) i "Zagraj nieudany" bez ikony w pasku tytułu: styl WS_EX_DLGMODALFRAME (także w
  AdjustWindowRectEx, żeby obszar roboczy miał właściwy rozmiar). Sprawdzone zrzutami prawdziwych okien (test:
  testwindows.cpp włącza main.cpp do jednego programu i robi PrintWindow okien wyboru i postępu).
- Rozdawanie z rezerwy i jego cofnięcie: karty lecą z mniej więcej tą samą PRĘDKOŚCIĄ, więc karta z najdłuższą drogą
  leci najdłużej i ląduje ostatnia (wcześniej wszystkie leciały tyle samo, więc najdalsze były najszybsze). Najdalsza
  karta leci DEAL_MAX_FLIGHT_MS = 440 + 200 = 640 ms (przed mnożnikiem prędkości animacji); bliższe proporcjonalnie
  krócej, ale nie mniej niż DEAL_MIN_FLIGHT_FRAC = 35% tego czasu (inaczej unoszenie i obrót byłyby rozmazane).
  Wszystkie karty startują naraz (dawny odstęp 40 ms między kartami usunięty — przy nim kolejność lądowania
  wynikałaby z kolejności kolumn, a nie z odległości). retimeDealFlights() ustawia czasy po zbudowaniu animacji;
  obie stałe są na górze tej funkcji, do eksperymentów.

# Wersja 1.0.8: przywrócone okno "Czy kontynuować poszukiwania?" (main.cpp)
W 1.0.6 okno z pytaniem (odliczanie 10 s) zostało usunięte, a o wszystkim decydowało samo pole — przy niezaznaczonym
polu solver nie miał jak przejść do następnego pasjansa przed upływem limitu 2 godzin. Teraz po upływie etapu bez
rozwiązania: pole zaznaczone -> od razu następny pasjans (Unsolvable.csv, postęp zapamiętany); pole niezaznaczone ->
okno z pytaniem (10 s, brak odpowiedzi = szukaj dalej; "Nie, następne" = jak zaznaczone pole). Treść pytania podaje
aktualną długość etapu z suwaka (zamiast na sztywno "5 minut"); okno bez ikony jak pozostałe okna Solvera.

# Rozdawanie z rezerwy i cofanie: lot o 30% wolniejszy (main.cpp)
DEAL_MAX_FLIGHT_MS (czas lotu najdalszej karty) = (440 + 200) * 1.3 = 832 ms; bliższe karty proporcjonalnie krócej
(minimum 35% tego czasu), reszta bez zmian. Pozostałe animacje (ruchy kart, podpowiedź, rozdanie początkowe) nie
tknięte. Numer wersji w exe zmienia się dopiero przy publikacji wydania.

# Solver: wybór liczby wątków (main.cpp, solver.h)
- Wiersz "Liczba wątków" ze spin buttonem (edit + UpDown) w oknie wyboru rozdań i w oknie postępu. Zakres 1 do
  (procesory logiczne − 2), bez dawnego limitu 8 (solver::maxThreads(); np. i9-12900 = 24 procesory logiczne -> maks. 22).
  Wartość domyślna bez zapisanego wyboru: solver::defaultThreads() = min(8, maks.), czyli jak dotąd. Zapamiętywana w
  pasjans.ini ([Solver] Threads). W oknie postępu zmiana działa od następnego etapu (wątki powstają przy starcie etapu);
  opis przy polu to mówi. Każdy wątek trzyma tablicę odwiedzonych pozycji do ok. 35 MB, więc 22 wątki to rzędu 0,8 GB.
- Okna powiększone o ten wiersz (wybór 496 px, postęp 408 px wysokości obszaru roboczego). Sprawdzone zrzutami prawdziwych
  okien (testwindows.cpp).

# Solver: liczba wątków zmieniana w trakcie etapu (solver.h, main.cpp)
Wcześniej wątki powstawały przy starcie etapu, więc zmiana liczby działała dopiero od następnego. Teraz solveDeal()
od razu uruchamia pulę (maks. 62 pomocnicze wątki, normalnie procesory logiczne − 2), a Progress::threads jest
"żywą" liczbą aktywnych: wątek o indeksie >= threads czeka (Sleep 50 ms) i porzuca bieżącą próbę, wątek poniżej
progu zaczyna szukać. Spin button w oknie postępu zapisuje do Progress::threads, zmiana jest widoczna po ułamku
sekundy; opis "od następnego etapu" usunięty. Sprawdzone na prawdziwym solverze (testthreads.cpp, jeden etap,
tryb Tylko król, seed 4): 2 wątki 1,30 -> 12 wątków 6,20 -> 3 wątki 1,91 -> 1 wątek 0,77 mln węzłów/s.

# Fajerwerki i dźwięki jak w Garibaldce (src/fireworks2.h, sound.h, main.cpp, res/sounds)
- Silnik fajerwerków zastąpiony tym z Garibaldki (src/fireworks2.h): setki cienkich smug z białą głową i kolorową
  smugą, opór powietrza i grawitacja, migoczące iskry, silna poświata (bloom), cztery rodzaje wybuchów (kula, wierzba,
  pierścień, dwie powłoki). Cały obraz liczony na CPU i kładziony na stół jednym obrazem addytywnym (alfa 0 = światło
  dodawane do tła; drawFireworks()). Stary system (anim.h: Particle/Firework/FireworkSystem) usunięty.
- Dźwięki podążają za animacją (fireworksSounds()): wystrzelona rakieta świszcze (jeden z 5 świstów, który mieści się
  w jej locie, tak ustawiony, by kończył się w chwili wybuchu), każdy wybuch huczy (jeden z 5 huków, głośniej przy
  większym wybuchu), z panoramą zależną od miejsca na ekranie. Pliki res/sounds/swist1-5.wav i wybuch1-5.wav z nagrania
  prawdziwego pokazu, wbudowane jako SND_9..SND_18; w oknie ustawień niewidoczne (SOUND_UI_COUNT = 9 slotów).
- Domyślny odgłos zwycięstwa (sukces.wav) z Garibaldki; własny plik z Ustawień nadal go zastępuje.
- SoundSystem::playIdx(idx, głośność, panorama).

# Wygląd jak w Garibaldce: pasek przycisków i nakładki Ustawienia / Statystyki / Pomoc (src/overlay.h, main.cpp, layout.h)
- Pasek przycisków: ciemniejszy pas w kolorze stołu, na nim przezroczyste zaokrąglone płytki (białe 14%, jaśniejsze pod
  myszą, cienki jasny obrys; Samograj "włączony" ma złoty obrys), tylko ikony — podpisy pod przyciskami usunięte, więc
  pasek jest niższy (Layout::TOOLBAR_H 94 -> 72). Tło gry bez zmian: jednolity kolor wybrany w Ustawieniach (zmiana koloru
  przemalowuje też pasek: toolbarRefresh()).
- Ustawienia, Statystyki i Pomoc nie są już osobnymi oknami Windows, tylko nakładkami rysowanymi wewnątrz okna gry
  (Direct2D, tryb natychmiastowy: kontrolki zgłaszają obszary kliknięć przy rysowaniu): przyciemniony stół, ciemnoniebieski
  panel ze złotą ramką, złote nagłówki, przezroczyste zaokrąglone przyciski. Panele są NIEBIESKIE (w Garibaldce zielone).
  Stary kod okien (ok. 770 linii) usunięty.
  * Ustawienia (3 grupy): Ogólne (kolor tła, tryb wolnego miejsca, zaznaczanie sekwencji, głębokość SI przyciskami -/+,
    prędkość animacji, aktualizacje), Dźwięki (głośność suwakiem, własne dźwięki: Wybierz / odtwórz / wycisz / domyślny),
    Klawisze (dwa skróty na akcję, przechwytywanie klawisza z Shiftem, czyszczenie, "Przywróć domyślne"). Zmiany działają
    od razu i zapisują się w pasjans.ini (wcześniej było OK/Anuluj). Zmiana trybu wolnego miejsca w trakcie partii pyta o
    potwierdzenie, bo zaczyna nową grę.
  * Statystyki: jedna tabela, kolumny "Tylko król" / "Dowolna karta"; "Wyzeruj statystyki" pyta o potwierdzenie.
  * Pomoc: przewijana (kółko, strzałki, PgUp/PgDn, Home/End, pasek), z wersją i datą budowy; treść przepisana pod
    obecną grę.
  Klik poza panelem lub Esc zamyka nakładkę; podczas otwartej nakładki mysz i klawiatura należą do niej.
- Sprawdzone na prawdziwym oknie gry (testoverlays.cpp włącza main.cpp do jednego programu, tworzy okno, otwiera każdą
  nakładkę i robi zrzut oraz symuluje kliknięcia): 23/23 sprawdzeń interakcji (kolory, krokowanie głębokości, pole
  aktualizacji, suwaki, przechwytywanie klawisza, wyciszanie dźwięków, przewijanie pomocy, zamykanie, przyciski paska).
- Bez zmian zostały okna Solvera, pytania "Brak ruchów", "Zagraj numer", okna komunikatów i pasek menu Akcje.


# Fajerwerki 20 s, animacja nowej partii (src/main.cpp, renderer_d2d.h, fireworks2.h)
- Fajerwerki: gdy są widoczne, stół jest przyciemniony o połowę (płynnie wchodzi i schodzi); po 20 s same się wyłączają
  (przez ostatnie 2,5 s nie startują już nowe rakiety, ostatnie sekundy przyciemnienie gaśnie).
- Nowa partia: karty są rozdawane z rezerwy na stół wierszami (wiersz po wierszu, każda karta leci z uniesieniem
  i obrotem jak przy rozdaniu z rezerwy, bliższe lądują wcześniej), licznik rezerwy maleje w trakcie. Zegar partii
  rusza, gdy karty leżą na stole.
- Jeśli na stole są już karty (nie przy starcie programu i nie w "Samograj bez końca"): najpierw 4-sekundowa animacja
  zbierania. Karty unoszą się kolejno (od tych na wierzchu), po czym wciąga je wir - każda krąży po spirali z własną,
  losową prędkością, koziołkuje (obrót wokół osi pionowej i w płaszczyźnie stołu) - aż wszystkie leżą rewersem do góry
  w jednej kupce na środku; kupka przesuwa się na miejsce rezerwy i zaczyna się rozdanie. Nowa partia kliknięta w
  trakcie animacji przerywa ją i od razu rozdaje.

# Wersja 1.1.2: animacja nowej partii dopracowana, sztuczne ognie (src/main.cpp, fireworks2.h)
- Zbieranie kart przy nowej partii: karta zaczyna wirować w tej samej chwili, w której zaczyna się unosić (bez czekania na
  pozostałe); kupka przesuwa się na rezerwę ruchem z przyspieszeniem i opóźnieniem (wygładzenie czwartego stopnia).
- Fajerwerki: wybuch "pierścień" jest teraz kołem (był spłaszczoną elipsą); mniej więcej co trzeci fajerwerk gaśnie z
  rozbłyskiem - każda iskra w końcówce życia na chwilę wybłyskuje na biało i dopiero gaśnie.

- Chwycona karta/sekwencja: miejsca, w które można ją przełożyć (kolumny i fundacje), są obrysowane zieloną ramką w miejscu lądowania, jak w Garibaldce.

# Wersja 1.1.3: podpisy przyciskow, Solver w stylu gry, szybsze tornado i rozdanie
- Pod przyciskami paska znów są podpisy (Nowa gra, Podpowiedź, Samograj, Solver, Cofnij, Ponów, Statystyki, Ustawienia);
  pasek ma znów 94 px (Layout::TOOLBAR_H).
- Okna Solvera (wybór rozdań, szukanie rozwiązań, pytanie "kontynuować?" z 10-sekundowym odliczaniem i wynik) są nakładkami
  w oknie gry, w tym samym stylu co Ustawienia: lista z polami wyboru i paskiem przewijania (klik zaznacza, Shift - zakres,
  Ctrl+A - wszystkie), pole ziarna wpisywane z klawiatury, suwak długości etapu, pole "przejdź do następnego", liczba wątków
  przyciskami -/+ (zmiana działa od razu w trwającym etapie), paski postępu. Okna są modalne: pasek, menu i skróty
  nie działają, dopóki Solver jest otwarty. Zamknięcie programu w trakcie szukania zatrzymuje wątki.
- Tornado nowej partii o 30% szybsze, bez postoju kupki na środku: kupka od razu jedzie na miejsce rezerwy (całość ok. 2,9 s).
- Rozdawanie na stół przy nowej partii: kolejny wiersz rusza, gdy poprzedni jest w połowie lotu (połowa czasu lotu
  najdalszej karty wiersza).

# Wersja 1.1.3: grupa Grafika, dźwięk tornada, prędkości animacji
- Ustawienia: nowa grupa "Grafika" po "Ogólne" (kolor tła, zaznaczanie sekwencji, prędkość animacji, pole "Animacja nowego
  rozdania" - domyślnie zaznaczone; odznaczone pomija animację nowej gry, rozdanie od razu leży na stole).
- Prędkość animacji: "Bardzo wolno" (25%), "Wolno" (50%), "Normalnie" (100%), "Szybko" (200%), "Bardzo szybko" (400%).
- Dźwięk tornada (res/sounds/tornado.wav, z dołączonego Tornado.mp3, pierwsze 3,4 s) odtwarzany przez animację zbierania
  kart z narastaniem (0,6 s) i wyciszaniem (0,8 s) na jej końcu; pole wyboru "Dźwięk tornada" w grupie Dźwięki. Z listy
  własnych dźwięków zniknął "Nowa gra" (jego miejsce zajął dźwięk tornada; nowa gra bez animacji zbierania jest bez dźwięku).
- Dźwięk rozdania przy nowej partii gra raz na całe rozdanie, nie przy każdym wierszu; przerwa między wierszami 0,2 s
  (skalowana prędkością animacji).

# Wersja 1.1.3: grupa "Deweloper"
- Ustawienia -> "Deweloper" pojawia się tylko wtedy, gdy obok gry leży plik Developer.PMa. Wszystkie jej wartości zapisują się
  w dev.txt (Key=Value) i działają tylko w tym trybie.
- Pola w milisekundach: czas całej animacji tornada, przyspieszanie na początku, opóźnianie na końcu (prędkość animacji rośnie
  i maleje liniowo przez podany czas) oraz odstęp między wierszami rozdania.
- Przyciski: rozdanie 1 wiersza z rezerwy (to prawdziwy ruch - można go cofnąć), rozdanie wszystkich wierszy i tornado
  (podgląd na aktualnym stole, gra się nie zmienia), fajerwerki. Na czas animacji nakładka znika; klik lub klawisz przywraca ją.
- Suwaki fajerwerków: odstęp między wystrzałami, liczba rakiet naraz, liczba i prędkość iskier, grawitacja, czas życia iskier,
  jasność poświaty, szansa rozbłysku na końcu, czas trwania. Zmiany działają od razu także w trwających fajerwerkach.

- Dźwięk tornada jest zapętlonym 6-sekundowym klipem (płynne połączenie końca z początkiem) i trwa tyle, ile animacja - także po zmianie jej czasu w ustawieniach dewelopera; narasta 0,6 s i cichnie 0,8 s na jej końcu.

# Wersja 1.1.3: dopracowana grupa "Deweloper", fajerwerki z zakresami i dwoma etapami
- Wartości domyślne (animacje i fajerwerki) przejęte z dev.txt: tornado 2500 ms, przyspieszanie 250 ms, opóźnianie 500 ms,
  odstęp wierszy 150 ms; fajerwerki: odstęp 0,8, 5 rakiet, iskry 1,3, życie 1,2, jasność 0,8, rozbłysk 35%, 20 s.
- Pola liczbowe: kursor tylko miga (tekst się nie przesuwa), zaznaczanie myszą / Shift+strzałki / Ctrl+A / dwuklik, Backspace
  kasuje znak przed kursorem, Delete za kursorem, strzałki, Home/End, Ctrl+C/X/V.
- Okno ustawień zostaje na ekranie podczas animacji i można je przesuwać za pasek tytułu; w grupie Deweloper nie przyciemnia
  stołu i nie zamyka się kliknięciem obok.
- "Fajerwerki" i "Rozdaj 1 wiersz" działają w pętli do ponownego wciśnięcia przycisku (fajerwerki startują od nowa; wiersz
  jest rozdawany i cofany do rezerwy). Zamknięcie ustawień zatrzymuje pętle.
- Suwaki fajerwerków z dwoma punktami (zakres min-maks: rakiety naraz, liczba i prędkość iskier, grawitacja, czas życia,
  jasność); gra losuje wartość z zakresu dla każdego ognia. Odstęp, szansa rozbłysku, czas trwania i podział - jeden punkt.
- Czas trwania fajerwerków ma dwa etapy: pierwsze 60% (suwak podziału 0-100%) - stała liczba rakiet i odstęp, potem
  liczba wystrzałów maleje aż do pojedynczego fajerwerku na samym końcu. Dotyczy też fajerwerków po wygranej.

- Numer układu, liczba ruchów i czas przeniesione z planszy na pasek przycisków, za ostatni przycisk (trzy wiersze); licznik Samograju jest dalej na prawo. Komunikaty (np. Brak możliwych ruchów) i Myślę zostają na planszy.

- Nowe wartości domyślne z dev.txt: tornado 2500 ms (przyspieszanie 250, opóźnianie 500), odstęp wierszy 150 ms; fajerwerki: iskry 0,4-2,0, prędkość 0,9-1,2, życie 0,9-1,2, podział etapów 70%.
