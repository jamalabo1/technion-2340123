#!/usr/bin/env bash
#
# tests/run_tests.sh
#
# Convenience orchestrator: builds the server (via CMake) and the test client,
# launches the server on free-ish ports, runs the test suite, then tears down.
#
# Usage:
#   ./run_tests.sh [tcp_port] [udp_port] [threads] [queue_size] [debug_sleep]
# Defaults:
#   tcp=5003 udp=5004 threads=8 queue=16 debug_sleep=0
#
# Notes:
#  - The server's atomic_inc/dec use x86 inline asm, so it builds/runs on x86
#    Linux (e.g. the course machines / CLion on Linux), not on Apple Silicon.
#  - Run from inside the tests/ directory or anywhere; paths are resolved
#    relative to this script.

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

TCP="${1:-5003}"
UDP="${2:-5004}"
THREADS="${3:-8}"
QUEUE="${4:-16}"
SLEEP_T="${5:-0}"

BUILD="$ROOT/build-tests"

echo ">> Building server (cmake) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" >/dev/null || { echo "cmake configure failed"; exit 1; }
cmake --build "$BUILD" --target server >/dev/null || { echo "server build failed"; exit 1; }

echo ">> Building test client"
make -C "$HERE" >/dev/null || { echo "test client build failed"; exit 1; }

# The server resolves static files relative to ./public in its CWD. CMake's
# copy_public target uses wrong source paths, so stage public/ ourselves.
echo ">> Staging public/ into $BUILD"
mkdir -p "$BUILD/public"
cp -f "$ROOT/public/"* "$BUILD/public/" 2>/dev/null
if [ ! -f "$BUILD/public/home.html" ]; then
    echo "WARNING: $BUILD/public/home.html missing — GET tests will 404"
fi

echo ">> Launching server: ./server $TCP $UDP $THREADS $QUEUE $SLEEP_T"
( cd "$BUILD" && ./server "$TCP" "$UDP" "$THREADS" "$QUEUE" "$SLEEP_T" ) &
SRV_PID=$!

cleanup() {
    kill "$SRV_PID" 2>/dev/null
    wait "$SRV_PID" 2>/dev/null
}
trap cleanup EXIT

# give it a moment to bind
sleep 1

echo ">> Running test suite"
"$HERE/test_client" localhost "$TCP" "$UDP" "$THREADS" home.html
RC=$?

exit $RC
