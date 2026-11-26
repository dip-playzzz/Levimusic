#include "FilePicker.hpp"
#include "JniBridge.hpp"
#include "PickerDex.inc"   // generated: kPickerDex / kPickerDexSize

static constexpr const char* LogTag = "LeviMusic";

namespace filepicker {
namespace {

jni::JClass gBridge{};
jni::JMethodId gOpen{};
jni::JMethodId gState{};
jni::JMethodId gDone{};
jni::JMethodId gTotal{};
jni::JMethodId gFailed{};
jni::JMethodId gTakeFinished{};
bool gReady{};

bool step(jni::Env* env, const char* what, const void* value) {
    if (value && !jni::checkAndClearException(env, what)) return true;
    levi_log(LOG_ERROR_VALUE, LogTag, "file picker: %s failed", what);
    jni::checkAndClearException(env, what);
    return false;
}

} // namespace

bool isReady() { return gReady; }

bool init(const char* importDirectory) {
    if (gReady) return true;
    if (kPickerDexSize < 64) {
        levi_log(LOG_WARN_VALUE, LogTag,
                 "file picker: this build has no embedded dex (Java compile step skipped)");
        return false;
    }
    jni::Vm* vm = jni::javaVm();
    if (!vm) return false;
    jni::EnvScope scope(vm);
    jni::Env* env = scope.get();
    if (!env) return false;

    jni::JClass activityThread = jni::findClass(env, "android/app/ActivityThread");
    if (!step(env, "find ActivityThread", activityThread)) return false;
    jni::JMethodId currentApplication = jni::getStaticMethodId(
        env, activityThread, "currentApplication", "()Landroid/app/Application;");
    if (!step(env, "find currentApplication", currentApplication)) return false;
    jni::JObject application =
        jni::callStaticObjectMethodA(env, activityThread, currentApplication, nullptr);
    if (!step(env, "currentApplication", application)) return false;

    jni::JClass applicationClass = jni::getObjectClass(env, application);
    jni::JMethodId getClassLoader = jni::getMethodId(
        env, applicationClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!step(env, "find getClassLoader", getClassLoader)) return false;
    jni::JObject parent = jni::callObjectMethodA(env, application, getClassLoader, nullptr);
    if (!step(env, "getClassLoader", parent)) return false;

    // dex bytes -> ByteBuffer -> InMemoryDexClassLoader
    jni::JByteArray bytes = jni::newByteArray(env, static_cast<jni::JSize>(kPickerDexSize));
    if (!step(env, "allocate dex array", bytes)) return false;
    jni::setByteArrayRegion(env, bytes, 0, static_cast<jni::JSize>(kPickerDexSize),
                            reinterpret_cast<const jni::JByte*>(kPickerDex));
    jni::JClass bufferClass = jni::findClass(env, "java/nio/ByteBuffer");
    if (!step(env, "find ByteBuffer", bufferClass)) return false;
    jni::JMethodId wrap = jni::getStaticMethodId(env, bufferClass, "wrap",
                                                 "([B)Ljava/nio/ByteBuffer;");
    if (!step(env, "find ByteBuffer.wrap", wrap)) return false;
    jni::JValue wrapArg{};
    wrapArg.l = bytes;
    jni::JObject buffer = jni::callStaticObjectMethodA(env, bufferClass, wrap, &wrapArg);
    if (!step(env, "ByteBuffer.wrap", buffer)) return false;

    jni::JClass loaderClass = jni::findClass(env, "dalvik/system/InMemoryDexClassLoader");
    if (!step(env, "find InMemoryDexClassLoader", loaderClass)) return false;
    jni::JMethodId loaderCtor = jni::getMethodId(
        env, loaderClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (!step(env, "find InMemoryDexClassLoader.<init>", loaderCtor)) return false;
    jni::JValue loaderArgs[2]{};
    loaderArgs[0].l = buffer;
    loaderArgs[1].l = parent;
    jni::JObject loader = jni::newObjectA(env, loaderClass, loaderCtor, loaderArgs);
    if (!step(env, "create InMemoryDexClassLoader", loader)) return false;

    jni::JClass classLoaderClass = jni::findClass(env, "java/lang/ClassLoader");
    jni::JMethodId loadClass = jni::getMethodId(
        env, classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!step(env, "find ClassLoader.loadClass", loadClass)) return false;
    jni::JString className = jni::newStringUtf(env, "com.levimusic.picker.Bridge");
    jni::JValue nameArg{};
    nameArg.l = className;
    jni::JObject bridge = jni::callObjectMethodA(env, loader, loadClass, &nameArg);
    if (!step(env, "load picker class", bridge)) return false;
    jni::JClass bridgeGlobal = static_cast<jni::JClass>(jni::newGlobalRef(env, bridge));
    if (!step(env, "pin picker class", bridgeGlobal)) return false;

    jni::JMethodId install = jni::getStaticMethodId(
        env, bridgeGlobal, "install", "(Landroid/app/Application;Ljava/lang/String;)V");
    jni::JMethodId open = jni::getStaticMethodId(env, bridgeGlobal, "open", "()V");
    jni::JMethodId state = jni::getStaticMethodId(env, bridgeGlobal, "state", "()I");
    jni::JMethodId done = jni::getStaticMethodId(env, bridgeGlobal, "done", "()I");
    jni::JMethodId total = jni::getStaticMethodId(env, bridgeGlobal, "total", "()I");
    jni::JMethodId failed = jni::getStaticMethodId(env, bridgeGlobal, "failed", "()I");
    jni::JMethodId takeFinishedId = jni::getStaticMethodId(env, bridgeGlobal, "takeFinished", "()Z");
    if (!step(env, "find picker methods",
              (install && open && state && done && total && failed && takeFinishedId)
                  ? static_cast<const void*>(install) : nullptr))
        return false;

    jni::JString dest = jni::newStringUtf(env, importDirectory ? importDirectory : "");
    jni::JValue installArgs[2]{};
    installArgs[0].l = application;
    installArgs[1].l = dest;
    jni::callStaticVoidMethodA(env, bridgeGlobal, install, installArgs);
    if (jni::checkAndClearException(env, "install picker")) return false;

    gBridge = bridgeGlobal;
    gOpen = open;
    gState = state;
    gDone = done;
    gTotal = total;
    gFailed = failed;
    gTakeFinished = takeFinishedId;
    gReady = true;
    levi_log(LOG_INFO_VALUE, LogTag, "file picker ready (imports go to %s)",
             importDirectory ? importDirectory : "?");
    return true;
}

bool open() {
    if (!gReady) return false;
    jni::EnvScope scope(jni::javaVm());
    jni::Env* env = scope.get();
    if (!env) return false;
    jni::callStaticVoidMethodA(env, gBridge, gOpen, nullptr);
    return !jni::checkAndClearException(env, "open picker");
}

Status poll() {
    Status status{};
    if (!gReady) {
        status.state = StateUnavailable;
        return status;
    }
    jni::EnvScope scope(jni::javaVm());
    jni::Env* env = scope.get();
    if (!env) return status;
    status.state = jni::callStaticIntMethodA(env, gBridge, gState, nullptr);
    status.done = jni::callStaticIntMethodA(env, gBridge, gDone, nullptr);
    status.total = jni::callStaticIntMethodA(env, gBridge, gTotal, nullptr);
    status.failed = jni::callStaticIntMethodA(env, gBridge, gFailed, nullptr);
    jni::checkAndClearException(env, "poll picker");
    return status;
}

bool takeFinished() {
    if (!gReady) return false;
    jni::EnvScope scope(jni::javaVm());
    jni::Env* env = scope.get();
    if (!env) return false;
    const bool finished =
        jni::callStaticBooleanMethodA(env, gBridge, gTakeFinished, nullptr) != 0;
    jni::checkAndClearException(env, "takeFinished");
    return finished;
}

} // namespace filepicker
