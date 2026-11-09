#pragma once
#include "JniBridge.hpp"

// Blocking HTTP(S) client implemented over java.net.HttpURLConnection through
// JNI, so the freestanding mod needs no networking or TLS library. Never call
// it from the game, UI or input threads.
namespace http {

struct Header {
    const char* name;
    const char* value;
};

struct Result {
    int status{-1};          // HTTP status, or -1 when the request failed
    long long received{};    // body bytes delivered to fd / buffer
    char contentRange[96]{}; // Content-Range response header, if any
};

// Body goes to `fd` when fd >= 0, otherwise into `buffer` (NUL-terminated,
// truncated at bufferCap - 1). `abortFlag` (optional) is polled between reads;
// when it becomes non-zero the transfer stops early.
Result request(jni::Vm* vm, const char* method, const char* url,
               const Header* headers, int headerCount, const char* body,
               int fd, char* buffer, int bufferCap,
               const volatile int* abortFlag);

} // namespace http
