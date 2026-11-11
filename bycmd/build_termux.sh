#!/data/data/com.termux/files/usr/bin/sh
# Builds eeeww on the phone with Termux clang, without ndk-build or CI.
# Same sources, include paths and flags as jni/Android.mk + jni/Application.mk.
#
#   pkg install clang ndk-sysroot binutils
#   sh build_termux.sh            -> libs/arm64-v8a/eeeww
#   sh build_termux.sh --install  -> also copies it to /data/local/tmp/bycmd (su)
set -eu

PROJ="$(cd "$(dirname "$0")" && pwd)"
OUT="$PROJ/libs/arm64-v8a"
BIN="$OUT/eeeww"
INSTALL_PATH="/data/local/tmp/bycmd"
CXX="${CXX:-clang++}"
BUILD_ID="${BYCMD_BUILD_ID:-termux-$(date +%Y%m%d-%H%M)}"

command -v "$CXX" >/dev/null 2>&1 || { echo "[!] $CXX not found: pkg install clang ndk-sysroot binutils"; exit 1; }
[ "$(uname -m)" = "aarch64" ] || echo "[!] host is $(uname -m), the profile expects arm64-v8a"

INCLUDES="
 -I$PROJ/includes
 -I$PROJ/includes/fonts
 -I$PROJ/includes/internal
 -I$PROJ/includes/internal/ImGui
 -I$PROJ/includes/internal/ImGui/backends
 -I$PROJ/includes/draw/ImGui
 -I$PROJ/includes/draw/ImGui/backends
 -I$PROJ/src
 -I$PROJ/src/ui"

SRCS="
 $PROJ/src/main.cpp
 $PROJ/src/ui/menu.cpp
 $PROJ/src/ui/bar.cpp
 $PROJ/src/ui/widgets/widgets.cpp
 $PROJ/src/func/visuals.cpp
 $PROJ/src/protect/oxorany.cpp
 $PROJ/includes/draw/Android_draw/draw.cpp
 $PROJ/includes/draw/Android_touch/Touch.cpp
 $PROJ/includes/draw/ImGui/imgui.cpp
 $PROJ/includes/draw/ImGui/imgui_draw.cpp
 $PROJ/includes/draw/ImGui/imgui_tables.cpp
 $PROJ/includes/draw/ImGui/imgui_widgets.cpp
 $PROJ/includes/draw/ImGui/backends/imgui_impl_android.cpp
 $PROJ/includes/draw/ImGui/backends/imgui_impl_opengl3.cpp"

CXXFLAGS="-std=c++17 -fno-rtti -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden
 -Oz -ffunction-sections -fdata-sections -fomit-frame-pointer -fmerge-all-constants -fno-ident
 -fPIE -Wno-error=format-security -DBYCMD_BUILD_ID=\"$BUILD_ID\""
LDFLAGS="-pie -static-libstdc++ -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,relro -Wl,-z,now
 -Wl,-z,noexecstack -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
 -llog -landroid -lEGL -lGLESv3"

mkdir -p "$OUT"
echo "[*] $($CXX --version | head -1)"
echo "[*] build id $BUILD_ID"
# shellcheck disable=SC2086
"$CXX" $CXXFLAGS $INCLUDES $SRCS -o "$BIN" $LDFLAGS

# The binary runs from /data/local/tmp as root, outside Termux: it must not
# depend on anything from the Termux prefix (libc++_shared.so and friends).
if command -v readelf >/dev/null 2>&1; then
    NEEDED="$(readelf -d "$BIN" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
    echo "[*] NEEDED: $NEEDED"
    for lib in $NEEDED; do
        case "$lib" in
            libc.so|libm.so|libdl.so|liblog.so|libandroid.so|libEGL.so|libGLESv3.so) ;;
            *) echo "[X] unexpected dependency $lib: it will not load outside Termux"; exit 1 ;;
        esac
    done
    if readelf -d "$BIN" | grep -q -E "RPATH|RUNPATH"; then
        echo "[!] binary carries an RPATH/RUNPATH into the Termux prefix (harmless if NEEDED is clean)"
    fi
else
    echo "[!] readelf missing (pkg install binutils): dependency check skipped"
fi
echo "[OK] $BIN ($(wc -c < "$BIN") bytes)"
sha256sum "$BIN" 2>/dev/null || true

if [ "${1:-}" = "--install" ]; then
    su -c "cp '$BIN' '$INSTALL_PATH' && chmod 755 '$INSTALL_PATH'"
    echo "[OK] installed to $INSTALL_PATH"
    echo "     su -c '$INSTALL_PATH --diag'   then   su -c '$INSTALL_PATH'"
fi
