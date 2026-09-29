# Reguły SI — Pasjans Dziadkowy

Ten plik opisuje **dwa niezależne silniki** w kodzie, bo łatwo je pomylić:

| | Gdzie | Zna kolejność rezerwy? | Cofa się? | Użycie |
|---|---|---|---|---|
| **Automat** | `game.h`, `scoreMove`/`rankCandidates`/`searchBest` | Nie — celowo | Nie, gra na żywo, nieodwołalnie | Podpowiedź (P), Automat (A), Samograj |
| **Solver** | `solver.h` | Tak — działa na znanym rozdaniu | Tak, pełne przeszukiwanie z nawrotami | przycisk Solver |

Ten dokument dotyczy głównie **automatu** (silnika grającego na żywo) — to jego reguły user opisał jako "SI gry". Solver ma inną, znacznie prostszą logikę (opisaną krótko na końcu).

---

## 1. Jak działa automat — ogólny obraz

Przy każdym ruchu (podpowiedź, pojedynczy auto-ruch, każdy krok Samograja):

1. Wygeneruj wszystkie legalne ruchy z bieżącej planszy (`allCandidateMoves`) — z wbudowanymi **twardymi filtrami** (rozdział 3), które od razu odrzucają ruchy jałowe/bezsensowne, więc nigdy nie trafiają do oceniania punktowego.
2. Dla najlepszych **20** kandydatów z korzenia (`kRootBeam=20`, wybranych po samej ocenie natychmiastowej) uruchom pełne przeszukiwanie w głąb (rozdział 4) — dla pozostałych kandydatów (jeśli jest ich więcej niż 20) nie liczy się nic głębszego.
3. Posortuj wynik trzema kryteriami po kolei (rozdział 5) i zagraj najlepszy.

Dwa specjalne, twarde skróty **całkowicie omijają** powyższe (rozdział 6): plansza "uprzątnięta" → rozdanie z rezerwy jest obowiązkowe; 3+ puste kolumny (tylko w trybie "Dowolna karta") → zajmij jedną od razu, bez pełnego przeszukiwania.

---

## 2. Punktacja pojedynczego ruchu — `scoreMove()`

Każdy ruch dostaje punkty **analitycznie**, bez faktycznego wykonywania go na planszy (szybkie — liczone dla każdego kandydata na każdym węźle przeszukiwania). Reguły są ponumerowane historycznie (1–16); część została usunięta/zastąpiona twardymi filtrami — zaznaczone niżej.

Legenda: **[K]** = inna wartość w trybie "Tylko król", **[D]** = inna/nieobecna wartość w trybie "Dowolna karta".

### Rozdanie z rezerwy
| Sytuacja | Punkty |
|---|---|
| Plansza "uprzątnięta" (patrz rozdz. 6) | 0 (reguła nieaktywna — i tak obowiązkowe) |
| W przeciwnym razie | **+1** (reguła 11) |
| **[D]** dodatkowo, jeśli istnieje pusta kolumna: | flat **−100**, oraz **−500** za każdą kolumnę, którą to rozdanie faktycznie zajmie (reguła 15, symetryczna do reguły 3), oraz **+200** za każdą pustą kolumnę, której rozdanie realnie NIE dosięgnie, bo rezerwa się skończy wcześniej (reguła 4) |
| **[K]** | żadna z powyższych trzech kar/premii nie obowiązuje — rozdanie to zawsze płaskie +1 |

### Każdy inny ruch — bazowy koszt
**−30** (reguła 12) — stały koszt każdego ruchu, zniechęcający do okrężnych dróg.

### Karta ze stosu z powrotem na stół (bufor)
| | Punkty |
|---|---|
| Baza | **[D] −50** / **[K] −40** (reguła 14, symetryczna do reguły 7) |
| Dodatkowa kara "świeżo odesłana": jeśli ta konkretna karta trafiła na stos w ciągu ostatnich 20 **prawdziwych** ruchów | **−(450 / wiek_w_ruchach)**, malejąco z wiekiem, zanika po 20 ruchach |

*[K] było −120, złagodzone do −40 po teście A/B (150 rozdań, ten sam zestaw: 4 rozdania przegrane→wygrane, 0 wygrane→przegrane). [D] zostało bez zmian po tym samym teście, tam złagodzenie wypadło na minus.*

### Karta na stos (na dowolny z 8 slotów)
| | Punkty |
|---|---|
| To globalnie najniższa ranga jeszcze nieukończona na stosach (reguła 5) | **+200** |
| W przeciwnym razie (reguła 7) | **[D] +50** / **[K] +120** |
| Jeśli ruch odsłania kartę pod spodem (`ci>0`) | + bonus odsłonięcia, patrz niżej |
| Jeśli to była ostatnia karta w kolumnie | kolumna liczy się jako zwolniona (patrz reguła 3 niżej) |

