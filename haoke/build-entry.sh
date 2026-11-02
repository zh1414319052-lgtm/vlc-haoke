#!/bin/sh
# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
# Builds the actual upstream WinMain against the unchanged official LibVLC ABI.
set -eu
src=$1
build=$2
mkdir -p "$build"
python3 - "$src" "$build" <<'PY'
import re, runpy, sys
from pathlib import Path
source, build = map(Path, sys.argv[1:])
v = runpy.run_path(str(source / 'haoke/version.py'))
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+', v['FILE_VERSION']):
    raise ValueError('Invalid Windows file version')
if not re.fullmatch(r'[0-9A-Za-z.\-]+', v['VERSION']):
    raise ValueError('Invalid product version')
(build / 'haoke_version_resource.h').write_text(
    '#define HAOKE_VERSION_NUMBERS ' + v['FILE_VERSION'].replace('.', ',') + '\n'
    '#define HAOKE_VERSION_NAME "' + v['VERSION'] + '"\n'
    '#define HAOKE_FILE_VERSION "' + v['FILE_VERSION'] + '"\n', encoding='ascii')
PY
{
  printf 'LIBRARY libvlc.dll\nEXPORTS\n'
  cat "$src/vlc-3.0.24/lib/libvlc.sym"
} > "$build/libvlc.def"
x86_64-w64-mingw32-dlltool -d "$build/libvlc.def" -l "$build/libvlc.dll.a"
x86_64-w64-mingw32-windres --codepage=65001 \
  -I "$build" \
  -I "$src/vlc-3.0.24/share/icons" \
  -I "$src/vlc-3.0.24/extras/package/win32" \
  "$src/haoke/win_resource.rc" "$build/resource.o"
x86_64-w64-mingw32-gcc -std=gnu11 -O2 -Wall -Wextra \
  -Wno-cast-function-type -D_WIN32_WINNT=0x0601 -DNDEBUG \
  '-DPACKAGE_VERSION="3.0.24"' '-DPACKAGE_NAME="vlc"' \
  -I "$src/vlc-3.0.24/include" \
  "$src/vlc-3.0.24/bin/winvlc.c" "$build/resource.o" \
  "$build/libvlc.dll.a" -mwindows -static -lshell32 -ladvapi32 \
  -Wl,--dynamicbase,--nxcompat,--high-entropy-va \
  -o "$build/vlc.exe"
gcc -std=c11 -Wall -Wextra -Werror "$src/haoke/tests/test_win_language.c" -o "$build/test-language"
"$build/test-language"
g++ -std=c++11 -Wall -Wextra -Werror -pedantic "$src/haoke/tests/test_window_policy.cpp" -o "$build/test-window-policy"
"$build/test-window-policy"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror \
  "$src/haoke/tests/test_crt_argc.c" "$src/haoke/crt_argc_compat.c" \
  -o "$build/test-crt-argc.exe"
