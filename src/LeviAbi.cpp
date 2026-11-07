#include "LeviAbi.hpp"

namespace leviabi {

bool resolveApi(Api& api) {
    memset(&api, 0, sizeof(api));
    api.preloader = dlopen("libpreloader.so", RTLD_NOW_VALUE | RTLD_NOLOAD_VALUE);
    if (!api.preloader) api.preloader = dlopen("libpreloader.so", RTLD_NOW_VALUE);
    if (!api.preloader) return false;

    auto getInput = reinterpret_cast<GetPreloaderInputFn>(
        dlsym(api.preloader, "GetPreloaderInput"));
    api.input = getInput ? getInput() : nullptr;

    api.registerModule = reinterpret_cast<RegisterModuleFn>(dlsym(api.preloader,
        "_ZN2pl7modmenu14registerModuleERKNS0_10ModuleInfoE"));
    api.unregisterModule = reinterpret_cast<UnregisterModuleFn>(dlsym(api.preloader,
        "_ZN2pl7modmenu16unregisterModuleENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEE"));
    api.setModuleEnabled = reinterpret_cast<SetModuleEnabledFn>(dlsym(api.preloader,
        "_ZN2pl7modmenu16setModuleEnabledENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEEb"));
    api.submitDrawCommands = reinterpret_cast<SubmitDrawCommandsFn>(dlsym(api.preloader,
        "_ZN2pl7modmenu18submitDrawCommandsENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKNS0_11DrawCommandELm18446744073709551615EEE"));
    api.registerImage = reinterpret_cast<RegisterImageFn>(dlsym(api.preloader,
        "_ZN2pl7modmenu13registerImageENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEENS1_4spanIKhLm18446744073709551615EEEii"));

    api.hook = reinterpret_cast<PlHookFn>(dlsym(api.preloader, "pl_hook"));
    api.unhook = reinterpret_cast<PlUnhookFn>(dlsym(api.preloader, "pl_unhook"));
    return api.input && api.input->registerTouchCallback &&
           api.registerModule && api.unregisterModule &&
           api.setModuleEnabled && api.submitDrawCommands &&
           api.registerImage && api.hook && api.unhook;
}

} // namespace leviabi
