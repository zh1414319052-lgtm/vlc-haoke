#!/bin/sh
# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
# Rebuild the real VLC Qt UI; preserve the official media core and modules.
set -eu
src=$1
prefix=$2
build=$3
stage=${4:-qa}
case "$stage" in qa|fixture|formal) ;; *) printf 'Invalid build stage\n' >&2; exit 1;; esac
mkdir -p "$build/abi-import"
if [ -f "$build/haoke-stage.txt" ] && [ "$(cat "$build/haoke-stage.txt")" != "$stage" ]; then
    printf 'Use a separate build directory for each stage\n' >&2; exit 1
fi
if [ "$stage" != qa ] && [ -f "$build/Makefile" ] && [ ! -f "$build/haoke-stage.txt" ]; then
    printf 'Cannot reuse an unclassified build for formal release\n' >&2; exit 1
fi
printf '%s\n' "$stage" > "$build/haoke-stage.txt"
export PATH="$prefix/bin:$PATH"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_PATH=
export PKG_CONFIG_SYSROOT_DIR=
export CC=x86_64-w64-mingw32-gcc
export CXX=x86_64-w64-mingw32-g++
export CPPFLAGS="-I$prefix/include"
identity="$src/haoke/release_identity.json"
if [ "$stage" = fixture ]; then
    identity=${5:?Fixture stage requires an explicit isolated public identity JSON}
fi
if [ "$stage" != qa ]; then
    python3 "$src/haoke/generate_update_config.py" "$identity" "$build/haoke_release_identity.hpp"
    export CPPFLAGS="$CPPFLAGS -I$build -DHAOKE_FORMAL_UPDATES"
fi
export LDFLAGS="-L$prefix/lib"
export CFLAGS="-O2 -g0"
export CXXFLAGS="-O2 -g0"
cd "$build"
if [ ! -f Makefile ]; then
    "$src/vlc-3.0.24/configure" --host=x86_64-w64-mingw32 \
        --build=x86_64-linux-gnu --with-contrib="$prefix" \
        --disable-vlc --disable-skins2 --disable-lua --enable-qt --enable-nls \
        --enable-update-check --enable-sout > configure-output.log 2>&1 || {
            tail -70 configure-output.log; exit 1;
        }
fi
{
  printf 'LIBRARY libvlccore.dll\nEXPORTS\n'
  cat "$src/vlc-3.0.24/src/libvlccore.sym"
} > abi-import/libvlccore.def
x86_64-w64-mingw32-dlltool -d abi-import/libvlccore.def -l abi-import/libvlccore.dll.a
make -C compat -j4 > compat-output.log 2>&1 || { tail -60 compat-output.log; exit 1; }
make -C src ../include/vlc_about.h > headers-output.log 2>&1 || { tail -50 headers-output.log; exit 1; }
printf 'haoke-qt-generated: $(nodist_libqt_plugin_la_SOURCES)\n' > "$build/qt-generated.mk"
make -C modules -j4 -f Makefile -f "$build/qt-generated.mk" haoke-qt-generated \
    > qt-generated-output.log 2>&1 || { tail -65 qt-generated-output.log; exit 1; }
make -C modules module.rc.lo > module-resource-output.log 2>&1 || { tail -50 module-resource-output.log; exit 1; }
"$CC" -O2 -g0 -Wall -Wextra -Werror -c "$src/haoke/crt_argc_compat.c" -o "$build/crt_argc_compat.o"
printf 'libqt_plugin_la_LIBADD += $(HAOKE_UPDATE_LINK)\n' > "$build/haoke-link.mk"
update_link=
if [ "$stage" != qa ]; then
    "$CC" -O2 -g0 -Wall -Wextra -Werror -c "$src/haoke/update_crt_compat.c" -o "$build/update_crt_compat.o"
    update_link="$build/update_crt_compat.o -lgcrypt -lgpg-error -lws2_32 -lwinhttp"
fi
make -C modules -j4 -f Makefile -f "$build/haoke-link.mk" libqt_plugin.la \
    HAOKE_UPDATE_LINK="$update_link" LTLIBVLCCORE="$build/abi-import/libvlccore.dll.a" \
    LDFLAGS="$LDFLAGS $build/crt_argc_compat.o" \
    > qt-output.log 2>&1 || { tail -90 qt-output.log; exit 1; }
x86_64-w64-mingw32-strip --strip-unneeded "$build/modules/.libs/libqt_plugin.dll"
python3 - "$src" "$build" "$stage" "$identity" <<'PY'
import hashlib, json, sys
from pathlib import Path
source, build = Path(sys.argv[1]), Path(sys.argv[2])
sys.path.insert(0, str(source / 'haoke'))
from version import VERSION
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
meta = {'stage':sys.argv[3], 'version':VERSION,
        'qt_plugin_sha256':digest(build/'modules/.libs/libqt_plugin.dll')}
if sys.argv[3] != 'qa':
    meta['release_identity_sha256'] = digest(Path(sys.argv[4]))
(build/'haoke-qt-build.json').write_text(json.dumps(meta, indent=2), encoding='utf-8')
PY
printf 'Qt plugin built: %s/modules/.libs/libqt_plugin.dll\n' "$build"
