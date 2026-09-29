#!/usr/bin/env bash
# MAO's power ladder and first-touch rules (components/mao_app/mao_power.c) on the host.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
APP=../../components/mao_app
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -I$APP/include test_power.c $APP/mao_power.c -o test_power.exe
./test_power.exe
