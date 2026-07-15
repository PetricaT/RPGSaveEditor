#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -Wall -Wextra"
INCLUDES="-I../src -I../build/_deps/nlohmann_json-src/include"
LDFLAGS="-lz"

RED='\033[0;31m'
GREEN='\033[0;32m'
BOLD='\033[1m'
RESET='\033[0m'

pass=0
fail=0

run_test() {
    local name="$1" src="$2"
    printf "${BOLD}Building %s...${RESET}\n" "$name"
    if ! $CXX $CXXFLAGS -o "$name" "$src" $INCLUDES $LDFLAGS 2>&1; then
        printf "${RED}BUILD FAILED: %s${RESET}\n\n" "$name"
        fail=$((fail + 1))
        return
    fi
    printf "${BOLD}Running %s...${RESET}\n" "$name"
    if ./"$name" 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
    echo
}

run_test smoke_test    smoke_test.cpp
run_test test_json_compat test_json_compat.cpp

printf "${BOLD}=== Totals: ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET} ===\n" "$pass" "$fail"
exit "$fail"