### Ruch kolumna → kolumna
| | Punkty |
|---|---|
| Cel nie był pusty — złączenie w sekwens (reguła 8) | **[D] +20** / **[K] +30** |
| Cel był pusty | brak własnego bonusu tutaj (patrz reguła 3) |
| Jeśli ruch odsłania kartę pod spodem | + bonus odsłonięcia, patrz niżej |

### Bonus odsłonięcia karty (reguły 6 / 9 / 10 / 16 — tylko gdy ruch faktycznie coś odsłania w kolumnie źródłowej)
Sumują się wszystkie poniższe:
- **+20** flat, za samo odsłonięcie czegokolwiek (reguła 16)
- **+60**, jeśli odsłonięta karta NIE była już poprawnie ułożona pod tym co się przeniosło (czyli to naprawdę nowe odsłonięcie, nie kosmetyczny rozdział jednego poprawnego sekwensu) (reguła 6)
- Za każdą niską kartę (As/2/3/4) wciąż pogrzebaną GŁĘBIEJ pod nowo odsłoniętą: suma wartości × liczba takich kart (reguła 9). Wartości pojedynczej karty: As **[D] 5 / [K] 8**, Dwójka **[D] 4 / [K] 6**, Trójka **3**, Czwórka **1**. Przykład z brief: wśród pogrzebanych kart jest As i Dwójka → (5+4)×2=18 pkt.
- **+10 [D] / +20 [K]**, jeśli wśród pogrzebanych kart jest ranga z 3+ kopiami tego samego koloru (czerwony/czarny, nie maści) — sygnał "tu utknęły karty tej samej rangi i koloru" (reguła 10)

### Zmiana liczby pustych kolumn (reguły 3 / 15 — liczone raz na koniec, po uwzględnieniu wszystkich powyższych)
| Sytuacja | Punkty |
|---|---|
| Ruch **zwolnił** kolumnę (reguła 3) | **[K]: +50** flat. **[D]: +500**, ale jeśli ta kolumna była zajęta "sztucznie" (przez ruch na TEJ SAMEJ ścieżce przeszukiwania, nie w realnej historii) w ciągu ostatnich ~20 ruchów wstecz — zamiast tego **500 / wiek_w_plysach** (malejąco, zapobiega farmieniu: zaparkuj i zaraz zabierz) |
| Ruch **zajął** pustą kolumnę (reguła 15) | **[D]: −500**. **[K]: 0** (brak kary — tam tylko Król może zająć puste miejsce, więc to zawsze świadoma, konieczna decyzja) |
| Bez zmiany | 0 |

*W trybie [K] reguła 3 uruchamia się tylko wtedy, gdy ostatnia karta w kolumnie (zawsze samotny Król) trafia na stos — to raczej zwykłe odsłonięcie niż "farmienie miejsca parkingowego", stąd płaska, nieamortyzowana wartość zamiast schematu 500/wiek.*

### Reguła 13 (usunięta)
Była karą −30 za rozdzielenie poprawnego sekwensu. Zastąpiona twardym warunkiem legalności (`sequenceSplitAllowed`, rozdział 3) — jeśli ruch przeszedł tamten filtr, już wiadomo że jest wart zrobienia, więc nie ma dodatkowej kary punktowej.

---

## 3. Twarde filtry legalności (zanim ruch w ogóle trafi do oceniania)

`allCandidateMoves()` generuje ruchy legalne wg zasad gry, ale **dodatkowo** odrzuca (nie generuje wcale, nie tylko punktuje nisko) ruchy uznane za jałowe:

- **`isPointlessColToCol`** — blokuje ruch, gdy: (A) karta ląduje na dokładnie tej samej karcie co teraz pod spodem (zero nowej informacji), (B) karta już poprawnie leżała pod tym co się przenosi I na nowym miejscu leżałaby w dokładnie tej samej relacji (ta sama ranga+kolor) — chyba że odsłania kartę gotową od razu na stos, (C) **[K] tylko**: Król z wierzchu kolumny na pustą kolumnę, gdy nic go nie przykrywa (przesuwanie samego siebie).
- **`sequenceSplitAllowed`** — rozdzielenie już poprawnego sekwensu jest dozwolone TYLKO gdy: (a) odsłania kartę gotową od razu na stos ORAZ (b) karta docelowa (na którą ląduje odłamana część) sama NIE jest gotowa od razu na stos (bo wtedy powinna tam iść wprost, a nie być podstawą do budowania).
- **`emptyColumnOccupyAllowed`** — ruch na pustą kolumnę wart jest swojej kary (reguła 15) tylko gdy coś realnie odsłania (ci>0) I przynajmniej jedno: (a) odsłonięta karta nie leżała już poprawnie, (b) odsłonięta karta idzie od razu na stos, (c) jakaś inna kolumna ma głowę/sekwens gotowy do wylądowania na odsłoniętej karcie od razu. **[K]: zawsze dozwolone** (tylko Król może tam wejść, więc nie ma ryzyka "zmarnowania" pustego miejsca na coś kosmetycznego).
- **`hasOtherLegalDestination`** — ruch NA pustą kolumnę jest w ogóle generowany tylko gdy nie ma innego legalnego celu (inna niepusta kolumna LUB — dla najniższej karty przenoszonego fragmentu — stos) dla tej samej karty.
- Analogiczny warunek "brak innej opcji" dla ruchu stos→kolumna na pustą kolumnę: dozwolony tylko gdy na stole nie ma już ŻADNEGO innego ruchu kolumna↔kolumna/stos.
- Ruch stos→kolumna w ogóle jest generowany tylko gdy "coś odblokowuje" — jakaś inna kolumna ma głowę/sekwens, który mógłby wylądować na tej karcie na kolejnym ruchu (poza samym trafieniem na pustą kolumnę, patrz wyżej) — chyba że ta odblokowywana karta i tak poszłaby wprost na własny stos (wtedy nie liczy się jako "odblokowanie").

