#include "Platform.hpp"

// Real liblog entry point (declared in Platform.hpp).
static char gLogPath[600];
static volatile int gLogLock;

void levi_log_set_path(const char* path) {
    if (!path) return;
    copyString(gLogPath, sizeof(gLogPath), path);
}

void levi_log(int priority, const char* tag, const char* format, ...) {
    char message[700];
    __builtin_va_list args;
    __builtin_va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    __builtin_va_end(args);

    __android_log_print(priority, tag, "%s", message);

    if (!gLogPath[0]) return;
    while (__atomic_exchange_n(&gLogLock, 1, __ATOMIC_ACQUIRE) != 0) {
        const TimeSpec delay{0, 200000L};
        nanosleep(&delay, nullptr);
    }
    // Keep the file small: start over once it grows past ~256 KB.
    static int written = 0;
    int flags = O_WRONLY_VALUE | O_CREAT_VALUE | O_APPEND_VALUE;
    if (written > 256 * 1024) { flags = O_WRONLY_VALUE | O_CREAT_VALUE | 512; written = 0; }
    const int fd = open(gLogPath, flags, 0644);
    if (fd >= 0) {
        const char* level = priority >= LOG_ERROR_VALUE ? "E " : "I ";
        char line[760];
        const int n = snprintf(line, sizeof(line), "%s%s: %s\n", level, tag, message);
        if (n > 0) {
            const usize len = static_cast<usize>(n) < sizeof(line)
                ? static_cast<usize>(n) : sizeof(line) - 1;
            write(fd, line, len);
            written += static_cast<int>(len);
        }
        close(fd);
    }
    __atomic_store_n(&gLogLock, 0, __ATOMIC_RELEASE);
}
