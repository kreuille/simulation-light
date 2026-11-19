#!/usr/bin/env bash
# Parity: simulator (JS) <-> msla_detect library (C++).
# Exports scenarios from the simulator, then checks that the C++ library gives the same
# per-layer learning verdicts and the same settle times. Needs node and g++.
set -euo pipefail
cd "$(dirname "$0")"
racine=../../..
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

node export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I "$lib" parite.cpp "$lib"/msla_*.cpp -o "$tmp/parite"
"$tmp/parite" "$tmp"
