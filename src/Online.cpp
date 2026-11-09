#include "Online.hpp"

#include "Http.hpp"
#include "Json.hpp"

namespace online {
namespace {

constexpr const char* LogTag = "LeviMusic";

// ---- Maintenance knobs ------------------------------------------------------
// YouTube changes client versions and policies without notice. If search or
// playback stops working, these are the values to bump first.
constexpr const char* SearchUrl = "https://www.youtube.com/youtubei/v1/search?prettyPrint=false";
constexpr const char* PlayerUrl = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
constexpr const char* WebClientVersion = "2.20250626.01.00";
constexpr const char* WebUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
constexpr const char* VisionClientVersion = "1.02";
constexpr const char* VisionDeviceModel = "RealityDevice17,1";
constexpr const char* VisionOsVersion = "26.5.23O471";
// googlevideo checks that the media request uses the same user agent.
constexpr const char* VisionUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15";
// Bounded Range requests: some googlevideo URLs reject open-ended ones.
constexpr long long ChunkBytes = 512 * 1024;
constexpr int SearchBufferBytes = 1536 * 1024;
constexpr int PlayerBufferBytes = 768 * 1024;
constexpr int RecentFiles = 6;

struct State {
    volatile int lock{};
    volatile int quit{};
    volatile int playGeneration{};
    AndroidMediaPlayer* player{};
    PThread thread{};
    bool running{};

    char cacheDir[600]{};

    bool hasSearch{};
    char searchText[256]{};
    bool hasPlay{};
    char playPath[520]{};
    char activePath[520]{};   // request currently pending or in flight

    OnlineEntry results[MusicLibrary::MaxOnlineEntries]{};
    int resultCount{};
    bool resultsReady{};
    char status[96]{};

