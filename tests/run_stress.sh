#!/usr/bin/env bash
#
# tests/run_stress.sh
#
# Builds the server (CMake) + the stress client, stages public/, launches the
# server, runs the stress/perf suite, then tears down.
#
# Usage:
#   ./run_stress.sh [tcp] [udp] [threads] [queue] [debug_sleep] [scale]
# Defaults:
#   tcp=5003 udp=5004 threads=8 queue=16 debug_sleep=0 scale=1
#
# `scale` multiplies request counts for longer/heavier runs.

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

TCP="${1:-5003}"; UDP="${2:-5004}"; THREADS="${3:-8}"
QUEUE="${4:-16}"; SLEEP_T="${5:-0}"; SCALE="${6:-1}"

BUILD="$ROOT/cmake-build-debug"
#
#echo ">> Building server (cmake)"
#cmake -S "$ROOT" -B "$BUILD" >/dev/null || { echo "cmake configure failed"; exit 1; }
#cmake --build "$BUILD" --target server >/dev/null || { echo "server build failed"; exit 1; }
#
echo ">> Building stress client"
make -C "$HERE" stress_client >/dev/null || { echo "stress client build failed"; exit 1; }

#echo ">> Staging public/"
#mkdir -p "$BUILD/public"
#cp -f "$ROOT/public/"* "$BUILD/public/" 2>/dev/null
#[ -f "$BUILD/public/home.html" ] || echo "WARNING: public/home.html missing — GET perf will 404"

#echo ">> Launching server: ./server $TCP $UDP $THREADS $QUEUE $SLEEP_T"
#( cd "$BUILD" && ./server "$TCP" "$UDP" "$THREADS" "$QUEUE" "$SLEEP_T" ) &
#SRV_PID=$!
#cleanup() { kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; }
#trap cleanup EXIT
#sleep 1

echo ">> Running stress/perf suite (scale=$SCALE)"
"$HERE/stress_client" localhost "$TCP" "$UDP" "$THREADS" home.html "$SCALE"
exit $?
