#include "LeviAbi.hpp"

namespace leviabi {

static char gOwnerModId[48];

void setOwnerModId(const char* id) { copyString(gOwnerModId, sizeof(gOwnerModId), id ? id : ""); }
const char* ownerModId() { return gOwnerModId; }

static char gOwnerModIdLong[64];

void fillOwnerModId(NdkString& out) {
    const char* id = gOwnerModId;
    const usize length = strlen(id);
    if (length <= 22) {
        setShortString(out, id);
        return;
    }
    // libc++ long form (little endian): word0 = (capacity << 1) | 1,
    // word1 = size, word2 = data pointer.
    copyString(gOwnerModIdLong, sizeof(gOwnerModIdLong), id);
    const u64 capacityWord = (static_cast<u64>(sizeof(gOwnerModIdLong) - 1) << 1) | 1u;
    const u64 sizeWord = static_cast<u64>(strlen(gOwnerModIdLong));
    const void* data = gOwnerModIdLong;
    memset(&out, 0, sizeof(out));
    memcpy(out.bytes, &capacityWord, sizeof(capacityWord));
    memcpy(out.bytes + 8, &sizeWord, sizeof(sizeWord));
    memcpy(out.bytes + 16, &data, sizeof(data));
}

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
