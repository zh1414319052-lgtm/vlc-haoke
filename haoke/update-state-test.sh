#!/bin/sh
# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
# Native Windows state/lifecycle policy test. No UI, no network, no credentials.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix=$1
output=$2
build=$3
mkdir -p "$build"
x86_64-w64-mingw32-gcc -O2 -c "$src/haoke/crt_argc_compat.c" -o "$build/state-crt.o"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -c "$src/haoke/update_crt_compat.c" -o "$build/state-update-crt.o"
x86_64-w64-mingw32-g++ -std=c++11 -O2 -Wall -Wextra -Werror -DQT_USE_QSTRINGBUILDER \
  -static -static-libgcc -static-libstdc++ \
  -isystem "$prefix/include/QtCore" -isystem "$prefix/include/QtGui" \
  -isystem "$prefix/include/QtWidgets" -isystem "$prefix/include" \
  "$src/haoke/tests/test_update_state.cpp" "$build/state-crt.o" "$build/state-update-crt.o" \
  -L"$prefix/lib" -lQt5Core -lgcrypt -lgpg-error -lqtpcre \
  -lole32 -luuid -lws2_32 -ladvapi32 -lshell32 -luser32 -lkernel32 -lmpr -lz -lwinpthread -o "$output"
