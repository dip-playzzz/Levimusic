#include "Config.hpp"

extern "C" void* PLGetModRegistration();

static const char* skipSpaces(const char* p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static const char* findKey(const char* json, const char* key) {
    if (!json || !key) return nullptr;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(json, pattern);
}

u64 ConfigManager::hashBytes(const char* data, usize size) const {
    u64 hash = 1469598103934665603ull;
    if (!data) return hash;
    for (usize i = 0; i < size; ++i) {
        hash ^= static_cast<u8>(data[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

void ConfigManager::defaults() {
    memset(&mConfig, 0, sizeof(mConfig));
    mConfig.maxTracks = 1024;
    mConfig.uiWidth = 2400;
    mConfig.uiHeight = 1080;
    mConfig.volumePercent = 80;
    mConfig.uiOpacityPercent = 92;
    mConfig.uiScalePercent = 100;
    mConfig.autoResolutionScale = false;
    mConfig.uiMode = 0;
    mConfig.loopMode = 0;
    mConfig.shuffleEnabled = false;
    mConfig.uiPanelX = 0;
    mConfig.uiPanelY = 0;
    mConfig.uiPanelPositionSaved = false;
    mConfig.uiSortMode = 0;
    mConfig.uiPage = 0;
    mConfig.uiSourceOffset = 0;
    mConfig.uiActivePlaylist[0] = 0;
    addMusicDirectory("/storage/emulated/0/Music");
    addMusicDirectory("/storage/emulated/0/Download");
    addMusicDirectory("/storage/emulated/0/Downloads");
}

void ConfigManager::addMusicDirectory(const char* value) {
    if (!value || !*value ||
        mConfig.musicDirectoryCount >= LeviMusicConfig::MaxMusicDirectories) return;
    for (int i = 0; i < mConfig.musicDirectoryCount; ++i)
        if (strcmp(mConfig.musicDirectories[i], value) == 0) return;
    copyString(mConfig.musicDirectories[mConfig.musicDirectoryCount++], 512, value);
}

void ConfigManager::initializePaths() {
    DlInfo info{};
    if (dladdr(reinterpret_cast<const void*>(&PLGetModRegistration), &info) &&
        info.dli_fname) {
        copyString(mModRoot, sizeof(mModRoot), info.dli_fname);
        char* slash = strrchr(mModRoot, '/');
        if (slash) *slash = 0;
    } else {
        copyString(mModRoot, sizeof(mModRoot), ".");
    }
    copyString(mConfigDir, sizeof(mConfigDir), mModRoot);
    appendString(mConfigDir, sizeof(mConfigDir), "/config");
    mkdir(mConfigDir, 0755);
    copyString(mConfigPath, sizeof(mConfigPath), mConfigDir);
    appendString(mConfigPath, sizeof(mConfigPath), "/config.json");
}

void ConfigManager::parseString(const char* json, const char* key,
                                char* out, usize outSize) {
    const char* at = findKey(json, key);
    if (!at) return;
    at = strchr(at + strlen(key) + 2, ':');
    if (!at) return;
    at = skipSpaces(at + 1);
    if (!at || *at != '"') return;
    ++at;
    usize n = 0;
    while (*at && *at != '"' && n + 1 < outSize) {
        if (*at == '\\' && at[1]) ++at;
        out[n++] = *at++;
    }
    out[n] = 0;
}

void ConfigManager::parseArray(const char* json, const char* key) {
    const char* at = findKey(json, key);
    if (!at) return;
    at = strchr(at + strlen(key) + 2, '[');
    if (!at) return;
    ++at;
    while (*at && *at != ']') {
        at = skipSpaces(at);
        if (*at != '"') { ++at; continue; }
        ++at;
        char value[512]{};
        usize n = 0;
        while (*at && *at != '"' && n + 1 < sizeof(value)) {
            if (*at == '\\' && at[1]) ++at;
            value[n++] = *at++;
        }
        value[n] = 0;
        addMusicDirectory(value);
        if (*at == '"') ++at;
    }
}

int ConfigManager::parseInteger(const char* json, const char* key, int fallback) {
    const char* at = findKey(json, key);
    if (!at) return fallback;
    at = strchr(at + strlen(key) + 2, ':');
    if (!at) return fallback;
    at = skipSpaces(at + 1);
    int value = 0;
    bool any = false;
    while (*at >= '0' && *at <= '9') {
        any = true;
        value = value * 10 + (*at++ - '0');
    }
    return any ? value : fallback;
}

bool ConfigManager::parseBoolean(const char* json, const char* key, bool fallback) {
    const char* at = findKey(json, key);
    if (!at) return fallback;
    at = strchr(at + strlen(key) + 2, ':');
    if (!at) return fallback;
    at = skipSpaces(at + 1);
    if (strncmp(at, "true", 4) == 0) return true;
    if (strncmp(at, "false", 5) == 0) return false;
    return fallback;
}

void ConfigManager::load() {
    defaults();
    int fd = open(mConfigPath, O_RDONLY_VALUE);
    if (fd < 0) { writeConfig(); return; }
    static char json[65536];
    long count = read(fd, json, sizeof(json) - 1);
    close(fd);
    if (count <= 0) return;
    json[count] = 0;
    mLastFileHash = hashBytes(json, static_cast<usize>(count));

    // Built-in Android music roots always remain active. Any additional
    // entries in music_directories are appended and hot-reloaded.
    parseArray(json, "music_directories");

    parseString(json, "selected_audio", mConfig.selectedAudio,
                sizeof(mConfig.selectedAudio));
    mConfig.maxTracks = parseInteger(json, "max_tracks", mConfig.maxTracks);
    if (mConfig.maxTracks < 1) mConfig.maxTracks = 1;
    if (mConfig.maxTracks > 1024) mConfig.maxTracks = 1024;
    mConfig.uiWidth = parseInteger(json, "ui_width", mConfig.uiWidth);
    mConfig.uiHeight = parseInteger(json, "ui_height", mConfig.uiHeight);
    mConfig.volumePercent = parseInteger(json, "volume_percent", mConfig.volumePercent);
    if (mConfig.volumePercent < 0) mConfig.volumePercent = 0;
    if (mConfig.volumePercent > 100) mConfig.volumePercent = 100;
    mConfig.uiOpacityPercent = parseInteger(json, "ui_opacity_percent",
                                             mConfig.uiOpacityPercent);
    if (mConfig.uiOpacityPercent < 20) mConfig.uiOpacityPercent = 20;
    if (mConfig.uiOpacityPercent > 100) mConfig.uiOpacityPercent = 100;
    mConfig.uiScalePercent = parseInteger(json, "ui_scale_percent",
                                          mConfig.uiScalePercent);
    if (mConfig.uiScalePercent < 40) mConfig.uiScalePercent = 40;
    if (mConfig.uiScalePercent > 150) mConfig.uiScalePercent = 150;
    mConfig.autoResolutionScale = parseBoolean(json, "auto_resolution_scale", false);
    mConfig.uiMode = parseInteger(json, "ui_mode", mConfig.uiMode);
    if (mConfig.uiMode < 0 || mConfig.uiMode > 2) mConfig.uiMode = 0;
    mConfig.loopMode = parseInteger(json, "loop_mode", mConfig.loopMode);
    if (mConfig.loopMode < 0 || mConfig.loopMode > 2) mConfig.loopMode = 0;
    mConfig.shuffleEnabled = parseBoolean(json, "shuffle_enabled", false);

    mConfig.uiPanelX = parseInteger(json, "ui_panel_x", mConfig.uiPanelX);
    mConfig.uiPanelY = parseInteger(json, "ui_panel_y", mConfig.uiPanelY);
    mConfig.uiPanelPositionSaved = parseBoolean(
        json, "ui_panel_position_saved", mConfig.uiPanelPositionSaved);
    mConfig.uiSortMode = parseInteger(json, "ui_sort_mode", mConfig.uiSortMode);
    if (mConfig.uiSortMode < 0 || mConfig.uiSortMode > 2) mConfig.uiSortMode = 0;
    mConfig.uiPage = parseInteger(json, "ui_page", mConfig.uiPage);
    if (mConfig.uiPage < 0) mConfig.uiPage = 0;
    mConfig.uiSourceOffset = parseInteger(
        json, "ui_source_offset", mConfig.uiSourceOffset);
    if (mConfig.uiSourceOffset < 0) mConfig.uiSourceOffset = 0;
    parseString(json, "ui_active_playlist", mConfig.uiActivePlaylist,
                sizeof(mConfig.uiActivePlaylist));

}

void ConfigManager::writeConfig() {
    char temp[600];
    copyString(temp, sizeof(temp), mConfigPath);
    appendString(temp, sizeof(temp), ".tmp");
    int fd = open(temp, O_WRONLY_VALUE | O_CREAT_VALUE | O_TRUNC_VALUE, 0644);
    if (fd < 0) return;

    static char json[32768];
    int at = 0;
    at += snprintf(json + at, sizeof(json) - at,
                   "{\n  \"music_directories\": [\n");
    for (int i = 0; i < mConfig.musicDirectoryCount && at < 30000; ++i) {
        at += snprintf(json + at, sizeof(json) - at, "    \"%s\"%s\n",
                       mConfig.musicDirectories[i],
                       i + 1 == mConfig.musicDirectoryCount ? "" : ",");
    }
    at += snprintf(json + at, sizeof(json) - at,
                   "  ],\n  \"selected_audio\": \"%s\",\n"
                   "  \"max_tracks\": %d,\n"
                   "  \"ui_width\": %d,\n"
                   "  \"ui_height\": %d,\n"
                   "  \"volume_percent\": %d,\n"
                   "  \"ui_opacity_percent\": %d,\n"
                   "  \"ui_scale_percent\": %d,\n"
                   "  \"auto_resolution_scale\": %s,\n"
                   "  \"ui_mode\": %d,\n"
                   "  \"loop_mode\": %d,\n"
                   "  \"shuffle_enabled\": %s,\n"
                   "  \"ui_panel_x\": %d,\n"
                   "  \"ui_panel_y\": %d,\n"
                   "  \"ui_panel_position_saved\": %s,\n"
                   "  \"ui_sort_mode\": %d,\n"
                   "  \"ui_page\": %d,\n"
                   "  \"ui_source_offset\": %d,\n"
                   "  \"ui_active_playlist\": \"%s\"\n}\n",
                   mConfig.selectedAudio, mConfig.maxTracks,
                   mConfig.uiWidth, mConfig.uiHeight, mConfig.volumePercent,
                   mConfig.uiOpacityPercent, mConfig.uiScalePercent,
                   mConfig.autoResolutionScale ? "true" : "false",
                   mConfig.uiMode, mConfig.loopMode,
                   mConfig.shuffleEnabled ? "true" : "false",
                   mConfig.uiPanelX, mConfig.uiPanelY,
                   mConfig.uiPanelPositionSaved ? "true" : "false",
                   mConfig.uiSortMode, mConfig.uiPage, mConfig.uiSourceOffset,
                   mConfig.uiActivePlaylist);
    write(fd, json, static_cast<usize>(at));
    close(fd);
    rename(temp, mConfigPath);
    mLastFileHash = hashBytes(json, static_cast<usize>(at));
}

bool ConfigManager::pollForExternalChange() {
    int fd = open(mConfigPath, O_RDONLY_VALUE);
    if (fd < 0) return false;
    static char json[65536];
    const long count = read(fd, json, sizeof(json) - 1);
    close(fd);
    if (count <= 0) return false;
    const u64 hash = hashBytes(json, static_cast<usize>(count));
    if (hash == mLastFileHash) return false;
    load();
    return true;
}

void ConfigManager::saveSelected(const char* selectedPath) {
    copyString(mConfig.selectedAudio, sizeof(mConfig.selectedAudio),
               selectedPath ? selectedPath : "");
    writeConfig();
}

void ConfigManager::setVolumePercent(int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    mConfig.volumePercent = value;
    writeConfig();
}

void ConfigManager::setUiOpacityPercent(int value) {
    if (value < 20) value = 20;
    if (value > 100) value = 100;
    mConfig.uiOpacityPercent = value;
    writeConfig();
}

void ConfigManager::setUiScalePercent(int value) {
    if (value < 40) value = 40;
    if (value > 150) value = 150;
    mConfig.uiScalePercent = value;
    writeConfig();
}

void ConfigManager::setAutoResolutionScale(bool enabled) {
    mConfig.autoResolutionScale = enabled;
    writeConfig();
}

void ConfigManager::setUiMode(int value) {
    if (value < 0 || value > 2) value = 0;
    mConfig.uiMode = value;
    writeConfig();
}

void ConfigManager::setMaxTracks(int value) {
    if (value < 1) value = 1;
    if (value > 1024) value = 1024;
    mConfig.maxTracks = value;
    writeConfig();
}

void ConfigManager::setLoopMode(int value) {
    if (value < 0 || value > 2) value = 0;
    mConfig.loopMode = value;
    writeConfig();
}

void ConfigManager::setShuffleEnabled(bool enabled) {
    mConfig.shuffleEnabled = enabled;
    writeConfig();
}


void ConfigManager::savePanelUiState(int panelX, int panelY,
                                     bool positionSaved,
                                     const char* activePlaylist,
                                     int sortMode, int page,
                                     int sourceOffset) {
    if (panelX < 0) panelX = 0;
    if (panelY < 0) panelY = 0;
    if (sortMode < 0 || sortMode > 2) sortMode = 0;
    if (page < 0) page = 0;
    if (sourceOffset < 0) sourceOffset = 0;
    mConfig.uiPanelX = panelX;
    mConfig.uiPanelY = panelY;
    mConfig.uiPanelPositionSaved = positionSaved;
    mConfig.uiSortMode = sortMode;
    mConfig.uiPage = page;
    mConfig.uiSourceOffset = sourceOffset;
    copyString(mConfig.uiActivePlaylist, sizeof(mConfig.uiActivePlaylist),
               activePlaylist ? activePlaylist : "");
    // Atomic temp-file + rename makes every panel/config change durable
    // without a manual Save/Reload button.
    writeConfig();
}

void ConfigManager::flushAutoSave() {
    // Existing setters already auto-save immediately. This explicit flush is
    // used by lifecycle shutdown to persist the final combined UI state too.
    writeConfig();
}

void ConfigManager::reloadFromDisk() { load(); }
