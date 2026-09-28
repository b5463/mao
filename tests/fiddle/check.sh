#!/usr/bin/env bash
# Fiddling detection (components/mao_app/mao_fiddle.c) on the host.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
APP=../../components/mao_app
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -I$APP/include test_fiddle.c $APP/mao_fiddle.c -o test_fiddle.exe
./test_fiddle.exe
