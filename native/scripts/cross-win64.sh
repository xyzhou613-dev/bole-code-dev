#!/usr/bin/env sh
# Syntax/link smoke for the Win32 platform layer on a POSIX host.
#
# This is not a substitute for the MSVC release build: it does not exercise
# /MT, the import library, or runtime behavior. It catches type/API drift in
# platform_win32.c and verifies that the DLL exports exactly the public ABI.
#
# Usage:
#   native/scripts/cross-win64.sh [output-dir]
#   CC=x86_64-w64-mingw32-gcc native/scripts/cross-win64.sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/build-win64"}
cc=${CC:-x86_64-w64-mingw32-gcc}

mkdir -p "$out"

common="-std=c99 -Wall -Wextra -Wpedantic -Werror -I$root/include -I$root/src -D_WIN32_WINNT=0x0601"
objects=""

for source in agent error json rpc transport platform_win32; do
  "$cc" $common -DBOLE_AGENT_BUILD -c "$root/src/$source.c" -o "$out/$source.o"
  objects="$objects $out/$source.o"
done

# --exclude-all-symbols keeps MinGW's auto-export from leaking internals.
"$cc" -shared -Wl,--exclude-all-symbols -o "$out/bole_agent.dll" $objects

"$cc" $common -DBOLE_AGENT_STATIC -o "$out/bole_test_json.exe" \
  "$root/tests/test_json.c" "$root/src/json.c"
"$cc" $common -DBOLE_AGENT_STATIC -o "$out/bole_test_error.exe" \
  "$root/tests/test_error.c" "$root/src/error.c"

echo "cross-built:"
echo "  $out/bole_agent.dll"
echo "  $out/bole_test_json.exe"
echo "  $out/bole_test_error.exe"
