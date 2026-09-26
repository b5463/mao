#!/usr/bin/env bash
# ODD link protocol logic on the host (codec, envelope/replay, pairing state
# machines, credential store). Crypto is a labelled TEST DOUBLE; primitive
# correctness is verified on the target ("mao link selftest").
#   CC="gcc" ./check.sh        (default: python -m ziglang cc)
set -e
cd "$(dirname "$0")"
L=../../components/odd_link
CC=${CC:-"python -m ziglang cc"}
$CC -O1 -std=gnu11 -Wall -Wextra -Werror -Wno-missing-field-initializers -I$L/include \
    test_link.c fake_crypto.c $L/odd_link_kdf.c $L/odd_link_frame.c $L/odd_link_env.c $L/odd_link_pair.c \
    -o test_link.exe
./test_link.exe
