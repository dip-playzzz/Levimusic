#include "MusicLibrary.hpp"

static const char* fileNameOf(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void normalizeDirectoryPath(const char* path, char* out,
                                   usize capacity) {
    copyString(out, capacity, path ? path : "");
    usize length = strlen(out);
    while (length > 1 && out[length - 1] == '/') out[--length] = 0;
}

static void parentDirectoryOf(const char* path, char* out, usize capacity) {
    normalizeDirectoryPath(path, out, capacity);
    char* slash = strrchr(out, '/');
    if (!slash) {
        copyString(out, capacity, ".");
        return;
    }
    if (slash == out) {
        slash[1] = 0;
        return;
    }
    *slash = 0;
}

static bool stringEqualIgnoreCase(const char* left, const char* right) {
    if (!left || !right) return left == right;
    usize index = 0;
    while (left[index] && right[index]) {
        if (!asciiEqualIgnoreCase(left[index], right[index])) return false;
        ++index;
    }
    return left[index] == right[index];
}

static const char* const AudioExtensions[] = {
    ".mp3", ".mp2", ".mp1", ".mpa", ".m4a", ".m4b", ".m4r",
    ".aac", ".adts", ".ogg", ".oga", ".opus", ".spx",
    ".wav", ".wave", ".bwf", ".flac", ".alac", ".ape", ".mac",
    ".wv", ".tta", ".tak", ".aif", ".aiff", ".aifc", ".caf",
    ".au", ".snd", ".amr", ".awb", ".ac3", ".ec3", ".eac3",
    ".dts", ".dtshd", ".wma", ".ra", ".aa", ".aax", ".voc",
    ".gsm", ".mid", ".midi", ".kar", ".xmf", ".mxmf", ".rtttl",
    ".rtx", ".ota", ".imy", ".mka"
};

bool MusicLibrary::accepted(const char* path) const {
    if (!path || !*path) return false;
    for (usize i = 0;
         i < sizeof(AudioExtensions) / sizeof(AudioExtensions[0]); ++i) {
        if (endsWithIgnoreCase(path, AudioExtensions[i])) return true;
    }
    return false;
}

int MusicLibrary::addSource(const char* rawPath, bool configured) {
    if (!rawPath || !*rawPath || mSourceCount >= MaxSources) return -1;

    char path[512]{};
    normalizeDirectoryPath(rawPath, path, sizeof(path));
    for (int i = 0; i < mSourceCount; ++i) {
        if (strcmp(mSources[i].path, path) == 0) {
            if (configured) mSources[i].configured = true;
            return i;
        }
    }

    MusicSource& source = mSources[mSourceCount];
    copyString(source.path, sizeof(source.path), path);
    source.configured = configured;

    const char* base = fileNameOf(path);
    char baseLabel[64]{};
    copyString(baseLabel, sizeof(baseLabel),
               base && *base ? base : "Audio");

    copyString(source.label, sizeof(source.label), baseLabel);
    int duplicateNumber = 2;
    bool unique = false;
    while (!unique) {
        unique = true;
        for (int i = 0; i < mSourceCount; ++i) {
            if (stringEqualIgnoreCase(mSources[i].label, source.label)) {
                unique = false;
                snprintf(source.label, sizeof(source.label), "%.48s (%d)",
                         baseLabel, duplicateNumber++);
                break;
            }
        }
    }

    return mSourceCount++;
}

void MusicLibrary::addTrack(const char* path, int sourceIndex) {
    if (!path || !*path || mCount >= MaxTracks) return;
    for (int i = 0; i < mCount; ++i) {
        if (strcmp(mTracks[i].path, path) == 0) return;
    }

    MusicTrack& track = mTracks[mCount];
    copyString(track.path, sizeof(track.path), path);
    copyString(track.name, sizeof(track.name), fileNameOf(path));
    char* dot = strrchr(track.name, '.');
    const char* extension = strrchr(path, '.');
    if (extension && extension[1]) {
        ++extension;
        usize i = 0;
        while (extension[i] && i + 1 < sizeof(track.format)) {
            char c = extension[i];
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            track.format[i++] = c;
        }
        track.format[i] = 0;
    } else {
        copyString(track.format, sizeof(track.format), "AUDIO");
    }
    if (dot) *dot = 0;
    copyString(track.title, sizeof(track.title), track.name);
    track.sourceIndex = sourceIndex;
    track.metadataState = 0;
    ++mCount;

    if (sourceIndex >= 0 && sourceIndex < mSourceCount)
        ++mSources[sourceIndex].trackCount;
}

void MusicLibrary::scanDirectory(const char* path,
                                 const LeviMusicConfig& config,
                                 int depth, bool configuredRoot) {
    if (!path || !*path || depth > 8 ||
        mCount >= config.maxTracks || mCount >= MaxTracks) return;

    // Create a playlist for this exact directory only when it contains audio
    // directly. Nested folders receive their own basename-labelled playlist.
    int sourceIndex = -1;
    DIR* directScan = opendir(path);
    if (!directScan) return;
    while (DirEnt* entry = readdir(directScan)) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        char child[768]{};
        copyString(child, sizeof(child), path);
        if (child[0] && child[strlen(child) - 1] != '/')
            appendString(child, sizeof(child), "/");
        appendString(child, sizeof(child), entry->d_name);
        if (entry->d_type != DT_DIR_VALUE && accepted(child)) {
            sourceIndex = addSource(path, configuredRoot);
            break;
        }
    }
    closedir(directScan);

    DIR* directory = opendir(path);
    if (!directory) return;
    while (DirEnt* entry = readdir(directory)) {
        if (mCount >= config.maxTracks || mCount >= MaxTracks) break;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;

        char child[768]{};
        copyString(child, sizeof(child), path);
        if (child[0] && child[strlen(child) - 1] != '/')
            appendString(child, sizeof(child), "/");
        appendString(child, sizeof(child), entry->d_name);

        if (entry->d_type == DT_DIR_VALUE) {
            scanDirectory(child, config, depth + 1, false);
        } else if (accepted(child)) {
            if (sourceIndex < 0)
                sourceIndex = addSource(path, configuredRoot);
            addTrack(child, sourceIndex);
        } else if (entry->d_type == 0) {
            // Some Android filesystems report DT_UNKNOWN. Opening the child
            // distinguishes a directory without relying on stat().
            DIR* probe = opendir(child);
            if (probe) {
                closedir(probe);
                scanDirectory(child, config, depth + 1, false);
            }
        }
    }
    closedir(directory);
}

