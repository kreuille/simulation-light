#!/usr/bin/env bash
# Test of the firmware serial protocol (Serial2), on a PC (g++, python3 + numpy).
set -euo pipefail
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 ../firmware_host/generate_cases.py "$tmp"
g++ -std=c++17 -O1 -I ../firmware_host/mock -w harness.cpp ../../lib/msla_detect/msla_*.cpp -o "$tmp/harness"
# 1st file = reference (clean vat), the next ones = control tests via "V"
out=$("$tmp/harness" "$tmp"/a_ref.csv "$tmp"/b_propre.csv "$tmp"/c_100um.csv "$tmp"/f_60um.csv "$tmp"/e_300um.csv)
echo "$out"

check() { echo "$out" | grep -q -- "$1" || { echo "FAIL: response \"$1\" expected"; exit 1; }; }
check "A1 RDY"
check "A2 ERR"
check "B VAT OK  <- .*b_propre.csv"
check "B VAT RESIDU  <- .*c_100um.csv"
check "B VAT OK  <- .*f_60um.csv"
check "B VAT RESIDU  <- .*e_300um.csv"
echo "All protocol tests pass."
