#pragma once
// Tiny tolerant JSON scanner for the InnerTube responses. Not a validating
// parser: it walks text by position, which is all that is needed to pick a few
// well-known keys out of very large documents without any allocation.
#include "Platform.hpp"

namespace json {

inline const char* skipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    return p;
}

// `p` points at the opening quote. Returns the position after the closing one.
inline const char* skipString(const char* p, const char* end) {
    ++p;
    while (p < end) {
        if (*p == '\\') { p += 2; continue; }
        if (*p == '"') return p + 1;
        ++p;
    }
    return end;
}

// Returns the position just after the value that starts at `p`.
inline const char* skipValue(const char* p, const char* end) {
    p = skipSpace(p, end);
    if (p >= end) return end;
    if (*p == '"') return skipString(p, end);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < end) {
            const char c = *p;
            if (c == '"') { p = skipString(p, end); continue; }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                --depth;
                ++p;
                if (depth == 0) return p;
                continue;
            }
            ++p;
        }
        return end;
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
           *p != '\n' && *p != '\r' && *p != '\t') ++p;
    return p;
}

// First `"key":` at any depth inside [begin, end). Returns the start of its
// value (after the colon and whitespace) or nullptr.
inline const char* findKey(const char* begin, const char* end, const char* key) {
    const usize n = strlen(key);
    const char* p = begin;
    while (p < end) {
        if (*p == '"') {
            const char* s = p + 1;
            if (static_cast<usize>(end - s) > n && memcmp(s, key, n) == 0 &&
                s[n] == '"') {
                const char* after = skipSpace(s + n + 1, end);
                if (after < end && *after == ':') return skipSpace(after + 1, end);
            }
            p = skipString(p, end);
            continue;
        }
        ++p;
    }
    return nullptr;
}

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool readHex4(const char* p, const char* end, u32& out) {
    if (end - p < 4) return false;
    u32 value = 0;
    for (int i = 0; i < 4; ++i) {
        const int digit = hexValue(p[i]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<u32>(digit);
    }
    out = value;
    return true;
}

// Decodes the JSON string at `v` (must be an opening quote) into UTF-8.
inline bool readString(const char* v, const char* end, char* out, usize cap) {
    if (!v || v >= end || *v != '"' || cap < 2) return false;
    usize o = 0;
    auto put = [&](u32 cp) {
        if (cp < 0x80u) {
            if (o + 1 < cap) out[o++] = static_cast<char>(cp);
        } else if (cp < 0x800u) {
            if (o + 2 < cap) {
                out[o++] = static_cast<char>(0xC0u | (cp >> 6));
                out[o++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            }
        } else if (cp < 0x10000u) {
            if (o + 3 < cap) {
                out[o++] = static_cast<char>(0xE0u | (cp >> 12));
                out[o++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                out[o++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            }
        } else {
            if (o + 4 < cap) {
                out[o++] = static_cast<char>(0xF0u | (cp >> 18));
                out[o++] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
                out[o++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                out[o++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            }
        }
    };
    const char* p = v + 1;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
            ++p;
            const char c = *p++;
            switch (c) {
            case 'n': put('\n'); break;
            case 't': put('\t'); break;
            case 'r': put('\r'); break;
            case 'b': put('\b'); break;
            case 'f': put('\f'); break;
            case 'u': {
                u32 cp = 0;
                if (!readHex4(p, end, cp)) break;
                p += 4;
                if (cp >= 0xD800u && cp <= 0xDBFFu && end - p >= 6 &&
                    p[0] == '\\' && p[1] == 'u') {
                    u32 low = 0;
                    if (readHex4(p + 2, end, low) && low >= 0xDC00u &&
                        low <= 0xDFFFu) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                        p += 6;
                    }
                }
                put(cp);
                break;
            }
            default: put(static_cast<u32>(static_cast<unsigned char>(c))); break;
            }
        } else {
            if (o + 1 < cap) out[o++] = *p;
            ++p;
        }
    }
    out[o] = 0;
    return true;
}

// Reads a number that may be bare (123) or quoted ("123").
inline long long readInteger(const char* v, const char* end, long long fallback) {
    if (!v || v >= end) return fallback;
    if (*v == '"') ++v;
    bool negative = false;
    if (v < end && *v == '-') { negative = true; ++v; }
    if (v >= end || *v < '0' || *v > '9') return fallback;
    long long value = 0;
    while (v < end && *v >= '0' && *v <= '9') {
        value = value * 10 + (*v - '0');
        ++v;
    }
    return negative ? -value : value;
}

} // namespace json
