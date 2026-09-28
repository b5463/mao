#!/usr/bin/env bash
# MAO's accent (components/mao_character/mao_character_accent.c) on the host,
# checked against the real expression library.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
COMP=../../components/mao_character
SHIM=../character_harness/shim
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -I$SHIM -I$SHIM/freertos -I$COMP -I$COMP/include \
    test_accent.c $COMP/mao_character_accent.c $COMP/mao_lark_library.c $COMP/mao_lark_states_*.c \
    $COMP/mao_lark_gen.c $COMP/mao_lark.c -lm -o test_accent.exe 2>&1 | grep -v 'warning' || true
./test_accent.exe
