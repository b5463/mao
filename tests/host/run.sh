#!/bin/sh
# Run MAO's host unit tests (perception engine, power policy).
#   tests/host/run.sh            local cc/gcc + make if available, else Docker
#   tests/host/run.sh --docker   always in espressif/idf:v6.0.3 (gcc, Linux)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

if [ "$1" != "--docker" ] && command -v make >/dev/null 2>&1 && \
   { command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1; }; then
    exec make -C "$HERE" BUILD="${MAO_HOST_BUILD:-build}" test
fi

exec docker run --rm -v "$ROOT":/project -w /project/tests/host espressif/idf:v6.0.3 \
    sh -c 'make CC=gcc BUILD=build-docker test'
