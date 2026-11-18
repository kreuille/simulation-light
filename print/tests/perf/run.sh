#!/usr/bin/env bash
# Simulation de performance du banc MSLA : budget temps réel, coût de calcul de la
# détection, robustesse au bruit. Outil d'analyse (pas un test pass/fail) ; les
# conclusions sont figées dans RAPPORT.md. Nécessite python3, g++ et node.
set -euo pipefail
cd "$(dirname "$0")"
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "######## 1. Budget temps réel (HX711, liaisons série) ########"
python3 budget.py
echo

echo "######## 2. Coût de calcul de la détection (benchmark) ########"
node ../lib/export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I ../firmware_host/mock -I "$lib" bench.cpp "$lib"/msla_*.cpp -o "$tmp/bench"
"$tmp/bench" "$tmp" residue_m9
echo

echo "######## 3. Robustesse au bruit du signal (~2 min) ########"
node noise_sweep.js
