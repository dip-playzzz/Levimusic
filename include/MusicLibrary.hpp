#pragma once
#include "Config.hpp"

struct MusicSource {
    char path[512];
    char label[64];
    bool configured;
    int trackCount;
};

struct MusicTrack {
    char path[512];
    char name[256];
    char title[256];
    char artist[160];
    char format[12];
    char artworkId[24];
    int sourceIndex;
    int metadataState; // 0=unchecked, 1=no art, 2=art registered
};

class MusicLibrary {
public:
    static constexpr int MaxTracks = 1024;
    static constexpr int MaxSources = 96;

    void scan(const LeviMusicConfig& config);
    int count() const { return mCount; }
    int sourceCount() const { return mSourceCount; }
    const MusicTrack* track(int index) const {
        return index >= 0 && index < mCount ? &mTracks[index] : nullptr;
    }
    MusicTrack* trackMutable(int index) {
        return index >= 0 && index < mCount ? &mTracks[index] : nullptr;
    }
    const MusicSource* source(int index) const {
        return index >= 0 && index < mSourceCount ? &mSources[index] : nullptr;
    }
    int filteredCount(int sourceIndex) const;
    int filteredGlobalIndex(int sourceIndex, int filteredIndex) const;
    int filteredIndexOfGlobal(int sourceIndex, int globalIndex) const;
    int globalIndexForPath(const char* path) const;
    int sourceIndexForPath(const char* path) const;

private:
    int addSource(const char* path, bool configured);
    void scanConfiguredPath(const char* path,
                            const LeviMusicConfig& config);
    void scanDirectory(const char* path, const LeviMusicConfig& config,
                       int depth, bool configuredRoot);
    void addTrack(const char* path, int sourceIndex);
    bool accepted(const char* path) const;
    void removeEmptySources();

    MusicTrack mTracks[MaxTracks]{};
    MusicSource mSources[MaxSources]{};
    int mCount{};
    int mSourceCount{};
};
