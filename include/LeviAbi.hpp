#pragma once
#include "Platform.hpp"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

namespace leviabi {

struct NdkString {
    alignas(8) u8 bytes[24];
};

inline void setShortString(NdkString& out, const char* value) {
    memset(&out, 0, sizeof(out));
    if (!value) return;

    const usize total = strlen(value);
    const usize limit = total < 22 ? total : 22;
    usize cursor = 0;
    usize safeLength = 0;
    while (cursor < limit) {
        const u8 lead = static_cast<u8>(value[cursor]);
        usize width = 1;
        if ((lead & 0xE0u) == 0xC0u) width = 2;
        else if ((lead & 0xF0u) == 0xE0u) width = 3;
        else if ((lead & 0xF8u) == 0xF0u) width = 4;
        if (cursor + width > limit || cursor + width > total) break;
        safeLength = cursor + width;
        cursor += width;
    }

    out.bytes[0] = static_cast<u8>(safeLength << 1);
    memcpy(out.bytes + 1, value, safeLength);
    out.bytes[safeLength + 1] = 0;
}


enum class ConfigType : int {
    Toggle = 0,
    SliderInt = 1,
    SliderFloat = 2,
    Radio = 3,
    Color = 4,
    Keybind = 5,
    Text = 6,
    Button = 7,
};

struct ConfigEntry {
    NdkString key;
    NdkString displayName;
    ConfigType type;
    u32 padding0;
    NdkString defaultValue;
    NdkString minValue;
    NdkString maxValue;
    NdkString dependsOn;
};
static_assert(sizeof(ConfigEntry) == 152,
              "Mod Menu ConfigEntry ABI mismatch");

struct TouchEvent {
    int action;
    int pointerId;
    float x;
    float y;
};

struct FunctionBlob {
    alignas(16) u8 bytes[48];
};
static_assert(sizeof(FunctionBlob) == 48, "preloader std::function ABI mismatch");

struct EmptyVector {
    void* begin;
    void* end;
    void* capacityEnd;
};

struct ModuleInfo {
    NdkString moduleId;
    NdkString displayName;
    NdkString description;
    NdkString modId;
    bool defaultEnabled;
    bool hideInHudEditor;
    u8 padding0[6];
    EmptyVector configs;
    FunctionBlob onToggle;
    FunctionBlob onConfigChanged;
    FunctionBlob onKeybind;
};
static_assert(offsetof(ModuleInfo, configs) == 104, "ModuleInfo.configs ABI mismatch");
static_assert(offsetof(ModuleInfo, onToggle) == 128, "ModuleInfo.onToggle ABI mismatch");
static_assert(offsetof(ModuleInfo, onConfigChanged) == 176,
              "ModuleInfo.onConfigChanged ABI mismatch");
static_assert(offsetof(ModuleInfo, onKeybind) == 224,
              "ModuleInfo.onKeybind ABI mismatch");
static_assert(offsetof(ModuleInfo, onKeybind) + 32 == 256,
              "Installed preloader callback-manager offset mismatch");
static_assert(sizeof(ModuleInfo) == 272, "Mod Menu ModuleInfo ABI mismatch");

inline void clearFunction(FunctionBlob& function) {
    memset(&function, 0, sizeof(function));
}

inline bool isEmptyFunction(const FunctionBlob& function) {
    const u8* bytes = function.bytes;
    for (usize i = 0; i < sizeof(function.bytes); ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

inline void initializeEmptyCallbacks(ModuleInfo& info) {
    clearFunction(info.onToggle);
    clearFunction(info.onConfigChanged);
    clearFunction(info.onKeybind);
}

inline bool callbacksAreEmpty(const ModuleInfo& info) {
    return isEmptyFunction(info.onToggle) &&
           isEmptyFunction(info.onConfigChanged) &&
           isEmptyFunction(info.onKeybind);
}

enum class DrawCommandType : int {
    Text = 0,
    Rect = 1,
    Line = 2,
    RectFilled = 3,
    CircleFilled = 4,
    TriangleFilled = 5,
    Image = 6,
};

struct DrawCommand {
    DrawCommandType type;
    float x;
    float y;
    float w;
    float h;
    float x3;
    float y3;
    u32 color;
    float size;
    NdkString text;
    NdkString fontId;
    NdkString imageId;
};
static_assert(sizeof(DrawCommand) == 112, "Mod Menu DrawCommand ABI mismatch");

using LegacyTouchCallback = bool (*)(int action, int pointerId, float x, float y);
using LegacyKeyCallback = bool (*)(int keyCode, unsigned int unicodeChar, bool isKeyDown);
using LegacyMouseCallback = bool (*)(int button, bool isDown);
using LegacyTextCallback = bool (*)(const char* text, usize size);

struct PreloaderInputInterface {
    void (*registerTouchCallback)(LegacyTouchCallback callback);
    void (*registerKeyEventCallback)(LegacyKeyCallback callback);
    void (*showKeyboard)();
    void (*hideKeyboard)();
    void (*registerMouseCallback)(LegacyMouseCallback callback);
    void (*registerTextInputCallback)(LegacyTextCallback callback);
};
static_assert(sizeof(PreloaderInputInterface) == 48,
              "Preloader input interface ABI mismatch");

using GetPreloaderInputFn = PreloaderInputInterface* (*)();
using RegisterModuleFn = bool (*)(const ModuleInfo*);
using UnregisterModuleFn = void (*)(const char*, usize);
using SetModuleEnabledFn = void (*)(const char*, usize, bool);
using SubmitDrawCommandsFn = void (*)(const char*, usize, const DrawCommand*, usize);
using RegisterImageFn = bool (*)(const char*, usize, const u8*, usize, int, int);
using PlHookFn = int (*)(void*, void*, void**, int);
using PlUnhookFn = bool (*)(void*, void*);

struct Api {
    void* preloader;
    PreloaderInputInterface* input;
    RegisterModuleFn registerModule;
    UnregisterModuleFn unregisterModule;
    SetModuleEnabledFn setModuleEnabled;
    SubmitDrawCommandsFn submitDrawCommands;
    RegisterImageFn registerImage;
    PlHookFn hook;
    PlUnhookFn unhook;
};

bool resolveApi(Api& api);

// Id the launcher assigned to this mod (ModContext::mInfo.id). Newer
// preloaders reject Mod Menu modules whose declared modId does not match the
// owning lifecycle mod, so ModuleInfo.modId must be filled with it.
void setOwnerModId(const char* id);
const char* ownerModId();
// Writes the owner id into a ModuleInfo string field. Handles ids longer than
// the 22-byte short-string limit by emitting a libc++ "long" string that points
// at static storage (the preloader copies it during registerModule).
void fillOwnerModId(NdkString& out);

} // namespace leviabi
