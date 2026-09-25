#!/usr/bin/env bash
# Character regression harness: build against the current sources, run every
# scenario x 3 seeds, diff against expected.txt (the M2.5 behaviour baseline).
#   CC="zig cc" ./check.sh        (default: python -m ziglang cc, zig 0.16)
set -e
cd "$(dirname "$0")"
COMP=../../components/mao_character
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -w -Ishim -Ishim/freertos -I$COMP -I$COMP/include -DCONFIG_MAO_DEV_CONSOLE=1 \
    harness.c $COMP/*.c -lm -o harness.exe
: > actual.txt
for s in idle dial press react sleep menu transfer peek mind states looks previews; do
  for seed in 1 7 42; do ./harness.exe $s $seed >> actual.txt; done
done
if diff expected.txt actual.txt; then echo "character harness: $(wc -l < actual.txt) runs identical"; else echo "character harness: BEHAVIOUR CHANGED"; exit 1; fi
