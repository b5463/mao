#!/usr/bin/env bash
# ODD device contract host tests (M4.0): codec structure, contract
# evaluation, capability -> control mapping. Synthetic vectors only.
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
B=../../components/odd_bus
D=../../components/mao_devices
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -Wno-missing-field-initializers -I../relationships/shim -I$B/include \
    -I$D/include test_contract.c $B/odd_codec.c $B/odd_contract.c $D/mao_device_view.c -o test_contract.exe
./test_contract.exe
