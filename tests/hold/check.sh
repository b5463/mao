#!/usr/bin/env bash
# The hold gesture (components/mao_app/mao_hold.c) on the host.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
APP=../../components/mao_app
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -Ishim -I$APP/include -I../../components/mao_system/include \
    test_hold.c $APP/mao_hold.c -o test_hold.exe
./test_hold.exe
