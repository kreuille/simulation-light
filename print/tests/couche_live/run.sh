#!/usr/bin/env bash
# Détection par couche EN DIRECT du firmware : rejoue les scénarios du simulateur à
# travers le chemin réel du firmware (commandes L/U + échantillons, phases déduites
# du mouvement Z) et vérifie que le défaut est détecté à la bonne couche, sans
# fausse alerte sur une impression normale. Nécessite node et g++.
set -euo pipefail
cd "$(dirname "$0")"
lib=../../lib/msla_detect
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

node ../lib/export_scenarios.js "$tmp" >/dev/null
g++ -std=c++17 -O2 -w -I ../firmware_host/mock -I "$lib" harness.cpp "$lib"/msla_*.cpp -o "$tmp/harness"
out=$("$tmp/harness" "$tmp" normal_m9 detach_m9 support_m9 residue_m9 fep_m9 residue_rrs normal_rrs)
echo "$out"

val() { echo "$out" | awk -v s="$1" '$1=="DET"&&$2==s{for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="premier")print a[2]}}'; }
att() { local got; got=$(val "$1"); [ "$got" = "$2" ] || { echo "ECHEC : $1 premier=$got, attendu $2"; exit 1; }; }

att normal_m9 -1       # impression normale : aucune alerte
att normal_rrs -1
att detach_m9 30       # décollement détecté à la couche de l'incident
att support_m9 30      # rupture de supports
att residue_m9 30      # résidu sous la pièce
att residue_rrs 30
# film détendu : détecté en moins de 5 couches après le début (couche 30)
fep=$(val fep_m9); [ "$fep" -ge 30 ] && [ "$fep" -le 35 ] || { echo "ECHEC : fep_m9 premier=$fep, attendu 30..35"; exit 1; }

echo "Tous les tests de détection par couche en direct passent."