---

## 4. Przeszukiwanie w głąb — `searchBest()`

Dla każdego z 20 najlepiej ocenionych kandydatów z korzenia:

- **Głębokość**: baza ustawiana w Ustawieniach (domyślnie **4** dla "Dowolna karta", **5** dla "Tylko król"; zakres 1–20). **[D]**: +4 do głębokości, gdy na planszy jest choć jedna pusta kolumna (rozgałęzienie eksploduje, bo prawie każda karta może tam wejść) — czyli domyślnie 8. **[K]**: bez tego bonusu (tylko Król może zająć puste miejsce, więc rozgałęzienie nie rośnie tak samo).
- **Szerokość wiązki na każdym poziomie**: **4** kandydatów, gdy zostało więcej niż 4 poziomy do końca; **6**, gdy zostało 4 lub mniej (bliżej liścia, taniej spróbować szerzej).
- **Budżet węzłów**: **60 000** na jednego kandydata z korzenia (każdy z 20 dostaje swój własny, niezależny budżet — nie jest dzielony).
- **Kolejność próbowania ruchów** na każdym poziomie: najpierw te dotykające tej samej kolumny co ruch, który doprowadził do tego węzła ("lokalność"), potem wg własnej oceny natychmiastowej.
- **Tablica transpozycji**: pozycje osiągnięte różną liczbą ruchów w tym samym wywołaniu są scalane — krótsza droga zawsze wygrywa, a różnica punktowa dłuższej drogi ponad to co dała droga krótsza jest ucinana (nie może być "premiowana" za sam objazd).
- **Premia za wygraną** (reguła 1): **12000 / numer_ruchu_wygrywającego** — im szybsza wygrana, tym więcej punktów; to zniechęca do przeciągania gry tylko po to, by "nafarmić" jeszcze jeden bonus reguły 3.
- **Premia za "uprzątniętą" planszę** (reguła 2, patrz rozdz. 6 co to znaczy): **[D] 4000** / **[K] 2000**, dzielone przez numer ruchu, przyznawane raz, dokładnie w chwili przejścia z "nieuprzątniętej" na "uprzątniętą".

---

## 5. Wybór najlepszego ruchu — trzypoziomowy remis

Kandydaci z korzenia sortowani są wg (w tej kolejności, każdy kolejny tylko przy remisie poprzedniego):

1. **Głęboki wynik skumulowany** (suma punktów za ruch własny + całą znalezioną kontynuację).
2. **Własny wynik natychmiastowy** tego ruchu (bez patrzenia w przyszłość) — przy remisie wygrywa ruch lepszy "tu i teraz".
3. **Ranga odsłanianej karty** — niższa wygrywa (odsłonięcie Asa cenniejsze niż Króla).

Ta kolejność, stały seed haszowania Zobrista i brak jakiejkolwiek losowości w całym pliku gwarantują, że cofnięcie automatycznego ruchu i ponowne jego zażądanie zawsze da **dokładnie ten sam** ruch.

---

## 6. Dwa twarde skróty omijające całe przeszukiwanie

- **Plansza "uprzątnięta"** (`isTrivialBoard`): każda kolumna jest pusta, ma jedną kartę, lub jest w całości jednym poprawnym sekwensem od góry. Wtedy rozdanie z rezerwy jest **obowiązkowe** (o ile rezerwa nie jest pusta) — natychmiast, bez liczenia punktów, z pominięciem reszty kandydatów, choćby jakiś inny ruch wyglądał punktowo lepiej.
- **[D] tylko, eksperymentalne**: gdy na planszy są **3 lub więcej** puste kolumny (i plansza nie jest "uprzątnięta"), natychmiast zajmij najlepiej punktowaną z nich (wg zwykłego `scoreMove`, bez przeszukiwania w głąb) i zagraj to — bez pełnego przeszukiwania. Powód: przy 3+ wolnych miejscach rozgałęzienie każdego INNEGO ruchu też eksploduje (prawie wszystko może wylądować na dowolnym z nich), więc pełne przeszukiwanie byłoby bardzo drogie i niewiele daje ponad "zajmij jedno z nadmiaru wolnych miejsc". **[K] nie ma tego skrótu** — tam zawsze pełne przeszukiwanie.

