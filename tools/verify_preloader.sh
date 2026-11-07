#!/usr/bin/env bash
# Verifies that a libpreloader.so exports everything LeviMusic uses and that
# the structs LeviMusic mirrors in include/LeviAbi.hpp still have the same size
# and layout. Run it whenever LeviLaunchroid updates its preloader.
#   usage: tools/verify_preloader.sh /path/to/libpreloader.so
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 /path/to/libpreloader.so" >&2; exit 2; }
PRE="$1"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

file "$PRE" | grep -q 'ARM aarch64' || { echo "FAIL: not an AArch64 ELF" >&2; exit 1; }
echo "sha256   : $(sha256sum "$PRE" | awk '{print $1}')"
echo "build-id : $(readelf -n "$PRE" | awk '/Build ID:/ {print $3; exit}')"

readelf -Ws --dyn-syms "$PRE" | awk '{print $8}' > "$TMP/syms"
for symbol in \
    GetPreloaderInput pl_hook pl_unhook \
    _ZN2pl7modmenu14registerModuleERKNS0_10ModuleInfoE \
    _ZN2pl7modmenu16unregisterModuleENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEE \
    _ZN2pl7modmenu16setModuleEnabledENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEEb \
    _ZN2pl7modmenu18submitDrawCommandsENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKNS0_11DrawCommandELm18446744073709551615EEE \
    _ZN2pl7modmenu13registerImageENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKhLm18446744073709551615EEEii \
    Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_nativeSetExternalModConfig \
    Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_nativeToggleExternalMod
do
    grep -Fxq "$symbol" "$TMP/syms" || { echo "FAIL: missing export $symbol" >&2; exit 1; }
done
echo "exports  : OK"

readelf --debug-dump=info "$PRE" > "$TMP/dwarf" 2>/dev/null
# Full DWARF definition with matching name+size (the preloader also contains
# unrelated same-named internal types, so the first match is not enough).
has_type_size() {
    awk -v type="$1" -v size="$2" '
      /DW_TAG_(structure_type|class_type)/ { active=1; name=""; bytes=""; lines=0 }
      active { lines++
        if ($0 ~ /DW_AT_name/ && $0 ~ (": " type "$")) name=type
        if ($0 ~ /DW_AT_byte_size/) { bytes=$0; sub(/^.*DW_AT_byte_size[[:space:]]*:[[:space:]]*/,"",bytes); sub(/[[:space:]].*$/,"",bytes) }
        if (name==type && bytes==size) { found=1; exit }
        if (lines>120 || /Abbrev Number: 0/) active=0 }
      END { exit(found?0:1) }' "$TMP/dwarf"
}
for t in "ModuleInfo 272" "ConfigEntry 152" "DrawCommand 112" "PreloaderInput_Interface 48"; do
    set -- $t
    has_type_size "$1" "$2" || { echo "FAIL: $1 is no longer $2 bytes" >&2; exit 1; }
    echo "layout   : $1 = $2 bytes OK"
done
echo "PASS: preloader is compatible with LeviMusic's ABI mirror."
