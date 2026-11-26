#!/usr/bin/env sh
# Compiles java/com/levimusic/picker/*.java to a dex and writes it as a C array
# (build/gen/PickerDex.inc) that FilePicker.cpp embeds into the native library.
# Needs a JDK plus an Android SDK (platform android.jar and build-tools d8).
# Without them it writes an empty array: the mod still builds and works, but the
# system file picker is reported as unavailable.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
GEN="$ROOT/build/gen"
WORK="$ROOT/build/picker"
mkdir -p "$GEN"
rm -rf "$WORK"
mkdir -p "$WORK/classes"

emit_empty() {
    printf '// generated: no dex available\nstatic const unsigned char kPickerDex[] = {0};\nstatic const unsigned kPickerDexSize = 0;\n' > "$GEN/PickerDex.inc"
    echo "WARNING: picker dex not built ($1); the system file picker will be unavailable." >&2
    if [ "${REQUIRE_PICKER:-0}" = "1" ]; then
        echo "ERROR: REQUIRE_PICKER=1 but the dex could not be built." >&2
        exit 1
    fi
    exit 0
}

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$SDK" ] && [ -d "$SDK" ] || emit_empty "no Android SDK"
command -v javac >/dev/null 2>&1 || emit_empty "no javac"
JAR="$(ls -d "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -n 1 || true)"
D8="$(ls -d "$SDK"/build-tools/*/d8 2>/dev/null | sort -V | tail -n 1 || true)"
[ -n "$JAR" ] || emit_empty "no platform android.jar"
[ -n "$D8" ] || emit_empty "no d8"

javac --release 11 -Xlint:-options -classpath "$JAR" -d "$WORK/classes" "$ROOT"/java/com/levimusic/picker/*.java || emit_empty "javac failed"
# shellcheck disable=SC2046
"$D8" --release --min-api 26 --lib "$JAR" --output "$WORK" $(find "$WORK/classes" -name '*.class') || emit_empty "d8 failed"
[ -f "$WORK/classes.dex" ] || emit_empty "d8 produced no classes.dex"

python3 - "$WORK/classes.dex" "$GEN/PickerDex.inc" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
rows = []
for i in range(0, len(data), 16):
    rows.append('    ' + ', '.join('0x%02x' % b for b in data[i:i+16]) + ',')
with open(sys.argv[2], 'w') as out:
    out.write('// generated from java/com/levimusic/picker by tools/build_picker_dex.sh\n')
    out.write('static const unsigned char kPickerDex[] = {\n' + '\n'.join(rows) + '\n};\n')
    out.write('static const unsigned kPickerDexSize = %d;\n' % len(data))
PY
echo "picker dex: $(wc -c < "$WORK/classes.dex") bytes"
