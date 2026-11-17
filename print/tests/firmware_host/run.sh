#!/usr/bin/env bash
# Test du contrôle du bac du firmware, sur PC (g++, python3 + numpy).
set -euo pipefail
cd "$(dirname "$0")"
tmp=$(mktemp -d)
python3 generate_cases.py "$tmp"
g++ -std=c++17 -O1 -I mock -w harness.cpp -o "$tmp/harness"
out=$("$tmp/harness" "$tmp"/a_ref.csv "$tmp"/b_propre.csv "$tmp"/c_100um.csv "$tmp"/d_100um_bord.csv "$tmp"/e_300um.csv "$tmp"/f_60um.csv "$tmp"/g_propre_niveau+2.csv "$tmp"/h_plan_parallele.csv "$tmp"/i_plan_incline.csv)
echo "$out"
check() { echo "$out" | grep -A0 -B1 -- "<- .*$1" | head -1 | grep -q -- "$2" || { echo "ECHEC : $1 devrait contenir « $2 »"; exit 1; }; }
check b_propre.csv "CONTROLE OK"
check c_100um.csv "CONTROLE RESIDU"
check d_100um_bord.csv "CONTROLE RESIDU"
check e_300um.csv "risque eleve"
check f_60um.csv "CONTROLE OK"
check g_propre_niveau+2.csv "CONTROLE OK"
check h_plan_parallele.csv "plateau parallele"
check i_plan_incline.csv "cote le plus bas avant droit"
echo "Tous les tests du firmware passent."
rm -rf "$tmp"
