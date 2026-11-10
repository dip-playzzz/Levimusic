#include "Http.hpp"

namespace http {
namespace {
constexpr const char* LogTag = "LeviMusic";

struct Refs {
    jni::Env* env;
    jni::JObject items[24];
    int count{};
    explicit Refs(jni::Env* e) : env(e) {}
    ~Refs() {
        for (int i = count - 1; i >= 0; --i)
            if (items[i]) jni::deleteLocalRef(env, items[i]);
    }
    jni::JObject add(jni::JObject object) {
        if (object && count < 24) items[count++] = object;
        return object;
    }
};
} // namespace

Result request(jni::Vm* vm, const char* method, const char* url,
               const Header* headers, int headerCount, const char* body,
               int fd, char* buffer, int bufferCap,
               const volatile int* abortFlag) {
    Result result;
    if (buffer && bufferCap > 0) buffer[0] = 0;
    if (!vm || !url || !method) return result;
    jni::EnvScope scope(vm);
    jni::Env* env = scope.get();
    if (!env) return result;
    Refs refs(env);
    auto failed = [&](const char* what) {
        if (jni::checkAndClearException(env, what)) return true;
        return false;
    };

    jni::JClass urlClass = refs.add(jni::findClass(env, "java/net/URL"));
    jni::JClass httpClass = refs.add(jni::findClass(env, "java/net/HttpURLConnection"));
    jni::JClass connClass = refs.add(jni::findClass(env, "java/net/URLConnection"));
    jni::JClass inClass = refs.add(jni::findClass(env, "java/io/InputStream"));
    jni::JClass outClass = refs.add(jni::findClass(env, "java/io/OutputStream"));
    jni::JClass strClass = refs.add(jni::findClass(env, "java/lang/String"));
    if (!urlClass || !httpClass || !connClass || !inClass || !outClass ||
        !strClass || failed("FindClass(http)")) return result;

    jni::JMethodId urlCtor = jni::getMethodId(env, urlClass, "<init>", "(Ljava/lang/String;)V");
    jni::JMethodId openConnection = jni::getMethodId(env, urlClass, "openConnection", "()Ljava/net/URLConnection;");
    jni::JMethodId setMethod = jni::getMethodId(env, httpClass, "setRequestMethod", "(Ljava/lang/String;)V");
    jni::JMethodId getCode = jni::getMethodId(env, httpClass, "getResponseCode", "()I");
    jni::JMethodId getError = jni::getMethodId(env, httpClass, "getErrorStream", "()Ljava/io/InputStream;");
    jni::JMethodId disconnect = jni::getMethodId(env, httpClass, "disconnect", "()V");
    jni::JMethodId setProp = jni::getMethodId(env, connClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    jni::JMethodId setConnectTimeout = jni::getMethodId(env, connClass, "setConnectTimeout", "(I)V");
    jni::JMethodId setReadTimeout = jni::getMethodId(env, connClass, "setReadTimeout", "(I)V");
    jni::JMethodId setDoOutput = jni::getMethodId(env, connClass, "setDoOutput", "(Z)V");
    jni::JMethodId getOut = jni::getMethodId(env, connClass, "getOutputStream", "()Ljava/io/OutputStream;");
    jni::JMethodId getIn = jni::getMethodId(env, connClass, "getInputStream", "()Ljava/io/InputStream;");
    jni::JMethodId getHeader = jni::getMethodId(env, connClass, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
    jni::JMethodId getBytes = jni::getMethodId(env, strClass, "getBytes", "()[B");
    jni::JMethodId outWrite = jni::getMethodId(env, outClass, "write", "([B)V");
    jni::JMethodId outClose = jni::getMethodId(env, outClass, "close", "()V");
    jni::JMethodId inRead = jni::getMethodId(env, inClass, "read", "([B)I");
    jni::JMethodId inClose = jni::getMethodId(env, inClass, "close", "()V");
    if (!urlCtor || !openConnection || !setMethod || !getCode || !getError ||
        !disconnect || !setProp || !setConnectTimeout || !setReadTimeout ||
        !setDoOutput || !getOut || !getIn || !getHeader || !getBytes ||
        !outWrite || !outClose || !inRead || !inClose ||
        failed("GetMethodID(http)")) return result;

    jni::JValue a[2]{};
    a[0].l = refs.add(jni::newStringUtf(env, url));
    jni::JObject urlObject = refs.add(jni::newObjectA(env, urlClass, urlCtor, a));
    if (!urlObject || failed("new URL")) return result;
    jni::JObject conn = refs.add(jni::callObjectMethodA(env, urlObject, openConnection, nullptr));
    if (!conn || failed("URL.openConnection")) return result;

    a[0].l = refs.add(jni::newStringUtf(env, method));
    jni::callVoidMethodA(env, conn, setMethod, a);
    if (failed("setRequestMethod")) { jni::callVoidMethodA(env, conn, disconnect, nullptr); jni::checkAndClearException(env, "disconnect"); return result; }
    a[0].i = 10000;
    jni::callVoidMethodA(env, conn, setConnectTimeout, a);
    a[0].i = 20000;
    jni::callVoidMethodA(env, conn, setReadTimeout, a);
    for (int i = 0; i < headerCount; ++i) {
        if (!headers[i].name || !headers[i].value || !headers[i].value[0]) continue;
        jni::JValue h[2]{};
        h[0].l = jni::newStringUtf(env, headers[i].name);
        h[1].l = jni::newStringUtf(env, headers[i].value);
        if (h[0].l && h[1].l) jni::callVoidMethodA(env, conn, setProp, h);
        jni::checkAndClearException(env, "setRequestProperty");
        if (h[0].l) jni::deleteLocalRef(env, h[0].l);
        if (h[1].l) jni::deleteLocalRef(env, h[1].l);
    }

    bool ok = true;
    if (body) {
        a[0].z = 1;
        jni::callVoidMethodA(env, conn, setDoOutput, a);
        jni::JObject stream = refs.add(jni::callObjectMethodA(env, conn, getOut, nullptr));
        jni::JObject text = refs.add(jni::newStringUtf(env, body));
        jni::JObject bytes = text ? refs.add(jni::callObjectMethodA(env, text, getBytes, nullptr)) : nullptr;
        if (!stream || !bytes || failed("request body")) ok = false;
        else {
            a[0].l = bytes;
            jni::callVoidMethodA(env, stream, outWrite, a);
            if (failed("OutputStream.write")) ok = false;
            jni::callVoidMethodA(env, stream, outClose, nullptr);
            jni::checkAndClearException(env, "OutputStream.close");
        }
    }

    if (ok) {
        const int status = jni::callIntMethodA(env, conn, getCode, nullptr);
        if (failed("getResponseCode")) ok = false;
        else result.status = status;
    }

    if (ok) {
        a[0].l = refs.add(jni::newStringUtf(env, "Content-Range"));
        jni::JObject range = refs.add(jni::callObjectMethodA(env, conn, getHeader, a));
        if (range && !jni::checkAndClearException(env, "getHeaderField")) {
            if (const char* chars = jni::getStringUtfChars(env, range)) {
                copyString(result.contentRange, sizeof(result.contentRange), chars);
                jni::releaseStringUtfChars(env, range, chars);
            }
        } else {
            jni::checkAndClearException(env, "getHeaderField");
        }

        jni::JObject stream = refs.add(result.status >= 400
            ? jni::callObjectMethodA(env, conn, getError, nullptr)
            : jni::callObjectMethodA(env, conn, getIn, nullptr));
        if (failed("getInputStream") || !stream) {
            if (!stream && result.status < 400) result.status = -1;
        } else {
            jni::JByteArray chunk = refs.add(jni::newByteArray(env, 16384));
            char* scratch = static_cast<char*>(malloc(16384));
            int used = 0;
            while (chunk && scratch) {
                if (abortFlag && *abortFlag) break;
                a[0].l = chunk;
                const int n = jni::callIntMethodA(env, stream, inRead, a);
                if (failed("InputStream.read")) { result.status = -1; break; }
                if (n < 0) break;
                if (n == 0) continue;
                jni::getByteArrayRegion(env, chunk, 0, n, reinterpret_cast<jni::JByte*>(scratch));
                if (fd >= 0) {
                    long done = 0;
                    while (done < n) {
                        const long w = write(fd, scratch + done, static_cast<usize>(n - done));
                        if (w <= 0) { result.status = -1; break; }
                        done += w;
                    }
                    if (result.status < 0) break;
                } else if (buffer && used + 1 < bufferCap) {
                    const int room = bufferCap - 1 - used;
                    const int take = n < room ? n : room;
                    memcpy(buffer + used, scratch, static_cast<usize>(take));
                    used += take;
                    buffer[used] = 0;
                }
                result.received += n;
            }
            if (scratch) free(scratch);
            else result.status = -1;
            jni::callVoidMethodA(env, stream, inClose, nullptr);
            jni::checkAndClearException(env, "InputStream.close");
        }
    }
    jni::callVoidMethodA(env, conn, disconnect, nullptr);
    jni::checkAndClearException(env, "disconnect");
    return result;
}

} // namespace http
