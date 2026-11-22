#!/bin/sh
# Verifies sat.cpp's copy of mi_umin_imm() matches iris_query.c line for line.
# Usage: sh check-sync.sh [path/to/iris_query.c]
set -eu
here=$(dirname "$0")
src=${1:-$here/../../../../mesa/src/gallium/drivers/iris/iris_query.c}
extract() { awk '/^mi_umin_imm\(/{p=1; print prev} p{print} p&&/^}/{exit} {prev=$0}' "$1"; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
extract "$src" > "$tmp/a"; extract "$here/sat.cpp" > "$tmp/b"
[ -s "$tmp/a" ] || { echo "mi_umin_imm not found in $src"; exit 1; }
if diff -u "$tmp/a" "$tmp/b"; then echo "mi_umin_imm in sync ($(wc -l < "$tmp/a") lines)"; else echo "mi_umin_imm OUT OF SYNC"; exit 1; fi