void MusicLibrary::scanConfiguredPath(
    const char* path, const LeviMusicConfig& config) {
    if (!path || !*path ||
        mCount >= config.maxTracks || mCount >= MaxTracks) return;

    if (accepted(path)) {
        char parent[512]{};
        parentDirectoryOf(path, parent, sizeof(parent));
        const int sourceIndex = addSource(parent, true);
        addTrack(path, sourceIndex);
        return;
    }
    scanDirectory(path, config, 0, true);
}

void MusicLibrary::removeEmptySources() {
    int remap[MaxSources];
    for (int i = 0; i < MaxSources; ++i) remap[i] = -1;

    int writeIndex = 0;
    for (int readIndex = 0; readIndex < mSourceCount; ++readIndex) {
        if (mSources[readIndex].trackCount <= 0) continue;
        remap[readIndex] = writeIndex;
        if (writeIndex != readIndex)
            mSources[writeIndex] = mSources[readIndex];
        ++writeIndex;
    }

    for (int i = writeIndex; i < mSourceCount; ++i)
        memset(&mSources[i], 0, sizeof(mSources[i]));

    for (int i = 0; i < mCount; ++i) {
        const int oldIndex = mTracks[i].sourceIndex;
        mTracks[i].sourceIndex =
            oldIndex >= 0 && oldIndex < MaxSources ? remap[oldIndex] : -1;
    }
    mSourceCount = writeIndex;
}

void MusicLibrary::scan(const LeviMusicConfig& config) {
    mCount = 0;
    mSourceCount = 0;
    memset(mTracks, 0, sizeof(mTracks));
    memset(mSources, 0, sizeof(mSources));

    for (int i = 0; i < config.musicDirectoryCount; ++i) {
        scanConfiguredPath(config.musicDirectories[i], config);
        if (mCount >= config.maxTracks || mCount >= MaxTracks) break;
    }
    removeEmptySources();
}

int MusicLibrary::filteredCount(int sourceIndex) const {
    if (sourceIndex < 0) return mCount;
    int count = 0;
    for (int i = 0; i < mCount; ++i) {
        if (mTracks[i].sourceIndex == sourceIndex) ++count;
    }
    return count;
}

int MusicLibrary::filteredGlobalIndex(int sourceIndex,
                                      int filteredIndex) const {
    if (filteredIndex < 0) return -1;
    if (sourceIndex < 0)
        return filteredIndex < mCount ? filteredIndex : -1;
    int cursor = 0;
    for (int i = 0; i < mCount; ++i) {
        if (mTracks[i].sourceIndex != sourceIndex) continue;
        if (cursor++ == filteredIndex) return i;
    }
    return -1;
}

int MusicLibrary::filteredIndexOfGlobal(int sourceIndex,
                                        int globalIndex) const {
    if (globalIndex < 0 || globalIndex >= mCount) return -1;
    if (sourceIndex < 0) return globalIndex;
    int cursor = 0;
    for (int i = 0; i < mCount; ++i) {
        if (mTracks[i].sourceIndex != sourceIndex) continue;
        if (i == globalIndex) return cursor;
        ++cursor;
    }
    return -1;
}

int MusicLibrary::globalIndexForPath(const char* path) const {
    if (!path || !*path) return -1;
    for (int i = 0; i < mCount; ++i) {
        if (strcmp(mTracks[i].path, path) == 0) return i;
    }
    return -1;
}

int MusicLibrary::sourceIndexForPath(const char* path) const {
    if (!path || !*path) return -1;
    for (int i = 0; i < mSourceCount; ++i) {
        if (strcmp(mSources[i].path, path) == 0) return i;
    }
    return -1;
}
