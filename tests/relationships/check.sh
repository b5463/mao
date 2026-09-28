#!/usr/bin/env bash
# Relationship table host tests (persistence / model only, synthetic ids).
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
REL=../../components/mao_relationships
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -Ishim -I$REL -I../../components/odd_bus/include \
    test_rel.c $REL/mao_rel_table.c -o test_rel.exe
./test_rel.exe
# Model-based fuzz (M4.1): random pair / re-pair / forget / rename / reboot
# with injected failures - table, fake flash and model must always agree.
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -Ishim -I$REL -I../../components/odd_bus/include     test_rel_fuzz.c $REL/mao_rel_table.c -o test_rel_fuzz.exe
./test_rel_fuzz.exe 40 20000
