#!/usr/bin/env zsh
set -e
cd "$(git rev-parse --show-toplevel)"
perf record -F 999 -g -o perf.data ./build/sd2
