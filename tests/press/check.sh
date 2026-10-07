#!/usr/bin/env bash
# The press gesture (components/mao_input/mao_press.c) on the host.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
INPUT=../../components/mao_input
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -I../hold/shim -I$INPUT/include -I../../components/mao_system/include \
    test_press.c $INPUT/mao_press.c -o test_press.exe
./test_press.exe
