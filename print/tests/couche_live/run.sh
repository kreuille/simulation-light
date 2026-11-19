#!/usr/bin/env bash
# LIVE per-layer detection of the firmware: replays the simulator scenarios through the
# real firmware path (L/U commands + samples, phases derived from the Z movement) and
# checks that the fault is detected at the correct layer, with no false alarm on a normal
# print. Needs node and g++.
set -euo pipefail
cd "$(dirname "$0")"
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

node ../lib/export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I ../firmware_host/mock -I "$lib" harness.cpp "$lib"/msla_*.cpp -o "$tmp/harness"
out=$("$tmp/harness" "$tmp" normal_m9 detach_m9 support_m9 residue_m9 fep_m9 residue_rrs normal_rrs)
echo "$out"

val() { echo "$out" | awk -v s="$1" '$1=="DET"&&$2==s{for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="first")print a[2]}}'; }
att() { local got; got=$(val "$1"); [ "$got" = "$2" ] || { echo "FAIL: $1 first=$got, expected $2"; exit 1; }; }

att normal_m9 -1       # normal print: no alarm
att normal_rrs -1
att detach_m9 30       # detachment detected at the incident layer
att support_m9 30      # support failure
att residue_m9 30      # debris under the part
att residue_rrs 30
# loose film: detected in less than 5 layers after the start (layer 30)
fep=$(val fep_m9); [ "$fep" -ge 30 ] && [ "$fep" -le 35 ] || { echo "FAIL: fep_m9 first=$fep, expected 30..35"; exit 1; }

echo "All live per-layer detection tests pass."
