#pragma once

#include "JniBridge.hpp"
#include "LeviAbi.hpp"

class ModMenuConfigHook {
public:
    using ChangeCallback = void (*)(const char* key, const char* value);

    bool initialize(leviabi::Api* api, ChangeCallback callback);
    void shutdown();
    bool installed() const { return mInstalled; }

private:
    using SetConfigJniFn = void (*)(jni::Env* env,
                                    jni::JClass clazz,
                                    jni::JString moduleId,
                                    jni::JString key,
                                    jni::JString value);

    static void configDetour(jni::Env* env,
                             jni::JClass clazz,
                             jni::JString moduleId,
                             jni::JString key,
                             jni::JString value);
    bool resolveTarget();
    void onConfigChanged(jni::Env* env,
                         jni::JString moduleId,
                         jni::JString key,
                         jni::JString value);

    leviabi::Api* mApi{};
    ChangeCallback mCallback{};
    void* mTarget{};
    SetConfigJniFn mOriginal{};
    bool mInstalled{};
};
