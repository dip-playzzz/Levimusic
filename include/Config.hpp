#pragma once
#include "Platform.hpp"

struct LeviMusicConfig {
    static constexpr int MaxMusicDirectories = 24;
    char musicDirectories[MaxMusicDirectories][512];
    int musicDirectoryCount;
    char selectedAudio[512];
    int maxTracks;
    int uiWidth;
    int uiHeight;
    int volumePercent;
    int uiOpacityPercent;
    int uiScalePercent;
    bool autoResolutionScale;
    int uiMode; // 0=follow system, 1=dark, 2=light
    int loopMode; // 0=off, 1=all, 2=one
    bool shuffleEnabled;
    bool onlineEnabled; // search + stream/cached online playback

    // Persisted panel/UI state. The panel stays disabled by default; only its
    // geometry/navigation state is restored when the user opens it again.
    int uiPanelX;
    int uiPanelY;
    bool uiPanelPositionSaved;
    int uiSortMode;
    int uiPage;
    int uiSourceOffset;
    char uiActivePlaylist[512];
};

class ConfigManager {
public:
    void initializePaths();
    void load();
    void reloadFromDisk();
    bool pollForExternalChange();
    void saveSelected(const char* selectedPath);
    void setVolumePercent(int volumePercent);
    void setUiOpacityPercent(int opacityPercent);
    void setUiScalePercent(int scalePercent);
    void setAutoResolutionScale(bool enabled);
    void setUiMode(int mode);
    void setMaxTracks(int maxTracks);
    void setLoopMode(int loopMode);
    void setShuffleEnabled(bool enabled);
    void savePanelUiState(int panelX, int panelY, bool positionSaved,
                          const char* activePlaylist, int sortMode,
                          int page, int sourceOffset);
    void flushAutoSave();
    const LeviMusicConfig& config() const { return mConfig; }
    const char* configPath() const { return mConfigPath; }
    const char* modRoot() const { return mModRoot; }

private:
    void defaults();
    void parseArray(const char* json, const char* key);
    void parseString(const char* json, const char* key, char* out, usize outSize);
    int parseInteger(const char* json, const char* key, int fallback);
    bool parseBoolean(const char* json, const char* key, bool fallback);
    void addMusicDirectory(const char* value);
    void writeConfig();
    u64 hashBytes(const char* data, usize size) const;

    LeviMusicConfig mConfig{};
    char mModRoot[512]{};
    char mConfigDir[512]{};
    char mConfigPath[512]{};
    u64 mLastFileHash{};
};
