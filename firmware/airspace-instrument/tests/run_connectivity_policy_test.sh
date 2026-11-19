#!/bin/sh
# AirESP32ace
# Created by VM, November 2026
set -eu
test_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/ares-connect.XXXXXX")"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$test_dir/../main" "$test_dir/connectivity_policy_test.c" -o "$build_dir/check"
"$build_dir/check"
# Exercise the actual IDF request routine with a transport that models header
# read-ahead, incomplete/chunked bodies and a lost connection.
awk '/^static char \*http_request_with_timeout\(/ { copy=1; found++ }
     copy { print } copy && /^}/ { copy=0; finished++ }
     END { if(found!=1 || finished!=1) exit 1 }' "$test_dir/../main/data.c" > "$build_dir/http_under_test.inc"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$build_dir" "$test_dir/provider_http_test.c" -o "$build_dir/http"
"$build_dir/http"
