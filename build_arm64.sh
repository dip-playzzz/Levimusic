#!/usr/bin/env sh
# Builds liblevi_music.so with the real Android NDK (r28c / 28.2.13676358).
# The mod is freestanding: it does not link libc++, libpreloader.so or
# libminecraftpe.so. It only needs libc, libdl and liblog from the device.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$ROOT/build"
OBJ="$BUILD/obj"
LIB=liblevi_music.so
TARGET=aarch64-linux-android28

NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"
BIN="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [ -z "$NDK_ROOT" ] || [ ! -d "$BIN" ]; then
    echo "Set ANDROID_NDK_ROOT to an Android NDK r28 directory." >&2
    exit 2
fi
CXX="$BIN/clang++"
OBJCOPY="$BIN/llvm-objcopy"
READELF="$BIN/llvm-readelf"

mkdir -p "$OBJ"
rm -f "$OBJ"/*.o

COMMON="--target=$TARGET -std=c++20 -fPIC -fvisibility=hidden -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin -mno-outline-atomics -nostdinc -I$ROOT/include"
SOURCES="main FileLog Http Online LeviMusicMod Compatibility LeviAbi JniBridge Config MusicLibrary AndroidMediaPlayer MediaMetadata NativeMusicUi ModMenuToggleHook ModMenuConfigHook VanillaUiAssets Cache"

for name in $SOURCES; do
    # shellcheck disable=SC2086
    "$CXX" $COMMON -c "$ROOT/src/$name.cpp" -o "$OBJ/$name.o"
done

"$CXX" --target=$TARGET -shared -nostdlib -fuse-ld=lld \
    -Wl,-soname,$LIB -Wl,--build-id=sha1 -Wl,-z,max-page-size=16384 -Wl,-z,defs \
    "$OBJ"/*.o -lc -ldl -llog -o "$BUILD/$LIB.unstripped"

cp "$BUILD/$LIB.unstripped" "$BUILD/$LIB"
"$OBJCOPY" --strip-unneeded "$BUILD/$LIB"

# --- regression checks -------------------------------------------------------
fail() { echo "BUILD CHECK FAILED: $1" >&2; exit 1; }
NEEDED="$("$READELF" -dW "$BUILD/$LIB")"
echo "$NEEDED" | grep -q 'Shared library: \[libc.so\]'   || fail "libc.so not linked"
echo "$NEEDED" | grep -q 'Shared library: \[libdl.so\]'  || fail "libdl.so not linked"
echo "$NEEDED" | grep -q 'Shared library: \[liblog.so\]' || fail "liblog.so not linked"
echo "$NEEDED" | grep -q 'libpreloader.so'    && fail "must not link libpreloader.so (resolved with dlopen)"
echo "$NEEDED" | grep -q 'libminecraftpe.so'  && fail "must not link libminecraftpe.so"
S="$BUILD/strings.txt"; strings "$BUILD/$LIB" > "$S"
for need in youtubei/v1/search youtubei/v1/player java/net/HttpURLConnection nativeSetExternalModConfig nativeToggleExternalMod 'android/media/audiofx/Visualizer' \
            getEmbeddedPicture ui_panel_position_saved 'Search songs, artists' 'runtime preloader ABI validation passed'; do
    grep -Fq "$need" "$S" || fail "missing expected string: $need"
done
for bad in FMOD_System_Init bringToFront requestLayout ConfigSignature ToggleSignature; do
    grep -Fq "$bad" "$S" && fail "unexpected string: $bad"
done
echo "$BUILD/$LIB"
