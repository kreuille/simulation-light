#!/usr/bin/env bash
# Builds and runs the quickstart detection example on the demo CSV.
# Usage: bash run.sh [your_data.csv]
set -euo pipefail
cd "$(dirname "$0")"

LIB=../lib            # holds msla_detect/
SRC=../lib/msla_detect
CSV="${1:-capteurs_demo.csv}"

echo "# compiling quickstart_detection ..."
c++ -std=c++17 -O2 -I"$LIB" \
    quickstart_detection.cpp \
    "$SRC"/msla_filtres.cpp "$SRC"/msla_couche.cpp "$SRC"/msla_stabilite.cpp "$SRC"/msla_bac.cpp \
    -o quickstart_detection

echo "# running on $CSV"
echo
./quickstart_detection "$CSV"
