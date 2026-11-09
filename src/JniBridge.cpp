#include "JniBridge.hpp"

namespace jni {
namespace {

Vm* gJavaVm = nullptr;
static constexpr const char* LogTag = "LeviMusic";

constexpr usize FindClassIndex = 6;
constexpr usize ExceptionClearIndex = 17;
constexpr usize NewGlobalRefIndex = 21;
constexpr usize DeleteGlobalRefIndex = 22;
constexpr usize DeleteLocalRefIndex = 23;
constexpr usize NewObjectAIndex = 30;
constexpr usize GetObjectClassIndex = 31;
constexpr usize GetMethodIdIndex = 33;
constexpr usize CallObjectMethodAIndex = 36;
constexpr usize CallBooleanMethodAIndex = 39;
constexpr usize CallIntMethodAIndex = 51;
constexpr usize CallVoidMethodAIndex = 63;
constexpr usize GetFieldIdIndex = 94;
constexpr usize GetObjectFieldIndex = 95;
constexpr usize GetIntFieldIndex = 100;
constexpr usize GetStaticMethodIdIndex = 113;
constexpr usize CallStaticObjectMethodAIndex = 116;
constexpr usize CallStaticBooleanMethodAIndex = 119;
constexpr usize NewStringUtfIndex = 167;
constexpr usize GetStringUtfCharsIndex = 169;
constexpr usize ReleaseStringUtfCharsIndex = 170;
constexpr usize GetArrayLengthIndex = 171;
constexpr usize NewByteArrayIndex = 176;
constexpr usize NewIntArrayIndex = 179;
constexpr usize GetByteArrayRegionIndex = 200;
constexpr usize GetIntArrayRegionIndex = 203;
constexpr usize ExceptionCheckIndex = 228;

constexpr usize AttachCurrentThreadIndex = 4;
constexpr usize DetachCurrentThreadIndex = 5;
constexpr usize GetEnvIndex = 6;

template <typename Function>
Function envFunction(Env* env, usize index) {
    if (!env || !env->functions) return nullptr;
    return reinterpret_cast<Function>(const_cast<void*>(env->functions[index]));
}

template <typename Function>
Function vmFunction(Vm* vm, usize index) {
    if (!vm || !vm->functions) return nullptr;
    return reinterpret_cast<Function>(const_cast<void*>(vm->functions[index]));
}

} // namespace

bool captureFromModContext(void* modContext) {
    if (!modContext) return false;
    Vm* candidate = nullptr;
    memcpy(&candidate, modContext, sizeof(candidate));
    if (!candidate || !candidate->functions || !candidate->functions[GetEnvIndex]) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "invalid JavaVM in Levi ModContext");
        return false;
    }
    gJavaVm = candidate;
    return true;
}

Vm* javaVm() { return gJavaVm; }

EnvScope::EnvScope(Vm* vm) : mVm(vm) {
    using GetEnvFn = JInt (*)(Vm*, void**, JInt);
    using AttachFn = JInt (*)(Vm*, void**, void*);
    auto getEnv = vmFunction<GetEnvFn>(mVm, GetEnvIndex);
    if (!getEnv) return;
    void* rawEnv = nullptr;
    const JInt result = getEnv(mVm, &rawEnv, Version16);
    if (result == Ok && rawEnv) {
        mEnv = reinterpret_cast<Env*>(rawEnv);
        return;
    }
    if (result != Detached) return;
    auto attach = vmFunction<AttachFn>(mVm, AttachCurrentThreadIndex);
    if (!attach) return;
    if (attach(mVm, &rawEnv, nullptr) == Ok && rawEnv) {
        mEnv = reinterpret_cast<Env*>(rawEnv);
        mAttached = true;
    }
}

EnvScope::~EnvScope() {
    if (!mAttached || !mVm) return;
    using DetachFn = JInt (*)(Vm*);
    auto detach = vmFunction<DetachFn>(mVm, DetachCurrentThreadIndex);
    if (detach) detach(mVm);
}

JClass findClass(Env* env, const char* name) {
    using Fn = JClass (*)(Env*, const char*);
    auto function = envFunction<Fn>(env, FindClassIndex);
    return function ? function(env, name) : nullptr;
}

JClass getObjectClass(Env* env, JObject object) {
    using Fn = JClass (*)(Env*, JObject);
    auto function = envFunction<Fn>(env, GetObjectClassIndex);
    return function && object ? function(env, object) : nullptr;
}

