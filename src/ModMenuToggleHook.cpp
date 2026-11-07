#include "ModMenuToggleHook.hpp"
#include "NativeMusicUi.hpp"

static ModMenuToggleHook* gModMenuToggleHook = nullptr;
static constexpr const char* LogTag = "LeviMusic";
static constexpr const char* ModuleId = "levi_music";
static constexpr const char* ToggleJniSymbol =
    "Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_"
    "nativeToggleExternalMod";

bool ModMenuToggleHook::resolveTarget() {
    if (!mApi || !mApi->preloader) return false;
    mTarget = dlsym(mApi->preloader, ToggleJniSymbol);
    if (!mTarget) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "stable Mod Menu toggle JNI export unavailable");
        return false;
    }
    return true;
}

bool ModMenuToggleHook::initialize(leviabi::Api* api, NativeMusicUi* ui) {
    mApi = api;
    mUi = ui;
    if (!mApi || !mApi->hook || !mApi->unhook || !mUi ||
        !resolveTarget()) {
        return false;
    }

    gModMenuToggleHook = this;
    void* original = nullptr;
    const int result = mApi->hook(
        mTarget, reinterpret_cast<void*>(&ModMenuToggleHook::toggleDetour),
        &original, 100);
    if (result != 0 || !original) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "Mod Menu toggle JNI hook failed: %d", result);
        gModMenuToggleHook = nullptr;
        mTarget = nullptr;
        return false;
    }

    mOriginal = reinterpret_cast<ToggleJniFn>(original);
    mInstalled = true;
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "Mod Menu toggle uses stable JNI bridge");
    return true;
}

void ModMenuToggleHook::toggleDetour(jni::Env* env,
                                     jni::JClass clazz,
                                     jni::JString moduleId,
                                     jni::JBoolean enabled) {
    ModMenuToggleHook* self = gModMenuToggleHook;
    if (self && self->mOriginal) {
        self->mOriginal(env, clazz, moduleId, enabled);
    }
    if (self) self->onToggle(env, moduleId, enabled);
}

void ModMenuToggleHook::onToggle(jni::Env* env,
                                 jni::JString moduleId,
                                 jni::JBoolean enabled) {
    if (!env || !moduleId || !mUi) return;
    const char* idChars = jni::getStringUtfChars(env, moduleId);
    if (!idChars) return;
    const bool ours = strcmp(idChars, ModuleId) == 0;
    jni::releaseStringUtfChars(env, moduleId, idChars);
    if (ours) mUi->handleModuleToggle(enabled != 0);
}

void ModMenuToggleHook::shutdown() {
    if (mInstalled && mApi && mApi->unhook && mTarget) {
        mApi->unhook(mTarget,
                     reinterpret_cast<void*>(&ModMenuToggleHook::toggleDetour));
    }
    mInstalled = false;
    mOriginal = nullptr;
    mTarget = nullptr;
    mUi = nullptr;
    mApi = nullptr;
    if (gModMenuToggleHook == this) gModMenuToggleHook = nullptr;
}
