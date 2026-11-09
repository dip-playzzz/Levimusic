#pragma once

#include "AndroidMediaPlayer.hpp"
#include "LeviAbi.hpp"
#include "MediaMetadata.hpp"
#include "MusicLibrary.hpp"
#include "VanillaUiAssets.hpp"

class ConfigManager;

class NativeMusicUi {
public:
    bool initialize(leviabi::Api* api, ConfigManager* config,
                    MusicLibrary* library, AndroidMediaPlayer* player,
                    MediaMetadata* metadata, VanillaUiAssets* assets);
    void shutdown();
    void handleModuleToggle(bool enabled);
    void forceDisabledDefault();
    void refresh();
    void redraw();
    bool isOpen() const { return mOpen != 0; }

private:
    static bool touchThunk(int action, int pointerId, float x, float y);
    static bool keyThunk(int keyCode, unsigned int unicodeChar, bool isKeyDown);
    static bool textThunk(const char* text, usize size);
    static void* animationThreadThunk(void* self);

    bool onTouch(const leviabi::TouchEvent& event);
    bool onKey(int keyCode, unsigned int unicodeChar, bool isKeyDown);
    bool onText(const char* text, usize size);
    void renderUnlocked();
    void refreshUnlocked();
    void rescanLibraryPreservingState();
    void tickPlaybackUnlocked();
    void startAnimationThread();
    void stopAnimationThread();
    void initializeSettingsEntries();
    void setOpenInternal(bool open);
    void refreshViewport();
    void restorePanelUiState();
    void savePanelUiState();

    void addRect(float x, float y, float w, float h, u32 color,
                 float radius = 0.0f);
    void addCircle(float x, float y, float radius, u32 color);
    void addTriangle(float x1, float y1, float x2, float y2,
                     float x3, float y3, u32 color);
    void addLine(float x1, float y1, float x2, float y2,
                 float width, u32 color);
    void addText(float x, float y, float size, const char* text, u32 color);
    void addTextCentered(float x, float y, float w, float h,
                         float size, const char* text, u32 color);
    void addTextRight(float rightX, float y, float size,
                      const char* text, u32 color);
    void addTextSegment(float x, float y, float size, const char* text,
                        int startGlyph, int maxBytes, u32 color);
    void addTextFlow(float x, float y, float size, const char* text,
                     float maxWidth, u32 color);
    void addTextWrapped(float x, float y, float size, const char* text,
                        float maxWidth, float lineHeight, int maxLines,
                        u32 color);
    float estimateTextWidth(const char* text, float size) const;
    void addImage(float x, float y, float w, float h,
                  const char* imageId, u32 color = 0xFFFFFFFFu);
    u32 applyUiOpacity(u32 color) const;

    bool hit(float px, float py, float x, float y, float w, float h) const;
    void calculateLayout(float& panelX, float& panelY,
                         float& panelW, float& panelH, float& scale);
    int classifyControl(float x, float y, float panelX, float panelY,
                        float panelW, float panelH, float scale) const;
    void activateControl(int control);
    void seekFromPointer(float x, float panelX, float panelY, float scale);
    void volumeFromPointer(float x, float panelX, float panelY, float scale);

    int currentSourceFilter() const;
    int visibleTrackCount() const;
    int selectedGlobalIndex() const;
    int globalIndexForRow(int row) const;
    int visibleIndexOfGlobal(int globalIndex) const;
    bool trackMatches(int globalIndex) const;
    int compareTracks(int leftGlobal, int rightGlobal) const;
    void rebuildVisibleIndex();
    void markFilterDirty();
    void selectTrackInternal(int globalIndex, bool autoplay, bool renderAfter,
                             int playbackSourceFilter = -999);
    void advanceTrack(int direction, bool fromCompletion);
    bool startPlayback(const char* path);
    void pollOnline();
    bool shouldSearchOnline();
    int randomVisibleIndex(int count, int current);
    void ensureSourceVisible();
    void setSearchActive(bool active);
    void clearSearch();
    void eraseLastSearchGlyph();
    void appendSearchText(const char* text, usize size);
    void formatTime(int milliseconds, char* out, usize capacity) const;
    u32 trackAccent(const char* text) const;
    bool useDarkTheme() const;

    void drawPreviousIcon(float centerX, float centerY, float scale, u32 color);
    void drawNextIcon(float centerX, float centerY, float scale, u32 color);
    void drawPlayPauseIcon(float centerX, float centerY, float scale,
                           bool playing, u32 color);
    void drawStopIcon(float centerX, float centerY, float scale, u32 color);
    void drawShuffleIcon(float centerX, float centerY, float scale, u32 color);
    void drawLoopIcon(float centerX, float centerY, float scale,
                      int loopMode, u32 color);
    void drawSearchIcon(float centerX, float centerY, float scale, u32 color);
    void drawCloseIcon(float centerX, float centerY, float scale, u32 color);
    void drawSortIcon(float centerX, float centerY, float scale, u32 color);
    void drawVolumeIcon(float centerX, float centerY, float scale,
                        bool muted, u32 color);

    leviabi::Api* mApi{};
    ConfigManager* mConfig{};
    MusicLibrary* mLibrary{};
    AndroidMediaPlayer* mPlayer{};
    MediaMetadata* mMetadata{};
    VanillaUiAssets* mAssets{};
    leviabi::ModuleInfo mModule{};
    leviabi::ConfigEntry mSettings[16]{};
    leviabi::DrawCommand mCommands[768]{};

    AndroidMediaPlayer::Snapshot mSnapshot{};
    float mSpectrum[24]{};
    int mVisibleIndices[MusicLibrary::MaxTracks]{};
    char mSearchText[128]{};
    char mPlaybackSourcePath[512]{};
    int mVisibleCount{};
    int mCommandCount{};
    int mSettingCount{};
    int mPage{};
    int mActiveTab{}; // 0 = All, 1..N = source index + 1
    int mSourceOffset{};
    int mPressedControl{};
    int mDragPointerId{-1};
    int mViewportWidth{};
    int mViewportHeight{};
    int mSortMode{}; // 0=scan order, 1=title A-Z, 2=artist A-Z
    int mPlaybackSourceFilter{-1}; // -1=All, otherwise source index
    int mAnimationFrame{};
    int mSwitchCooldownFrames{};
    int mLastPlaybackPositionMs{};
    int mLastPlaybackDurationMs{};
    int mCompletionIdleFrames{};
    int mVolumeBeforeMute{80};
    u32 mShuffleSeed{0xA5C31E27u};

    float mPanelX{};
    float mPanelY{};
    float mPanelW{};
    float mPanelH{};
    float mDragOffsetX{};
    float mDragOffsetY{};

    PThread mAnimationThread{};
    volatile int mUiBusy{};
    volatile int mAnimationStop{};
    volatile int mOpen{};
    volatile int mVisible{};
    volatile u32 mCapturedPointerMask{};
    bool mRegistered{};
    bool mPanelPositionValid{};
    bool mDragging{};
    bool mAnimationStarted{};
    bool mPlaybackWasRunning{};
    bool mCompletionHandled{};
    bool mMinecraftExitHandled{};
    bool mMinecraftActivitySeen{};
    bool mSearchActive{};
    bool mFilterDirty{true};
    bool mSystemDarkMode{true};
    int mLifecyclePollFrames{};
    int mConfigPollFrames{};
    int mThemePollFrames{};
};