JMethodId getMethodId(Env* env, JClass clazz, const char* name,
                      const char* signature) {
    using Fn = JMethodId (*)(Env*, JClass, const char*, const char*);
    auto function = envFunction<Fn>(env, GetMethodIdIndex);
    return function ? function(env, clazz, name, signature) : nullptr;
}

JMethodId getStaticMethodId(Env* env, JClass clazz, const char* name,
                            const char* signature) {
    using Fn = JMethodId (*)(Env*, JClass, const char*, const char*);
    auto function = envFunction<Fn>(env, GetStaticMethodIdIndex);
    return function ? function(env, clazz, name, signature) : nullptr;
}

JFieldId getFieldId(Env* env, JClass clazz, const char* name,
                    const char* signature) {
    using Fn = JFieldId (*)(Env*, JClass, const char*, const char*);
    auto function = envFunction<Fn>(env, GetFieldIdIndex);
    return function ? function(env, clazz, name, signature) : nullptr;
}

JObject newObjectA(Env* env, JClass clazz, JMethodId method,
                   const JValue* arguments) {
    using Fn = JObject (*)(Env*, JClass, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, NewObjectAIndex);
    return function ? function(env, clazz, method, arguments) : nullptr;
}

JObject callObjectMethodA(Env* env, JObject object, JMethodId method,
                          const JValue* arguments) {
    using Fn = JObject (*)(Env*, JObject, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallObjectMethodAIndex);
    return function && object && method ? function(env, object, method, arguments)
                                        : nullptr;
}

JObject callStaticObjectMethodA(Env* env, JClass clazz, JMethodId method,
                                const JValue* arguments) {
    using Fn = JObject (*)(Env*, JClass, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallStaticObjectMethodAIndex);
    return function && clazz && method ? function(env, clazz, method, arguments)
                                       : nullptr;
}

JBoolean callStaticBooleanMethodA(Env* env, JClass clazz, JMethodId method,
                                  const JValue* arguments) {
    using Fn = JBoolean (*)(Env*, JClass, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallStaticBooleanMethodAIndex);
    return function && clazz && method ? function(env, clazz, method, arguments)
                                       : static_cast<JBoolean>(0);
}

JObject getObjectField(Env* env, JObject object, JFieldId field) {
    using Fn = JObject (*)(Env*, JObject, JFieldId);
    auto function = envFunction<Fn>(env, GetObjectFieldIndex);
    return function && object && field ? function(env, object, field) : nullptr;
}

JString newStringUtf(Env* env, const char* text) {
    using Fn = JString (*)(Env*, const char*);
    auto function = envFunction<Fn>(env, NewStringUtfIndex);
    return function ? function(env, text) : nullptr;
}

const char* getStringUtfChars(Env* env, JString text, JBoolean* isCopy) {
    using Fn = const char* (*)(Env*, JString, JBoolean*);
    auto function = envFunction<Fn>(env, GetStringUtfCharsIndex);
    return function && text ? function(env, text, isCopy) : nullptr;
}

void releaseStringUtfChars(Env* env, JString text, const char* chars) {
    using Fn = void (*)(Env*, JString, const char*);
    auto function = envFunction<Fn>(env, ReleaseStringUtfCharsIndex);
    if (function && text && chars) function(env, text, chars);
}

JObject newGlobalRef(Env* env, JObject object) {
    using Fn = JObject (*)(Env*, JObject);
    auto function = envFunction<Fn>(env, NewGlobalRefIndex);
    return function ? function(env, object) : nullptr;
}

void deleteGlobalRef(Env* env, JObject object) {
    using Fn = void (*)(Env*, JObject);
    auto function = envFunction<Fn>(env, DeleteGlobalRefIndex);
    if (function && object) function(env, object);
}

void deleteLocalRef(Env* env, JObject object) {
    using Fn = void (*)(Env*, JObject);
    auto function = envFunction<Fn>(env, DeleteLocalRefIndex);
    if (function && object) function(env, object);
}

void callVoidMethodA(Env* env, JObject object, JMethodId method,
                     const JValue* arguments) {
    using Fn = void (*)(Env*, JObject, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallVoidMethodAIndex);
    if (function && object && method) function(env, object, method, arguments);
}

JBoolean callBooleanMethodA(Env* env, JObject object, JMethodId method,
                            const JValue* arguments) {
    using Fn = JBoolean (*)(Env*, JObject, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallBooleanMethodAIndex);
    return function && object && method ? function(env, object, method, arguments)
                                        : static_cast<JBoolean>(0);
}

