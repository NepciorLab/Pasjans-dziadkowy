#!/bin/bash
# Budowanie Pasjans.exe za pomocą mingw-w64 (cross-compiler Linux -> Windows).
# Wymaga: apt-get install mingw-w64
# UWAGA: res/cards.rc wskazuje na obrazki kart w res/cards_png/*.png, których
# nie było w dostarczonych źródłach - jeśli ich brakuje, ten skrypt tworzy
# pusty "stub" zasobów, żeby program dało się zbudować, ale karty nie będą
# miały grafik (program się nie wywali, po prostu narysuje puste miejsca).
set -e
cd "$(dirname "$0")"
mkdir -p build
cd build

x86_64-w64-mingw32-windres ../res/app.rc -O coff -o app_res.o

if [ -d "../res/cards_png" ]; then
   x86_64-w64-mingw32-windres ../res/cards.rc -O coff -o cards_res.o
else
   echo "UWAGA: brak res/cards_png - budowanie bez grafik kart (stub)."
   printf '#include <windows.h>\r\nDUMMY RCDATA {1}\r\n' > cards_stub.rc
   x86_64-w64-mingw32-windres cards_stub.rc -O coff -o cards_res.o
fi

x86_64-w64-mingw32-g++ -std=c++17 -O2 -c ../src/main.cpp -o main.o

x86_64-w64-mingw32-g++ -std=c++17 -mwindows -static -static-libgcc -static-libstdc++ \
   -o PasjansD.exe main.o app_res.o cards_res.o \
   -lgdiplus -ld2d1 -ldwrite -lwindowscodecs -ldsound -lwinmm \
   -lcomctl32 -lcomdlg32 -lole32 -lgdi32 -luser32 -lshell32

echo "Zbudowano: build/PasjansD.exe"
