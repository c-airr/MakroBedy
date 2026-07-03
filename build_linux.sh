#!/bin/bash
set -e
cd "$(dirname "$0")"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Brak pkg-config. Zainstaluj: sudo apt install pkg-config"
    exit 1
fi

for pkg in gtk+-3.0 x11 xtst; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        case "$pkg" in
            gtk+-3.0) echo "Brak GTK3. Zainstaluj: sudo apt install libgtk-3-dev" ;;
            x11) echo "Brak X11. Zainstaluj: sudo apt install libx11-dev" ;;
            xtst) echo "Brak XTest. Zainstaluj: sudo apt install libxtst-dev" ;;
        esac
        exit 1
    fi
done

g++ -std=c++17 -O2 -o makrobedy_linux makrobedy/makrobedy_linux.cpp \
    $(pkg-config --cflags gtk+-3.0) \
    $(pkg-config --libs gtk+-3.0 x11) -lXtst -lpthread

echo "Gotowe: ./makrobedy_linux"
echo "Uwaga: makro dziala tylko na sesji X11, nie na Waylandzie."
