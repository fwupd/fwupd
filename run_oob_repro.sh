#!/usr/bin/env bash
set -euo pipefail

repo_root="$(dirname "${BASH_SOURCE[0]}")"
cd "$repo_root"

if [[ ! -x ".ossfuzz/out/logitech-bulkcontroller-protocol_fuzzer" ]]; then
  echo "Missing .ossfuzz/out/logitech-bulkcontroller-protocol_fuzzer"
  echo "Run: ./build_and_repro.sh"
  exit 1
fi

python3 "fuzzing/generate_oob_poc.py" -o "poc_oob_read.bin"
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:halt_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1 \
".ossfuzz/out/logitech-bulkcontroller-protocol_fuzzer" "poc_oob_read.bin"
