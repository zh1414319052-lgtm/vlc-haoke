#!/bin/sh
# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
# Build the native Windows protocol acceptance test with the pinned Qt contrib.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix=$1
output=$2
build=$3
mkdir -p "$build"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_PATH=
x86_64-w64-mingw32-gcc -O2 -c "$src/haoke/crt_argc_compat.c" -o "$build/crt.o"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -c "$src/haoke/update_crt_compat.c" -o "$build/update-crt.o"
x86_64-w64-mingw32-g++ -std=c++11 -O2 -Wall -Wextra -Werror -DQT_USE_QSTRINGBUILDER \
  -Wno-error=deprecated-copy -static -static-libgcc -static-libstdc++ \
  -I"$prefix/include/QtCore" -I"$prefix/include" \
  "$src/haoke/tests/test_update_protocol.cpp" "$build/crt.o" "$build/update-crt.o" \
  -L"$prefix/lib" -lQt5Core -lgcrypt -lgpg-error -lqtpcre \
  -lole32 -luuid -lws2_32 -ladvapi32 -lshell32 -luser32 -lkernel32 -lmpr -lz -lwinpthread -o "$output"
