#include "ModMenuConfigHook.hpp"

static ModMenuConfigHook* gModMenuConfigHook = nullptr;
static constexpr const char* LogTag = "LeviMusic";
static constexpr const char* ModuleId = "levi_music";
static constexpr const char* ConfigJniSymbol =
    "Java_org_levimc_launcher_core_mods_inbuilt_ExternalModBridge_"
    "nativeSetExternalModConfig";

bool ModMenuConfigHook::resolveTarget() {
    if (!mApi || !mApi->preloader) return false;
    mTarget = dlsym(mApi->preloader, ConfigJniSymbol);
    if (!mTarget) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "stable Mod Menu config JNI export unavailable");
        return false;
    }
    return true;
}

bool ModMenuConfigHook::initialize(leviabi::Api* api,
                                   ChangeCallback callback) {
    mApi = api;
    mCallback = callback;
    if (!mApi || !mApi->hook || !mApi->unhook || !mCallback ||
        !resolveTarget()) {
        return false;
    }

    gModMenuConfigHook = this;
    void* original = nullptr;
    const int result = mApi->hook(
        mTarget, reinterpret_cast<void*>(&ModMenuConfigHook::configDetour),
        &original, 110);
    if (result != 0 || !original) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "Mod Menu config JNI hook failed: %d", result);
        gModMenuConfigHook = nullptr;
        mTarget = nullptr;
        return false;
    }

    mOriginal = reinterpret_cast<SetConfigJniFn>(original);
    mInstalled = true;
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "Mod Menu config uses stable JNI bridge");
    return true;
}

void ModMenuConfigHook::configDetour(jni::Env* env,
                                     jni::JClass clazz,
                                     jni::JString moduleId,
                                     jni::JString key,
                                     jni::JString value) {
    ModMenuConfigHook* self = gModMenuConfigHook;
    if (self && self->mOriginal) {
        self->mOriginal(env, clazz, moduleId, key, value);
    }
    if (self) self->onConfigChanged(env, moduleId, key, value);
}

void ModMenuConfigHook::onConfigChanged(jni::Env* env,
                                        jni::JString moduleId,
                                        jni::JString key,
                                        jni::JString value) {
    if (!env || !moduleId || !key || !mCallback) return;

    const char* idChars = jni::getStringUtfChars(env, moduleId);
    if (!idChars) return;
    const bool ours = strcmp(idChars, ModuleId) == 0;
    jni::releaseStringUtfChars(env, moduleId, idChars);
    if (!ours) return;

    const char* keyChars = jni::getStringUtfChars(env, key);
    if (!keyChars) return;
    const char* valueChars = value ? jni::getStringUtfChars(env, value) : nullptr;

    mCallback(keyChars, valueChars ? valueChars : "");

    if (valueChars) jni::releaseStringUtfChars(env, value, valueChars);
    jni::releaseStringUtfChars(env, key, keyChars);
}

void ModMenuConfigHook::shutdown() {
    if (mInstalled && mApi && mApi->unhook && mTarget) {
        mApi->unhook(mTarget,
                     reinterpret_cast<void*>(&ModMenuConfigHook::configDetour));
    }
    mInstalled = false;
    mOriginal = nullptr;
    mTarget = nullptr;
    mCallback = nullptr;
    mApi = nullptr;
    if (gModMenuConfigHook == this) gModMenuConfigHook = nullptr;
}
