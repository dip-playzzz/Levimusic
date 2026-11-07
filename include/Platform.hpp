#pragma once

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using s32 = int;
using usize = unsigned long;
using uptr = unsigned long;

extern "C" {
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
int dlclose(void*);
struct DlInfo { const char* dli_fname; void* dli_fbase; const char* dli_sname; void* dli_saddr; };
int dladdr(const void*, DlInfo*);

void* malloc(usize);
void free(void*);
void* memcpy(void*, const void*, usize);
void* memset(void*, int, usize);
int memcmp(const void*, const void*, usize);
usize strlen(const char*);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, usize);
char* strstr(const char*, const char*);
char* strchr(const char*, int);
char* strrchr(const char*, int);
int snprintf(char*, usize, const char*, ...);

int open(const char*, int, ...);
long read(int, void*, usize);
long write(int, const void*, usize);
int close(int);
int mkdir(const char*, unsigned int);
int rename(const char*, const char*);

struct DIR;
struct DirEnt {
    u64 d_ino;
    long long d_off;
    u16 d_reclen;
    u8 d_type;
    char d_name[256];
};
DIR* opendir(const char*);
DirEnt* readdir(DIR*);
int closedir(DIR*);

using PThread = unsigned long;
int pthread_create(PThread*, const void*, void* (*)(void*), void*);
int pthread_join(PThread, void**);
struct TimeSpec { long tv_sec; long tv_nsec; };
int nanosleep(const TimeSpec*, TimeSpec*);

int mprotect(void*, usize, int);
long sysconf(int);

struct Elf64Phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
};
struct DlPhdrInfo {
    uptr dlpi_addr;
    const char* dlpi_name;
    const Elf64Phdr* dlpi_phdr;
    u16 dlpi_phnum;
};
int dl_iterate_phdr(int (*)(DlPhdrInfo*, usize, void*), void*);

int __android_log_print(int, const char*, const char*, ...);
}

constexpr int RTLD_NOW_VALUE = 2;
constexpr int RTLD_NOLOAD_VALUE = 4;
constexpr int O_RDONLY_VALUE = 0;
constexpr int O_WRONLY_VALUE = 1;
constexpr int O_CREAT_VALUE = 64;
constexpr int O_TRUNC_VALUE = 512;
constexpr int PROT_READ_VALUE = 1;
constexpr int PROT_WRITE_VALUE = 2;
constexpr int PROT_EXEC_VALUE = 4;
constexpr int LOG_INFO_VALUE = 4;
constexpr int LOG_WARN_VALUE = 5;
constexpr int LOG_ERROR_VALUE = 6;
constexpr int DT_DIR_VALUE = 4;
constexpr int DT_REG_VALUE = 8;

inline bool asciiEqualIgnoreCase(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
    return a == b;
}

inline bool endsWithIgnoreCase(const char* text, const char* suffix) {
    if (!text || !suffix) return false;
    const usize n = strlen(text);
    const usize m = strlen(suffix);
    if (m > n) return false;
    for (usize i = 0; i < m; ++i) {
        if (!asciiEqualIgnoreCase(text[n - m + i], suffix[i])) return false;
    }
    return true;
}

inline void copyString(char* dst, usize capacity, const char* src) {
    if (!dst || capacity == 0) return;
    usize i = 0;
    if (src) {
        for (; i + 1 < capacity && src[i]; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

inline void appendString(char* dst, usize capacity, const char* src) {
    if (!dst || !src || capacity == 0) return;
    usize at = strlen(dst);
    usize i = 0;
    while (at + 1 < capacity && src[i]) dst[at++] = src[i++];
    dst[at] = '\0';
}
