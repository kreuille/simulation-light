#!/usr/bin/env bash
# MSLA bench performance simulation: real-time budget, detection compute cost, noise
# robustness. Analysis tool (not a pass/fail test); the conclusions are frozen in
# RAPPORT.md. Needs python3, g++ and node.
set -euo pipefail
cd "$(dirname "$0")"
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "######## 1. Real-time budget (HX711, serial links) ########"
python3 budget.py
echo

echo "######## 2. Detection compute cost (benchmark) ########"
node ../lib/export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I ../firmware_host/mock -I "$lib" bench.cpp "$lib"/msla_*.cpp -o "$tmp/bench"
"$tmp/bench" "$tmp" residue_m9
echo

echo "######## 3. Signal noise robustness (~2 min) ########"
node noise_sweep.js
