#pragma once

#include "JniBridge.hpp"
#include "LeviAbi.hpp"

class NativeMusicUi;

class ModMenuToggleHook {
public:
    bool initialize(leviabi::Api* api, NativeMusicUi* ui);
    void shutdown();
    bool installed() const { return mInstalled; }

private:
    using ToggleJniFn = void (*)(jni::Env* env,
                                 jni::JClass clazz,
                                 jni::JString moduleId,
                                 jni::JBoolean enabled);

    static void toggleDetour(jni::Env* env,
                             jni::JClass clazz,
                             jni::JString moduleId,
                             jni::JBoolean enabled);
    bool resolveTarget();
    void onToggle(jni::Env* env,
                  jni::JString moduleId,
                  jni::JBoolean enabled);

    leviabi::Api* mApi{};
    NativeMusicUi* mUi{};
    void* mTarget{};
    ToggleJniFn mOriginal{};
    bool mInstalled{};
};
