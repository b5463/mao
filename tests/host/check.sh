#!/usr/bin/env bash
# MAO host unit tests (perception engine, battery policy), like the other
# suites' check.sh. Sanitizers are left to run.sh / make (gcc, clang).
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
ROOT=../..
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=c99 -Wall -Wextra -Wshadow -Wpedantic -Werror -Wno-unused-parameter \
    -I. -I$ROOT/components/mao_perception/include -I$ROOT/components/mao_perception/core \
    -I$ROOT/components/mao_battery/include \
    test_main.c test_percept.c test_policy.c \
    $ROOT/components/mao_perception/core/percept_engine.c $ROOT/components/mao_battery/mao_battery_policy.c \
    -o test_host.exe -lm
./test_host.exe
