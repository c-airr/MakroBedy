#!/bin/bash
set -e
cd "$(dirname "$0")"
clang++ -std=c++17 -O2 -o makrobedy_mac makrobedy/makrobedy_mac.mm \
  -framework Cocoa -framework ApplicationServices -framework CoreFoundation
echo "Gotowe: ./makrobedy_mac"
