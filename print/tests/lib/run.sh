#!/usr/bin/env bash
# Parité simulateur (JS) ↔ bibliothèque msla_detect (C++).
# Exporte des scénarios depuis le simulateur, puis vérifie que la bibliothèque C++
# retrouve les mêmes verdicts d'apprentissage par couche et les mêmes temps de
# stabilisation. Nécessite node et g++.
set -euo pipefail
cd "$(dirname "$0")"
racine=../../..
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

node export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I "$lib" parite.cpp "$lib"/msla_*.cpp -o "$tmp/parite"
"$tmp/parite" "$tmp"
