#include "LeviMusicMod.hpp"

struct ModRegistration {
    void* instance;
    bool (*load)(void*, void*);
    bool (*enable)(void*, void*);
    bool (*disable)(void*, void*);
    bool (*unload)(void*, void*);
};

static bool loadBridge(void* instance, void* context) {
    return static_cast<LeviMusicMod*>(instance)->load(context);
}
static bool enableBridge(void* instance, void* context) {
    return static_cast<LeviMusicMod*>(instance)->enable(context);
}
static bool disableBridge(void* instance, void* context) {
    return static_cast<LeviMusicMod*>(instance)->disable(context);
}
static bool unloadBridge(void* instance, void* context) {
    return static_cast<LeviMusicMod*>(instance)->unload(context);
}

static ModRegistration gRegistration{};

extern "C" __attribute__((visibility("default")))
ModRegistration* PLGetModRegistration() {
    gRegistration.instance = &leviMusicMod();
    gRegistration.load = &loadBridge;
    gRegistration.enable = &enableBridge;
    gRegistration.disable = &disableBridge;
    gRegistration.unload = &unloadBridge;
    return &gRegistration;
}
