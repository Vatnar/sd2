#!/usr/bin/env zsh
set -e
cd "$(git rev-parse --show-toplevel)"
perf record -F 999 --call-graph dwarf -o perf.data ./build/sd2
perf report -i perf.data --dsos=sd2