---

## 7. Anti-pętla i wykrywanie "utknięcia" (main.cpp, poziom PRAWDZIWYCH rozegranych ruchów — nie przeszukiwania)

To działa NA ŻYWO, dodatkowo ponad wszystko powyższe, i jest jedynym mechanizmem chroniącym przed nieskończonym powtarzaniem:

- **Historia 20 ostatnich prawdziwych ruchów** (`AUTO_HISTORY_SIZE=20`) — odrzuca kandydata identycznego z jednym z 20 ostatnich LUB jego dokładnym odwróceniem.
- **Historia 20 ostatnich stanów planszy** (`BOARD_HISTORY_SIZE=20`) — dodatkowo odrzuca kandydata, którego WYNIKOWA plansza powtarza którykolwiek z 20 ostatnich stanów (łapie dłuższe cykle, których sam poprzedni punkt nie widzi).
- **Licznik "brak postępu na stosach"** (`FOUNDATION_STALL_LIMIT=50`) — jeśli przez 50 kolejnych prawdziwych ruchów łączna liczba kart na stosach nie pobiła swojego dotychczasowego maksimum, automat się zatrzymuje (Samograj bez końca: zamiast stać, ogłasza to rozdanie przegranym i rozdaje kolejne — patrz dalej).
- **Ostatnia deska ratunku**: gdy WSZYSTKIE ranked kandydaci odrzuceni powyższymi regułami, zaakceptuj dowolnego kandydata (poza rozdaniem — to zawsze bezpieczne), którego wynikowa plansza nie powtarza żadnego z 20 ostatnich stanów — nawet jeśli sam ruch jest "świeżym" odwróceniem czegoś starszego niż 20 ruchów.
- **Samograj bez końca (Shift+1)**: gdy silnik faktycznie nie ma nic do zagrania (powyższe wyczerpane), ale gra nadal widzi jakiś legalny ruch — rozdanie liczy się jako przegrane i od razu rozdawane jest kolejne, zamiast zatrzymywać całą serię. Rozdanie bez ani jednego wykonanego ruchu nadal zatrzymuje serię (ochrona przed nieskończoną pętlą "od razu utknięte").

---

## 8. Solver — dla porównania (skrót; pełny opis w komentarzach `solver.h`)

Zupełnie inny silnik, używany tylko przez przycisk **Solver**, działający w pamięci na ZNANYM rozdaniu (zna kolejność rezerwy — bo nie gra na żywo, tylko szuka dowodu że rozdanie jest wygrywalne):

- Przeszukiwanie w głąb z **prawdziwymi nawrotami**: cięcie "brak postępu" — jeśli od N ruchów żaden nie pobił najlepszego dotąd wyniku NA TEJ ŚCIEŻCE, gałąź jest porzucana (cofnięcie), N rośnie z numerem próby.
- Miara postępu: karty na stosach ×3 + suma poprawnie ułożonych "spodów" kolumn (tolerancyjna na chwilowe zdjęcie karty ze stosu z powrotem na stół — bywa konieczne, zwłaszcza [K]).
- Wiele niezależnych prób równolegle (wątki), z rosnącym budżetem węzłów (120 000 → 2 000 000, ×1,35 na próbę) i losowym szumem w kolejności ruchów od 2. próby (różnicuje próby).
- Kolejne próby budują na najlepszej dotąd osiągniętej linii (część — reszta nadal startuje od zera dla różnorodności).
- Ruch stos→kolumna: **−50** (złagodzone z −200 — okazało się kluczowe dla trudnych rozdań wymagających buforowania).
- **Limit bezpieczeństwa: 2 godziny łącznie na jedno rozdanie** (licząc przez wszystkie wznowienia) — po przekroczeniu solver poddaje się automatycznie (bez pytania), zapisuje postęp i przechodzi do następnego rozdania w kolejce. Bez tego, przy domyślnej odpowiedzi "tak, szukaj dalej" na pytanie co 5 minut, solver szukałby w nieskończoność, jeśli nikt nie odpowie inaczej — bo przeszukanie NIGDY nie wyczerpuje się samo z siebie (przestrzeń rozdania jest zbyt ogromna), więc bez jawnego limitu zawsze kończy się czasem/budżetem, nigdy udowodnieniem "to się nie da".
