#include "Compatibility.hpp"

namespace compatibility {
namespace {

struct RequiredSymbol {
    const char* name;
};

constexpr RequiredSymbol RequiredSymbols[] = {
    {"GetPreloaderInput"},
    {"pl_hook"},
    {"pl_unhook"},
    {"_ZN2pl7modmenu14registerModuleERKNS0_10ModuleInfoE"},
    {"_ZN2pl7modmenu16unregisterModuleENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEE"},
    {"_ZN2pl7modmenu16setModuleEnabledENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEEb"},
    {"_ZN2pl7modmenu18submitDrawCommandsENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKNS0_11DrawCommandELm18446744073709551615EEE"},
    {"_ZN2pl7modmenu13registerImageENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKhLm18446744073709551615EEEii"},
    {"Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_nativeSetExternalModConfig"},
    {"Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_nativeToggleExternalMod"},
};

} // namespace

bool validateRuntime(void* preloader, const char*& missingSymbol) {
    missingSymbol = nullptr;
    if (!preloader) {
        missingSymbol = "libpreloader.so";
        return false;
    }
    for (usize i = 0; i < sizeof(RequiredSymbols) / sizeof(RequiredSymbols[0]); ++i) {
        if (!dlsym(preloader, RequiredSymbols[i].name)) {
            missingSymbol = RequiredSymbols[i].name;
            return false;
        }
    }
    return true;
}

} // namespace compatibility