JInt callIntMethodA(Env* env, JObject object, JMethodId method,
                    const JValue* arguments) {
    using Fn = JInt (*)(Env*, JObject, JMethodId, const JValue*);
    auto function = envFunction<Fn>(env, CallIntMethodAIndex);
    return function && object && method ? function(env, object, method, arguments)
                                        : 0;
}

JInt getIntField(Env* env, JObject object, JFieldId field) {
    using Fn = JInt (*)(Env*, JObject, JFieldId);
    auto function = envFunction<Fn>(env, GetIntFieldIndex);
    return function && object && field ? function(env, object, field) : 0;
}

JSize getArrayLength(Env* env, JArray array) {
    using Fn = JSize (*)(Env*, JArray);
    auto function = envFunction<Fn>(env, GetArrayLengthIndex);
    return function && array ? function(env, array) : 0;
}

JByteArray newByteArray(Env* env, JSize length) {
    using Fn = JByteArray (*)(Env*, JSize);
    auto function = envFunction<Fn>(env, NewByteArrayIndex);
    return function && length > 0 ? function(env, length) : nullptr;
}

JIntArray newIntArray(Env* env, JSize length) {
    using Fn = JIntArray (*)(Env*, JSize);
    auto function = envFunction<Fn>(env, NewIntArrayIndex);
    return function && length > 0 ? function(env, length) : nullptr;
}

void getByteArrayRegion(Env* env, JByteArray array, JSize start,
                        JSize length, JByte* out) {
    using Fn = void (*)(Env*, JByteArray, JSize, JSize, JByte*);
    auto function = envFunction<Fn>(env, GetByteArrayRegionIndex);
    if (function && array && out && length > 0)
        function(env, array, start, length, out);
}

void getIntArrayRegion(Env* env, JIntArray array, JSize start,
                       JSize length, JInt* out) {
    using Fn = void (*)(Env*, JIntArray, JSize, JSize, JInt*);
    auto function = envFunction<Fn>(env, GetIntArrayRegionIndex);
    if (function && array && out && length > 0)
        function(env, array, start, length, out);
}

bool checkAndClearException(Env* env, const char* operation) {
    using CheckFn = JBoolean (*)(Env*);
    using ClearFn = void (*)(Env*);
    auto check = envFunction<CheckFn>(env, ExceptionCheckIndex);
    if (!check || !check(env)) return false;
    auto clear = envFunction<ClearFn>(env, ExceptionClearIndex);
    if (clear) clear(env);
    levi_log(LOG_ERROR_VALUE, LogTag,
                        "JNI failure during %s; exception cleared",
                        operation ? operation : "unknown operation");
    return true;
}

static bool clearExceptionSilent(Env* env) {
    using CheckFn = JBoolean (*)(Env*);
    using ClearFn = void (*)(Env*);
    auto check = envFunction<CheckFn>(env, ExceptionCheckIndex);
    if (!check || !check(env)) return false;
    auto clear = envFunction<ClearFn>(env, ExceptionClearIndex);
    if (clear) clear(env);
    return true;
}

