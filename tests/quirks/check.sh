#!/usr/bin/env bash
# When MAO's quirks may happen (components/mao_app/mao_quirks.c) on the host.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
APP=../../components/mao_app
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -I$APP/include test_quirks.c $APP/mao_quirks.c -o test_quirks.exe
./test_quirks.exe
