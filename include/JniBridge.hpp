#pragma once

#include "Platform.hpp"

namespace jni {

using JInt = int;
using JSize = int;
using JBoolean = unsigned char;
using JByte = signed char;
using JLong = long long;
using JFloat = float;
using JDouble = double;
using JObject = void*;
using JClass = void*;
using JString = void*;
using JArray = void*;
using JByteArray = void*;
using JIntArray = void*;
using JMethodId = void*;
using JFieldId = void*;

union JValue {
    JBoolean z;
    JByte b;
    unsigned short c;
    short s;
    JInt i;
    JLong j;
    JFloat f;
    JDouble d;
    JObject l;
};

struct Env { const void* const* functions; };
struct Vm { const void* const* functions; };

constexpr JInt Version16 = 0x00010006;
constexpr JInt Ok = 0;
constexpr JInt Detached = -2;

bool captureFromModContext(void* modContext);
Vm* javaVm();

class EnvScope {
public:
    explicit EnvScope(Vm* vm);
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;
    ~EnvScope();

    [[nodiscard]] Env* get() const { return mEnv; }
    [[nodiscard]] bool valid() const { return mEnv != nullptr; }

private:
    Vm* mVm{};
    Env* mEnv{};
    bool mAttached{};
};

JClass findClass(Env* env, const char* name);
JClass getObjectClass(Env* env, JObject object);
JMethodId getMethodId(Env* env, JClass clazz, const char* name,
                      const char* signature);
JMethodId getStaticMethodId(Env* env, JClass clazz, const char* name,
                            const char* signature);
JFieldId getFieldId(Env* env, JClass clazz, const char* name,
                    const char* signature);
JObject newObjectA(Env* env, JClass clazz, JMethodId method,
                   const JValue* arguments);
JObject callObjectMethodA(Env* env, JObject object, JMethodId method,
                          const JValue* arguments);
JObject callStaticObjectMethodA(Env* env, JClass clazz, JMethodId method,
                                const JValue* arguments);
JBoolean callStaticBooleanMethodA(Env* env, JClass clazz, JMethodId method,
                                  const JValue* arguments);
JObject getObjectField(Env* env, JObject object, JFieldId field);
JString newStringUtf(Env* env, const char* text);
const char* getStringUtfChars(Env* env, JString text, JBoolean* isCopy = nullptr);
void releaseStringUtfChars(Env* env, JString text, const char* chars);
JObject newGlobalRef(Env* env, JObject object);
void deleteGlobalRef(Env* env, JObject object);
void deleteLocalRef(Env* env, JObject object);
void callVoidMethodA(Env* env, JObject object, JMethodId method,
                     const JValue* arguments);
JBoolean callBooleanMethodA(Env* env, JObject object, JMethodId method,
                            const JValue* arguments);
JInt callIntMethodA(Env* env, JObject object, JMethodId method,
                    const JValue* arguments);
JInt getIntField(Env* env, JObject object, JFieldId field);
JSize getArrayLength(Env* env, JArray array);
JByteArray newByteArray(Env* env, JSize length);
JIntArray newIntArray(Env* env, JSize length);
void getByteArrayRegion(Env* env, JByteArray array, JSize start,
                        JSize length, JByte* out);
void getIntArrayRegion(Env* env, JIntArray array, JSize start,
                       JSize length, JInt* out);
bool checkAndClearException(Env* env, const char* operation);

bool queryDisplaySize(int& widthPixels, int& heightPixels);
bool querySystemDarkMode(bool& darkMode);
bool hasRecordAudioPermission();
bool isMinecraftActivityAlive();

} // namespace jni