bool queryDisplaySize(int& widthPixels, int& heightPixels) {
    widthPixels = 0;
    heightPixels = 0;
    if (!gJavaVm) return false;
    EnvScope scope(gJavaVm);
    Env* env = scope.get();
    if (!env) return false;

    JClass resourcesClass = findClass(env, "android/content/res/Resources");
    if (!resourcesClass || checkAndClearException(env, "FindClass(Resources)")) {
        if (resourcesClass) deleteLocalRef(env, resourcesClass);
        return false;
    }
    JMethodId getSystem = getStaticMethodId(
        env, resourcesClass, "getSystem", "()Landroid/content/res/Resources;");
    JMethodId getMetrics = getMethodId(
        env, resourcesClass, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (!getSystem || !getMetrics ||
        checkAndClearException(env, "resolve display metrics methods")) {
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    JObject resources = callStaticObjectMethodA(env, resourcesClass, getSystem, nullptr);
    if (!resources || checkAndClearException(env, "Resources.getSystem")) {
        if (resources) deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }
    JObject metrics = callObjectMethodA(env, resources, getMetrics, nullptr);
    if (!metrics || checkAndClearException(env, "Resources.getDisplayMetrics")) {
        if (metrics) deleteLocalRef(env, metrics);
        deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }
    JClass metricsClass = getObjectClass(env, metrics);
    if (!metricsClass || checkAndClearException(env, "GetObjectClass(DisplayMetrics)")) {
        if (metricsClass) deleteLocalRef(env, metricsClass);
        deleteLocalRef(env, metrics);
        deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    JFieldId widthField = getFieldId(env, metricsClass, "widthPixels", "I");
    JFieldId heightField = getFieldId(env, metricsClass, "heightPixels", "I");
    if (!widthField || !heightField ||
        checkAndClearException(env, "resolve DisplayMetrics fields")) {
        deleteLocalRef(env, metricsClass);
        deleteLocalRef(env, metrics);
        deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    widthPixels = getIntField(env, metrics, widthField);
    heightPixels = getIntField(env, metrics, heightField);
    const bool failed = checkAndClearException(env, "read DisplayMetrics fields");
    deleteLocalRef(env, metricsClass);
    deleteLocalRef(env, metrics);
    deleteLocalRef(env, resources);
    deleteLocalRef(env, resourcesClass);

    if (failed || widthPixels < 320 || heightPixels < 240) {
        widthPixels = 0;
        heightPixels = 0;
        return false;
    }
    return true;
}


bool querySystemDarkMode(bool& darkMode) {
    darkMode = true;
    if (!gJavaVm) return false;
    EnvScope scope(gJavaVm);
    Env* env = scope.get();
    if (!env) return false;

    JClass resourcesClass = findClass(env, "android/content/res/Resources");
    if (!resourcesClass || clearExceptionSilent(env)) {
        if (resourcesClass) deleteLocalRef(env, resourcesClass);
        return false;
    }
    JMethodId getSystem = getStaticMethodId(
        env, resourcesClass, "getSystem", "()Landroid/content/res/Resources;");
    JMethodId getConfiguration = getMethodId(
        env, resourcesClass, "getConfiguration",
        "()Landroid/content/res/Configuration;");
    if (!getSystem || !getConfiguration || clearExceptionSilent(env)) {
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    JObject resources = callStaticObjectMethodA(
        env, resourcesClass, getSystem, nullptr);
    JObject configuration = resources
        ? callObjectMethodA(env, resources, getConfiguration, nullptr)
        : nullptr;
    if (!resources || !configuration || clearExceptionSilent(env)) {
        if (configuration) deleteLocalRef(env, configuration);
        if (resources) deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    JClass configurationClass = getObjectClass(env, configuration);
    JFieldId uiModeField = configurationClass
        ? getFieldId(env, configurationClass, "uiMode", "I")
        : nullptr;
    if (!configurationClass || !uiModeField || clearExceptionSilent(env)) {
        if (configurationClass) deleteLocalRef(env, configurationClass);
        deleteLocalRef(env, configuration);
        deleteLocalRef(env, resources);
        deleteLocalRef(env, resourcesClass);
        return false;
    }

    const int uiMode = getIntField(env, configuration, uiModeField);
    const bool failed = clearExceptionSilent(env);
    if (!failed) {
        constexpr int UiModeNightMask = 0x30;
        constexpr int UiModeNightYes = 0x20;
        darkMode = (uiMode & UiModeNightMask) == UiModeNightYes;
    }

    deleteLocalRef(env, configurationClass);
    deleteLocalRef(env, configuration);
    deleteLocalRef(env, resources);
    deleteLocalRef(env, resourcesClass);
    return !failed;
}

bool hasRecordAudioPermission() {
    if (!gJavaVm) return false;
    EnvScope scope(gJavaVm);
    Env* env = scope.get();
    if (!env) return false;

    JClass activityThreadClass = findClass(env, "android/app/ActivityThread");
    if (!activityThreadClass || clearExceptionSilent(env)) {
        if (activityThreadClass) deleteLocalRef(env, activityThreadClass);
        return false;
    }
    JMethodId currentApplication = getStaticMethodId(
        env, activityThreadClass, "currentApplication", "()Landroid/app/Application;");
    if (!currentApplication || clearExceptionSilent(env)) {
        deleteLocalRef(env, activityThreadClass);
        return false;
    }
    JObject application = callStaticObjectMethodA(env, activityThreadClass,
                                                   currentApplication, nullptr);
    if (!application || clearExceptionSilent(env)) {
        if (application) deleteLocalRef(env, application);
        deleteLocalRef(env, activityThreadClass);
        return false;
    }
    JClass applicationClass = getObjectClass(env, application);
    JMethodId checkPermission = applicationClass
        ? getMethodId(env, applicationClass, "checkSelfPermission",
                      "(Ljava/lang/String;)I")
        : nullptr;
    if (!applicationClass || !checkPermission ||
        clearExceptionSilent(env)) {
        if (applicationClass) deleteLocalRef(env, applicationClass);
        deleteLocalRef(env, application);
        deleteLocalRef(env, activityThreadClass);
        return false;
    }
    JString permission = newStringUtf(env, "android.permission.RECORD_AUDIO");
    if (!permission || clearExceptionSilent(env)) {
        if (permission) deleteLocalRef(env, permission);
        deleteLocalRef(env, applicationClass);
        deleteLocalRef(env, application);
        deleteLocalRef(env, activityThreadClass);
        return false;
    }
    JValue arg{};
    arg.l = permission;
    const int result = callIntMethodA(env, application, checkPermission, &arg);
    const bool failed = clearExceptionSilent(env);
    deleteLocalRef(env, permission);
    deleteLocalRef(env, applicationClass);
    deleteLocalRef(env, application);
    deleteLocalRef(env, activityThreadClass);
    return !failed && result == 0;
}

bool isMinecraftActivityAlive() {
    // Native-created pthreads cannot FindClass application classes through the
    // bootstrap loader. Resolve the launcher state class through the
    // Application ClassLoader instead. Any lookup failure remains conservative.
    if (!gJavaVm) return true;
    EnvScope scope(gJavaVm);
    Env* env = scope.get();
    if (!env) return true;

    JClass activityThreadClass = findClass(env, "android/app/ActivityThread");
    if (!activityThreadClass || clearExceptionSilent(env)) {
        if (activityThreadClass) deleteLocalRef(env, activityThreadClass);
        return true;
    }
    JMethodId currentApplication = getStaticMethodId(
        env, activityThreadClass, "currentApplication", "()Landroid/app/Application;");
    JObject application = currentApplication
        ? callStaticObjectMethodA(env, activityThreadClass, currentApplication, nullptr)
        : nullptr;
    if (!currentApplication || !application || clearExceptionSilent(env)) {
        if (application) deleteLocalRef(env, application);
        deleteLocalRef(env, activityThreadClass);
        return true;
    }

    JClass applicationClass = getObjectClass(env, application);
    JMethodId getClassLoader = applicationClass
        ? getMethodId(env, applicationClass, "getClassLoader",
                      "()Ljava/lang/ClassLoader;")
        : nullptr;
    JObject classLoader = getClassLoader
        ? callObjectMethodA(env, application, getClassLoader, nullptr)
        : nullptr;
    JClass classLoaderClass = classLoader ? getObjectClass(env, classLoader) : nullptr;
    JMethodId loadClass = classLoaderClass
        ? getMethodId(env, classLoaderClass, "loadClass",
                      "(Ljava/lang/String;)Ljava/lang/Class;")
        : nullptr;
    JString stateName = newStringUtf(
        env, "org.levimc.launcher.core.minecraft.MinecraftActivityState");
    JValue nameArg{};
    nameArg.l = stateName;
    JClass stateClass = (loadClass && stateName)
        ? reinterpret_cast<JClass>(callObjectMethodA(
              env, classLoader, loadClass, &nameArg))
        : nullptr;

    bool failed = !applicationClass || !getClassLoader || !classLoader ||
                  !classLoaderClass || !loadClass || !stateName || !stateClass ||
                  clearExceptionSilent(env);
    bool running = true;
    if (!failed) {
        JMethodId isRunning = getStaticMethodId(env, stateClass, "isRunning", "()Z");
        if (!isRunning || clearExceptionSilent(env)) {
            failed = true;
        } else {
            running = callStaticBooleanMethodA(env, stateClass,
                                               isRunning, nullptr) != 0;
            if (clearExceptionSilent(env)) failed = true;
        }
    }

    if (stateClass) deleteLocalRef(env, stateClass);
    if (stateName) deleteLocalRef(env, stateName);
    if (classLoaderClass) deleteLocalRef(env, classLoaderClass);
    if (classLoader) deleteLocalRef(env, classLoader);
    if (applicationClass) deleteLocalRef(env, applicationClass);
    deleteLocalRef(env, application);
    deleteLocalRef(env, activityThreadClass);
    return failed ? true : running;
}

} // namespace jni