    // Worker-thread only.
    char visitor[256]{};
    char recent[RecentFiles][640]{};
    int recentCount{};
};
State g;

void lockState() {
    while (__atomic_exchange_n(&g.lock, 1, __ATOMIC_ACQUIRE) != 0) {
        const TimeSpec delay{0, 200000L};
        nanosleep(&delay, nullptr);
    }
}
void unlockState() { __atomic_store_n(&g.lock, 0, __ATOMIC_RELEASE); }

void setStatus(const char* text) {
    lockState();
    copyString(g.status, sizeof(g.status), text ? text : "");
    unlockState();
}

void setStatusFormat(const char* format, long long a) {
    char text[96];
    snprintf(text, sizeof(text), format, a);
    setStatus(text);
}

bool startsWith(const char* text, const char* prefix) {
    return text && strncmp(text, prefix, strlen(prefix)) == 0;
}

bool validVideoId(const char* id) {
    if (!id || strlen(id) != 11) return false;
    for (int i = 0; i < 11; ++i) {
        const char c = id[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

void appendEscaped(char* dst, usize cap, usize& at, const char* text) {
    for (; text && *text && at + 8 < cap; ++text) {
        const unsigned char c = static_cast<unsigned char>(*text);
        if (c == '"' || c == '\\') { dst[at++] = '\\'; dst[at++] = static_cast<char>(c); }
        else if (c < 0x20u) { dst[at++] = ' '; }
        else dst[at++] = static_cast<char>(c);
    }
    dst[at] = 0;
}

bool fileExists(const char* path) {
    const int fd = open(path, O_RDONLY_VALUE);
    if (fd < 0) return false;
    close(fd);
    return true;
}

// ---- cache ------------------------------------------------------------------

bool hasSuffix(const char* text, const char* suffix) {
    const usize a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

void purgeCache() {
    if (!g.cacheDir[0]) return;
    DIR* directory = opendir(g.cacheDir);
    if (!directory) return;
    while (DirEnt* entry = readdir(directory)) {
        if (!hasSuffix(entry->d_name, ".m4a") && !hasSuffix(entry->d_name, ".webm") &&
            !hasSuffix(entry->d_name, ".part")) continue;
        char path[800];
        snprintf(path, sizeof(path), "%s/%s", g.cacheDir, entry->d_name);
        unlink(path);
    }
    closedir(directory);
}

void rememberFile(const char* path) {
    for (int i = 0; i < g.recentCount; ++i)
        if (strcmp(g.recent[i], path) == 0) return;
    if (g.recentCount == RecentFiles) {
        unlink(g.recent[0]);
        for (int i = 1; i < RecentFiles; ++i)
            memcpy(g.recent[i - 1], g.recent[i], sizeof(g.recent[i]));
        --g.recentCount;
    }
    copyString(g.recent[g.recentCount++], sizeof(g.recent[0]), path);
}

bool cachedFileFor(const char* id, char* out, usize cap) {
    char needle[32];
    snprintf(needle, sizeof(needle), "/%s.", id);
    for (int i = 0; i < g.recentCount; ++i) {
        if (strstr(g.recent[i], needle) && fileExists(g.recent[i])) {
            copyString(out, cap, g.recent[i]);
            return true;
        }
    }
    return false;
}

// ---- InnerTube ----------------------------------------------------------------

bool captureVisitor(const char* body, const char* end) {
    const char* value = json::findKey(body, end, "visitorData");
    if (!value) return false;
    char visitor[256];
    if (!json::readString(value, end, visitor, sizeof(visitor)) || !visitor[0]) return false;
    copyString(g.visitor, sizeof(g.visitor), visitor);
    return true;
}

// POSTs a WEB-client search. Returns the HTTP status; body lands in `buffer`.
int webSearch(const char* query, char* buffer, int capacity) {
    char body[1024];
    usize at = 0;
    at += static_cast<usize>(snprintf(
        body, sizeof(body),
        "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"%s\","
        "\"hl\":\"en\",\"gl\":\"US\"}},\"params\":\"EgIQAQ%%3D%%3D\",\"query\":\"",
        WebClientVersion));
    appendEscaped(body, sizeof(body) - 4, at, query);
    body[at++] = '"';
    body[at++] = '}';
    body[at] = 0;
    const http::Header headers[] = {
        {"Content-Type", "application/json"},
        {"User-Agent", WebUserAgent},
        {"Origin", "https://www.youtube.com"},
        {"Accept-Language", "en-US,en;q=0.9"},
    };
    const http::Result result = http::request(jni::javaVm(), "POST", SearchUrl,
                                              headers, 4, body, -1, buffer, capacity, nullptr);
    if (result.status == 200) captureVisitor(buffer, buffer + strlen(buffer));
    return result.status;
}

void ensureVisitor() {
    if (g.visitor[0]) return;
    char* buffer = static_cast<char*>(malloc(SearchBufferBytes));
    if (!buffer) return;
    webSearch("music", buffer, SearchBufferBytes);
    free(buffer);
}

void publishResults(const OnlineEntry* entries, int count) {
    lockState();
    for (int i = 0; i < count; ++i) g.results[i] = entries[i];
    g.resultCount = count;
    g.resultsReady = true;
    unlockState();
}

void directUrlEntry(const char* url, OnlineEntry& entry) {
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.path, sizeof(entry.path), "url:%s", url);
    // Title: last path segment, otherwise the host.
    char clean[256];
    copyString(clean, sizeof(clean), url);
    if (char* q = strchr(clean, '?')) *q = 0;
    if (char* h = strchr(clean, '#')) *h = 0;
    usize length = strlen(clean);
    while (length > 0 && clean[length - 1] == '/') clean[--length] = 0;
    const char* slash = strrchr(clean, '/');
    const char* name = slash ? slash + 1 : clean;
    if (!*name || strchr(name, ':')) {
        name = strstr(clean, "://") ? strstr(clean, "://") + 3 : clean;
    }
    copyString(entry.title, sizeof(entry.title), name);
    copyString(entry.artist, sizeof(entry.artist), "Direct stream");
}

void doSearch(const char* query) {
    if (startsWith(query, "http://") || startsWith(query, "https://")) {
        if (strlen(query) + 5 >= sizeof(OnlineEntry::path)) {
            setStatus("URL is too long");
            return;
        }
        OnlineEntry entry;
        directUrlEntry(query, entry);
        publishResults(&entry, 1);
        setStatus("");
        return;
    }

    setStatus("Searching...");
    char* buffer = static_cast<char*>(malloc(SearchBufferBytes));
    if (!buffer) { setStatus("Out of memory"); return; }
    const int status = webSearch(query, buffer, SearchBufferBytes);
    if (status != 200) {
        char text[96];
        snprintf(text, sizeof(text), status < 0 ? "Search failed (no network)"
                                                : "Search failed (HTTP %d)", status);
        setStatus(text);
        levi_log(LOG_ERROR_VALUE, LogTag, "search failed: status %d", status);
        free(buffer);
        return;
    }

    const char* end = buffer + strlen(buffer);
    static OnlineEntry found[MusicLibrary::MaxOnlineEntries];
    int count = 0;
    const char* cursor = buffer;
    while (count < MusicLibrary::MaxOnlineEntries) {
        const char* hit = strstr(cursor, "\"videoRenderer\"");
        if (!hit) break;
        const char* value = json::findKey(hit, end, "videoRenderer");
        if (!value || *value != '{') { cursor = hit + 15; continue; }
        const char* objectEnd = json::skipValue(value, end);
        cursor = objectEnd;

        char id[32]{};
        const char* idValue = json::findKey(value, objectEnd, "videoId");
        if (!idValue || !json::readString(idValue, objectEnd, id, sizeof(id)) ||
            !validVideoId(id)) continue;
        // Live streams and premieres have no length and cannot be downloaded.
        if (!json::findKey(value, objectEnd, "lengthText")) continue;

        OnlineEntry& entry = found[count];
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.path, sizeof(entry.path), "yt:%s", id);
        if (const char* title = json::findKey(value, objectEnd, "title")) {
            const char* titleEnd = json::skipValue(title, objectEnd);
            if (const char* text = json::findKey(title, titleEnd, "text"))
                json::readString(text, titleEnd, entry.title, sizeof(entry.title));
        }
        const char* owner = json::findKey(value, objectEnd, "ownerText");
        if (!owner) owner = json::findKey(value, objectEnd, "longBylineText");
        if (owner) {
            const char* ownerEnd = json::skipValue(owner, objectEnd);
            if (const char* text = json::findKey(owner, ownerEnd, "text"))
                json::readString(text, ownerEnd, entry.artist, sizeof(entry.artist));
        }
        if (!entry.title[0]) copyString(entry.title, sizeof(entry.title), id);
        if (!entry.artist[0]) copyString(entry.artist, sizeof(entry.artist), "YouTube");

        bool duplicate = false;
        for (int i = 0; i < count; ++i)
            if (strcmp(found[i].path, entry.path) == 0) { duplicate = true; break; }
        if (!duplicate) ++count;
    }
    free(buffer);

    levi_log(LOG_INFO_VALUE, LogTag, "search \"%s\": %d results", query, count);
    if (count == 0) {
        setStatus("No online results");
        return;
    }
    publishResults(found, count);
    setStatus("");
}

struct Stream {
    char url[3072];
    char extension[8];
    long long size;
};

// Picks an audio-only format with a plain URL (no signature cipher).
bool chooseFormat(const char* body, const char* end, Stream& stream) {
    const char* list = json::findKey(body, end, "adaptiveFormats");
    if (!list || *list != '[') return false;
    const char* listEnd = json::skipValue(list, end);
    int bestScore = 0;
    const char* p = json::skipSpace(list + 1, listEnd);
    while (p < listEnd && *p != ']') {
        if (*p != '{') { ++p; continue; }
        const char* objectEnd = json::skipValue(p, listEnd);
        char mime[96]{};
        const char* mimeValue = json::findKey(p, objectEnd, "mimeType");
        const char* urlValue = json::findKey(p, objectEnd, "url");
        if (mimeValue && urlValue && json::readString(mimeValue, objectEnd, mime, sizeof(mime)) &&
            startsWith(mime, "audio/")) {
            const long long itag = json::readInteger(json::findKey(p, objectEnd, "itag"), objectEnd, 0);
            const bool mp4 = startsWith(mime, "audio/mp4");
            int score = itag == 140 ? 4 : (mp4 ? 3 : 2);   // 140 = 128k AAC, cleanest
            if (score > bestScore) {
                char url[3072];
                if (json::readString(urlValue, objectEnd, url, sizeof(url)) &&
                    startsWith(url, "https://")) {
                    bestScore = score;
                    copyString(stream.url, sizeof(stream.url), url);
                    copyString(stream.extension, sizeof(stream.extension), mp4 ? "m4a" : "webm");
                    stream.size = json::readInteger(
                        json::findKey(p, objectEnd, "contentLength"), objectEnd, 0);
                }
            }
        }
        p = objectEnd;
    }
    return bestScore > 0;
}

bool resolveStream(const char* id, Stream& stream, char* error, usize errorCap) {
    char* buffer = static_cast<char*>(malloc(PlayerBufferBytes));
    if (!buffer) { copyString(error, errorCap, "Out of memory"); return false; }
    bool ok = false;
    for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
        ensureVisitor();
        char visitor[320]{};
        if (g.visitor[0]) snprintf(visitor, sizeof(visitor), ",\"visitorData\":\"%s\"", g.visitor);
        char body[1280];
        snprintf(body, sizeof(body),
                 "{\"context\":{\"client\":{\"clientName\":\"VISIONOS\",\"clientVersion\":\"%s\","
                 "\"deviceMake\":\"Apple\",\"deviceModel\":\"%s\",\"osName\":\"visionOS\","
                 "\"osVersion\":\"%s\",\"hl\":\"en\",\"gl\":\"US\"%s}},"
                 "\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}",
                 VisionClientVersion, VisionDeviceModel, VisionOsVersion, visitor, id);
        const http::Header headers[] = {
            {"Content-Type", "application/json"},
            {"User-Agent", VisionUserAgent},
            {"X-Goog-Visitor-Id", g.visitor},
            {"Origin", "https://www.youtube.com"},
        };
        const http::Result result = http::request(jni::javaVm(), "POST", PlayerUrl,
                                                  headers, 4, body, -1, buffer, PlayerBufferBytes, nullptr);
        if (result.status != 200) {
            snprintf(error, errorCap, result.status < 0 ? "No network" : "Player HTTP %d", result.status);
            g.visitor[0] = 0;   // refresh the session and retry once
            continue;
        }
        const char* end = buffer + strlen(buffer);
        captureVisitor(buffer, end);
        char state[48]{};
        char reason[96]{};
        if (const char* playability = json::findKey(buffer, end, "playabilityStatus")) {
            const char* pEnd = json::skipValue(playability, end);
            json::readString(json::findKey(playability, pEnd, "status"), pEnd, state, sizeof(state));
            json::readString(json::findKey(playability, pEnd, "reason"), pEnd, reason, sizeof(reason));
        }
        if (strcmp(state, "OK") != 0) {
            copyString(error, errorCap, reason[0] ? reason : (state[0] ? state : "Unplayable"));
            levi_log(LOG_ERROR_VALUE, LogTag, "player %s: %s %s", id, state, reason);
            g.visitor[0] = 0;
            continue;
        }
        if (chooseFormat(buffer, end, stream)) { ok = true; break; }
        copyString(error, errorCap, "No direct audio stream");
        levi_log(LOG_ERROR_VALUE, LogTag, "player %s: no audio format with a plain url", id);
    }
    free(buffer);
    return ok;
}

bool parseTotalFromRange(const char* contentRange, long long& total) {
    const char* slash = contentRange ? strchr(contentRange, '/') : nullptr;
    if (!slash) return false;
    long long value = 0;
    int digits = 0;
    for (++slash; *slash >= '0' && *slash <= '9'; ++slash, ++digits)
        value = value * 10 + (*slash - '0');
    if (digits == 0) return false;
    total = value;
    return true;
}

bool downloadAudio(const Stream& stream, const char* id, int generation,
                   char* finalPath, usize finalCap, char* error, usize errorCap) {
    char partPath[700];
    snprintf(partPath, sizeof(partPath), "%s/%s.part", g.cacheDir, id);
    snprintf(finalPath, finalCap, "%s/%s.%s", g.cacheDir, id, stream.extension);
    const int fd = open(partPath, O_WRONLY_VALUE | O_CREAT_VALUE | O_TRUNC_VALUE, 0644);
    if (fd < 0) { copyString(error, errorCap, "Cache folder not writable"); return false; }

    long long written = 0;
    long long total = stream.size;
    bool ok = false;
    for (;;) {
        if (g.playGeneration != generation) break;   // superseded by a newer request
        long long last = written + ChunkBytes - 1;
        if (total > 0 && last >= total) last = total - 1;
        char range[64];
        snprintf(range, sizeof(range), "bytes=%lld-%lld", written, last);
        const http::Header headers[] = {
            {"User-Agent", VisionUserAgent},
            {"Range", range},
        };
        const http::Result result = http::request(jni::javaVm(), "GET", stream.url,
                                                  headers, 2, nullptr, fd, nullptr, 0, nullptr);
        if (result.status == 416 && written > 0) { ok = true; break; }
        if (result.status != 206 && result.status != 200) {
            snprintf(error, errorCap, result.status < 0 ? "Download failed (no network)"
                                                        : "Download failed (HTTP %d)", result.status);
            levi_log(LOG_ERROR_VALUE, LogTag, "download %s: status %d", id, result.status);
            break;
        }
        written += result.received;
        if (parseTotalFromRange(result.contentRange, total)) {}
        if (result.status == 200) { ok = written > 0; break; }   // full body in one go
        if (result.received == 0) { ok = written > 0; break; }
        if (total > 0) {
            if (written >= total) { ok = true; break; }
            setStatusFormat("Loading %lld%%", written * 100 / total);
        } else {
            setStatusFormat("Loading %lld KB", written / 1024);
        }
    }
    close(fd);
    if (!ok) { unlink(partPath); return false; }
    if (rename(partPath, finalPath) != 0) {
        unlink(partPath);
        copyString(error, errorCap, "Could not store audio");
        return false;
    }
    return true;
}

void doPlay(const char* path, int generation) {
    AndroidMediaPlayer* player = g.player;
    if (!player) return;

    if (startsWith(path, "url:")) {
        setStatus("Connecting...");
        const bool started = player->playAs(path + 4, path);
        if (g.playGeneration != generation) { if (started) player->stop(); return; }
        setStatus(started ? "" : "Could not open stream");
        return;
    }
    if (!startsWith(path, "yt:") || !validVideoId(path + 3)) {
        setStatus("Unsupported online track");
        return;
    }

    const char* id = path + 3;
    char file[700]{};
    char error[96]{};
    if (!cachedFileFor(id, file, sizeof(file))) {
        setStatus("Resolving...");
        Stream stream{};
        if (!resolveStream(id, stream, error, sizeof(error))) {
            if (g.playGeneration == generation) setStatus(error);
            return;
        }
        if (g.playGeneration != generation) return;
        setStatus("Loading 0%");
        if (!downloadAudio(stream, id, generation, file, sizeof(file), error, sizeof(error))) {
            if (g.playGeneration == generation) setStatus(error[0] ? error : "Download cancelled");
            return;
        }
        rememberFile(file);
    }
    if (g.playGeneration != generation) return;
    const bool started = player->playAs(file, path);
    if (g.playGeneration != generation) { if (started) player->stop(); return; }
    setStatus(started ? "" : "Could not play audio");
    levi_log(LOG_INFO_VALUE, LogTag, "online play %s: %s", id, started ? "started" : "failed");
}

void* workerMain(void*) {
    jni::EnvScope keepAttached(jni::javaVm());   // reuse one JNI attachment
    while (!g.quit) {
        bool hasSearch = false, hasPlay = false;
        char query[256]{};
        char path[520]{};
        int generation = 0;
        lockState();
        if (g.hasSearch) { hasSearch = true; copyString(query, sizeof(query), g.searchText); g.hasSearch = false; }
        if (g.hasPlay) { hasPlay = true; copyString(path, sizeof(path), g.playPath); g.hasPlay = false; }
        generation = g.playGeneration;
        unlockState();

        if (hasSearch) doSearch(query);
        if (hasPlay && !g.quit) {
            doPlay(path, generation);
            lockState();
            if (g.playGeneration == generation) g.activePath[0] = 0;
            unlockState();
        }

        const TimeSpec delay{0, 100000000L};
        nanosleep(&delay, nullptr);
    }
    return nullptr;
}

} // namespace

bool start(AndroidMediaPlayer* player, const char* cacheDirectory) {
    if (g.running) return true;
    if (!player || !cacheDirectory || !*cacheDirectory) return false;
    g.player = player;
    copyString(g.cacheDir, sizeof(g.cacheDir), cacheDirectory);
    mkdir(g.cacheDir, 0755);
    purgeCache();
    g.quit = 0;
    g.hasSearch = g.hasPlay = false;
    g.recentCount = 0;
    if (pthread_create(&g.thread, nullptr, &workerMain, nullptr) != 0) return false;
    g.running = true;
    levi_log(LOG_INFO_VALUE, LogTag, "online service started (cache %s)", g.cacheDir);
    return true;
}

void shutdown() {
    if (!g.running) return;
    __atomic_add_fetch(&g.playGeneration, 1, __ATOMIC_ACQ_REL);   // abort any download
    __atomic_store_n(&g.quit, 1, __ATOMIC_RELEASE);
    pthread_join(g.thread, nullptr);
    g.running = false;
    g.player = nullptr;
    purgeCache();
}

void search(const char* query) {
    if (!g.running || !query) return;
    char trimmed[256];
    copyString(trimmed, sizeof(trimmed), query);
    usize length = strlen(trimmed);
    while (length > 0 && trimmed[length - 1] == ' ') trimmed[--length] = 0;
    const char* start = trimmed;
    while (*start == ' ') ++start;
    if (!*start) return;
    lockState();
    copyString(g.searchText, sizeof(g.searchText), start);
    g.hasSearch = true;
    copyString(g.status, sizeof(g.status), "Searching...");
    unlockState();
}

void requestPlay(const char* path) {
    if (!g.running || !path || !*path) return;
    lockState();
    copyString(g.playPath, sizeof(g.playPath), path);
    copyString(g.activePath, sizeof(g.activePath), path);
    g.hasPlay = true;
    __atomic_add_fetch(&g.playGeneration, 1, __ATOMIC_ACQ_REL);
    copyString(g.status, sizeof(g.status), "Loading...");
    unlockState();
}

void cancel() {
    if (!g.running) return;
    lockState();
    g.hasPlay = false;
    g.activePath[0] = 0;
    __atomic_add_fetch(&g.playGeneration, 1, __ATOMIC_ACQ_REL);
    g.status[0] = 0;
    unlockState();
}

bool isLoading(const char* path) {
    if (!g.running || !path || !*path) return false;
    lockState();
    const bool loading = g.activePath[0] && strcmp(g.activePath, path) == 0;
    unlockState();
    return loading;
}

bool takeResults(OnlineEntry* out, int capacity, int* count) {
    bool ready = false;
    lockState();
    if (g.resultsReady) {
        const int n = g.resultCount < capacity ? g.resultCount : capacity;
        for (int i = 0; i < n; ++i) out[i] = g.results[i];
        if (count) *count = n;
        g.resultsReady = false;
        ready = true;
    }
    unlockState();
    return ready;
}

void statusText(char* out, usize capacity) {
    lockState();
    copyString(out, capacity, g.status);
    unlockState();
}

} // namespace online
