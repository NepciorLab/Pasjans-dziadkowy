# Pasjans Dziadkowy

Pasjans na dwie talie (104 karty) dla Windows — C++ / Win32 / GDI+ / Direct2D.

[![Latest release](https://img.shields.io/github/v/release/NepciorLab/Pasjans-dziadkowy)](https://github.com/NepciorLab/Pasjans-dziadkowy/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## O projekcie i udziale AI

Ten program jest rozwijany przy współpracy z [Claude](https://claude.ai) (Anthropic) —
znaczna część kodu (silnik gry, sztuczna inteligencja automatycznego układania,
solver, interfejs użytkownika, mechanizm aktualizacji) powstała w rozmowie
z AI na podstawie wymagań i decyzji autora. Pliki źródłowe niosące ten wkład
mają o tym notkę u góry. Historia zmian: [CHANGES.md](CHANGES.md).

## Funkcje

- Klasyczny pasjans na dwie talie, dwa tryby: "Tylko król" i "Dowolna karta" na wolne miejsce.
- Automatyczne układanie (Samograj / Samograj bez końca) z konfigurowalną głębokością przewidywań SI.
- **Solver** — w pełni odtwarzalne, offline przeszukiwanie z powrotami (backtracking) dla konkretnego,
  znanego rozdania; znajduje rozwiązania nawet dla partii, których nie rozwiązał automat.
- Zapisywanie/wczytywanie gier (`.dat`), pełna historia cofania/ponawiania.
- Statystyki, rekordy, listy rozdań wygranych/przegranych/nierozwiązywalnych.
- Automatyczne sprawdzanie aktualizacji przy starcie (możliwość wyłączenia w Ustawieniach).

## Budowanie ze źródeł

Brak natywnego kompilatora C++? Program buduje się przez [Zig](https://ziglang.org/)
jako kompilator mingw-w64 (`zig c++`), bez instalowania Visual Studio ani MinGW:

```
pip install ziglang
```

Następnie:

```
powershell -File build.ps1
```

Plik wynikowy: `build/PasjansD.exe`.

## Aktualizacje

Program przy starcie (opcjonalnie — przełącznik w Ustawieniach → Ogólne) sprawdza
w tym repozytorium (GitHub Releases), czy dostępna jest nowsza wersja. Jeśli tak,
pyta o zgodę i po potwierdzeniu pobiera oraz podmienia plik `.exe` samodzielnie.

Aby opublikować nową wersję: zbuduj `PasjansD.exe`, utwórz w tym repozytorium
nowy Release z tagiem `vMAJOR.MINOR.PATCH` (musi być zgodny z `APP_VERSION`
w `src/main.cpp`) i dołącz do niego plik dokładnie nazwany `PasjansD.exe`.

## Pobieranie

Gotowy plik `PasjansD.exe` (bez kompilowania) — [najnowsze wydanie](https://github.com/NepciorLab/Pasjans-dziadkowy/releases/latest).

## Licencja

[MIT](LICENSE) — używaj, kopiuj, modyfikuj i rozpowszechniaj dowolnie, z zachowaniem informacji o autorstwie.
