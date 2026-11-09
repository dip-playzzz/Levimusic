#include "NativeMusicUi.hpp"
#include "Online.hpp"
#include "Config.hpp"
#include "JniBridge.hpp"

static NativeMusicUi* gNativeMusicUi = nullptr;
static constexpr const char* ModuleId = "levi_music";
static constexpr const char* LogTag = "LeviMusic";

static constexpr float BasePanelWidth = 1240.0f;
static constexpr float BasePanelHeight = 760.0f;
static constexpr int RowsPerPage = 7;
static constexpr int VisibleSources = 7;

static constexpr int ControlRescan = 1;
static constexpr int ControlPagePrev = 2;
static constexpr int ControlPageNext = 3;
static constexpr int ControlTrackPrev = 4;
static constexpr int ControlPlayPause = 5;
static constexpr int ControlStop = 6;
static constexpr int ControlTrackNext = 7;
static constexpr int ControlSeek = 8;
static constexpr int ControlVolumeBar = 9;
static constexpr int ControlShuffle = 10;
static constexpr int ControlLoop = 11;
static constexpr int ControlSourcePrev = 12;
static constexpr int ControlSourceNext = 13;
static constexpr int ControlSearch = 14;
static constexpr int ControlClearSearch = 15;
static constexpr int ControlSort = 16;
static constexpr int ControlMute = 17;
static constexpr int ControlAllSource = 18;
static constexpr int ControlSourceBase = 40;
static constexpr int ControlRowBase = 100;

static float minFloat(float a, float b) { return a < b ? a : b; }
static float maxFloat(float a, float b) { return a > b ? a : b; }
static float clampFloat(float value, float low, float high) {
    if (high < low) return low;
    return maxFloat(low, minFloat(value, high));
}

static char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a') : value;
}

static int compareIgnoreCase(const char* left, const char* right) {
    if (!left) left = "";
    if (!right) right = "";
    usize i = 0;
    while (left[i] && right[i]) {
        const unsigned char a = static_cast<unsigned char>(lowerAscii(left[i]));
        const unsigned char b = static_cast<unsigned char>(lowerAscii(right[i]));
        if (a < b) return -1;
        if (a > b) return 1;
        ++i;
    }
    if (left[i]) return 1;
    if (right[i]) return -1;
    return 0;
}

static bool containsIgnoreCase(const char* text, const char* query) {
    if (!query || !*query) return true;
    if (!text || !*text) return false;
    const usize queryLength = strlen(query);
    const usize textLength = strlen(text);
    if (queryLength > textLength) return false;
    for (usize start = 0; start + queryLength <= textLength; ++start) {
        bool same = true;
        for (usize i = 0; i < queryLength; ++i) {
            if (lowerAscii(text[start + i]) != lowerAscii(query[i])) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

class ScopedUiBusy {
public:
    ScopedUiBusy(volatile int* flag, bool wait) : mFlag(flag) {
        if (!mFlag) return;
        if (wait) {
            while (__atomic_exchange_n(mFlag, 1, __ATOMIC_ACQUIRE) != 0) {
                const TimeSpec delay{0, 500000L};
                nanosleep(&delay, nullptr);
            }
            mLocked = true;
        } else {
            int expected = 0;
            mLocked = __atomic_compare_exchange_n(
                mFlag, &expected, 1, false, __ATOMIC_ACQUIRE,
                __ATOMIC_RELAXED);
        }
    }
    ~ScopedUiBusy() {
        if (mLocked) __atomic_store_n(mFlag, 0, __ATOMIC_RELEASE);
    }
    bool locked() const { return mLocked; }
private:
    volatile int* mFlag{};
    bool mLocked{};
};

static void setConfigEntry(leviabi::ConfigEntry& entry, const char* key,
                           const char* displayName, leviabi::ConfigType type,
                           const char* defaultValue, const char* minValue = "",
                           const char* maxValue = "") {
    memset(&entry, 0, sizeof(entry));
    leviabi::setShortString(entry.key, key);
    leviabi::setShortString(entry.displayName, displayName);
    entry.type = type;
    leviabi::setShortString(entry.defaultValue, defaultValue);
    leviabi::setShortString(entry.minValue, minValue);
    leviabi::setShortString(entry.maxValue, maxValue);
}

bool NativeMusicUi::initialize(leviabi::Api* api, ConfigManager* config,
                               MusicLibrary* library,
                               AndroidMediaPlayer* player,
                               MediaMetadata* metadata,
                               VanillaUiAssets* assets) {
    mApi = api;
    mConfig = config;
    mLibrary = library;
    mPlayer = player;
    mMetadata = metadata;
    mAssets = assets;
    if (!mApi || !mConfig || !mLibrary || !mPlayer || !mMetadata ||
        !mAssets || !mAssets->ready()) return false;

    memset(&mModule, 0, sizeof(mModule));
    leviabi::initializeEmptyCallbacks(mModule);
    leviabi::setShortString(mModule.moduleId, ModuleId);
    leviabi::setShortString(mModule.displayName,
                            "LeviMusic");
    leviabi::setShortString(mModule.description, "Local music library");
    if (leviabi::ownerModId()[0]) leviabi::fillOwnerModId(mModule.modId);
    levi_log(LOG_INFO_VALUE, LogTag, "registering module id=%s modId=%s",
             ModuleId, leviabi::ownerModId());
    mModule.defaultEnabled = false;
    initializeSettingsEntries();
    if (mSettingCount <= 0 || mSettingCount > 16) return false;
    mModule.configs.begin = mSettings;
    mModule.configs.end = mSettings + mSettingCount;
    mModule.configs.capacityEnd = mSettings + mSettingCount;
    if (!leviabi::callbacksAreEmpty(mModule)) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "Mod Menu callback ABI validation failed");
        return false;
    }
    mRegistered = mApi->registerModule(&mModule);
    levi_log(LOG_INFO_VALUE, LogTag, "registerModule returned %d",
             mRegistered ? 1 : 0);
    if (!mRegistered) return false;

    refreshViewport();
    bool detectedDark = true;
    if (jni::querySystemDarkMode(detectedDark))
        mSystemDarkMode = detectedDark;
    mVolumeBeforeMute = mConfig->config().volumePercent > 0
        ? mConfig->config().volumePercent : 80;
    restorePanelUiState();
    gNativeMusicUi = this;
    mApi->input->registerTouchCallback(&NativeMusicUi::touchThunk);
    if (mApi->input->registerKeyEventCallback)
        mApi->input->registerKeyEventCallback(&NativeMusicUi::keyThunk);
    if (mApi->input->registerTextInputCallback)
        mApi->input->registerTextInputCallback(&NativeMusicUi::textThunk);
    setOpenInternal(false);
    startAnimationThread();
    return true;
}

void NativeMusicUi::initializeSettingsEntries() {
    char volume[8]{};
    char opacity[8]{};
    char uiScale[8]{};
    char limit[8]{};
    char uiMode[4]{};
    char loop[4]{};
    snprintf(volume, sizeof(volume), "%d", mConfig->config().volumePercent);
    snprintf(opacity, sizeof(opacity), "%d",
             mConfig->config().uiOpacityPercent);
    snprintf(uiScale, sizeof(uiScale), "%d",
             mConfig->config().uiScalePercent);
    snprintf(limit, sizeof(limit), "%d", mConfig->config().maxTracks);
    snprintf(uiMode, sizeof(uiMode), "%d", mConfig->config().uiMode);
    snprintf(loop, sizeof(loop), "%d", mConfig->config().loopMode);
    mSettingCount = 0;
    setConfigEntry(mSettings[mSettingCount++], "volume_percent", "Volume",
                   leviabi::ConfigType::SliderInt, volume, "0", "100");
    setConfigEntry(mSettings[mSettingCount++], "ui_opacity_percent",
                   "UI Opacity", leviabi::ConfigType::SliderInt,
                   opacity, "20", "100");
    setConfigEntry(mSettings[mSettingCount++], "ui_scale_percent",
                   "UI Scale", leviabi::ConfigType::SliderInt,
                   uiScale, "40", "150");
    setConfigEntry(mSettings[mSettingCount++], "auto_resolution_scale",
                   "Auto Resolution Scale", leviabi::ConfigType::Toggle,
                   mConfig->config().autoResolutionScale ? "true" : "false");
    setConfigEntry(mSettings[mSettingCount++], "ui_mode", "UI Mode",
                   leviabi::ConfigType::Radio, uiMode,
                   "System,Dark,Light");
    setConfigEntry(mSettings[mSettingCount++], "max_tracks",
                   "Library Limit", leviabi::ConfigType::SliderInt,
                   limit, "1", "1024");
    setConfigEntry(mSettings[mSettingCount++], "loop_mode", "Loop Mode",
                   leviabi::ConfigType::Radio, loop, "Off,All,One");
    setConfigEntry(mSettings[mSettingCount++], "shuffle_enabled", "Shuffle",
                   leviabi::ConfigType::Toggle,
                   mConfig->config().shuffleEnabled ? "true" : "false");
    setConfigEntry(mSettings[mSettingCount++], "pause_playback",
                   "Pause Playback", leviabi::ConfigType::Button, "");
    setConfigEntry(mSettings[mSettingCount++], "stop_playback",
                   "Stop Playback", leviabi::ConfigType::Button, "");
}

void NativeMusicUi::shutdown() {
    stopAnimationThread();
    ScopedUiBusy guard(&mUiBusy, true);
    savePanelUiState();
    setSearchActive(false);
    mOpen = 0;
    mVisible = 0;
    mCapturedPointerMask = 0;
    mDragging = false;
    mDragPointerId = -1;
    mPressedControl = 0;
    if (mApi && mRegistered) {
        mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands, 0);
        mApi->unregisterModule(ModuleId, strlen(ModuleId));
    }
    mRegistered = false;
    if (gNativeMusicUi == this) gNativeMusicUi = nullptr;
}

bool NativeMusicUi::touchThunk(int action, int pointerId, float x, float y) {
    NativeMusicUi* self = gNativeMusicUi;
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0 ||
        __atomic_load_n(&self->mVisible, __ATOMIC_ACQUIRE) == 0) return false;

    const u32 pointerBit = 1u << (static_cast<u32>(pointerId) & 31u);
    const u32 capturedBefore = __atomic_load_n(
        &self->mCapturedPointerMask, __ATOMIC_ACQUIRE);
    const bool pointerCaptured = (capturedBefore & pointerBit) != 0;
    const bool inside = x >= self->mPanelX && y >= self->mPanelY &&
                        x <= self->mPanelX + self->mPanelW &&
                        y <= self->mPanelY + self->mPanelH;
    const leviabi::TouchEvent event{action, pointerId, x, y};
    const bool handled = self->onTouch(event);

    if ((action == 0 || action == 5) && (inside || handled)) {
        __atomic_fetch_or(&self->mCapturedPointerMask, pointerBit,
                          __ATOMIC_ACQ_REL);
    }
    const bool consume = inside || pointerCaptured || handled;
    if (action == 3) {
        __atomic_store_n(&self->mCapturedPointerMask, 0u,
                         __ATOMIC_RELEASE);
    } else if ((action == 1 || action == 6) && pointerCaptured) {
        __atomic_fetch_and(&self->mCapturedPointerMask, ~pointerBit,
                           __ATOMIC_ACQ_REL);
    }
    return consume;
}

bool NativeMusicUi::keyThunk(int keyCode, unsigned int unicodeChar,
                             bool isKeyDown) {
    NativeMusicUi* self = gNativeMusicUi;
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0)
        return false;
    return self->onKey(keyCode, unicodeChar, isKeyDown);
}

bool NativeMusicUi::textThunk(const char* text, usize size) {
    NativeMusicUi* self = gNativeMusicUi;
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0)
        return false;
    return self->onText(text, size);
}

void* NativeMusicUi::animationThreadThunk(void* pointer) {
    auto* self = static_cast<NativeMusicUi*>(pointer);
    if (!self) return nullptr;
    const TimeSpec frameDelay{0, 50000000L};
    while (__atomic_load_n(&self->mAnimationStop, __ATOMIC_ACQUIRE) == 0) {
        ++self->mAnimationFrame;
        if (self->mSwitchCooldownFrames > 0) --self->mSwitchCooldownFrames;

        if (++self->mLifecyclePollFrames >= 5) {
            self->mLifecyclePollFrames = 0;
            const bool minecraftAlive = jni::isMinecraftActivityAlive();
            if (minecraftAlive) {
                self->mMinecraftActivitySeen = true;
                self->mMinecraftExitHandled = false;
            } else if (self->mMinecraftActivitySeen &&
                       !self->mMinecraftExitHandled) {
                self->mMinecraftExitHandled = true;
                self->mPlayer->stop();
                self->mPlaybackWasRunning = false;
                self->mCompletionHandled = false;
                self->mCompletionIdleFrames = 0;
                self->mLastPlaybackPositionMs = 0;
                self->mLastPlaybackDurationMs = 0;
                __atomic_store_n(&self->mOpen, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&self->mVisible, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&self->mCapturedPointerMask, 0u,
                                 __ATOMIC_RELEASE);
                if (self->mApi && self->mRegistered) {
                    self->mApi->submitDrawCommands(ModuleId,
                                                   strlen(ModuleId),
                                                   self->mCommands, 0);
                }
                levi_log(LOG_INFO_VALUE, LogTag,
                                    "MinecraftActivity closed; playback stopped immediately");
            }
        }

        if (++self->mConfigPollFrames >= 10) {
            self->mConfigPollFrames = 0;
            ScopedUiBusy guard(&self->mUiBusy, false);
            if (guard.locked() && self->mConfig->pollForExternalChange()) {
                const LeviMusicConfig& config = self->mConfig->config();
                self->mPlayer->setVolume(config.volumePercent / 100.0f);
                // Looping is handled by the native playlist state machine.
                // Keeping Android MediaPlayer looping disabled guarantees that
                // completion is observable on every vendor implementation.
                self->mPlayer->setLooping(false);
                self->rescanLibraryPreservingState();
                self->mPanelPositionValid = false;
                if (__atomic_load_n(&self->mOpen,
                                    __ATOMIC_ACQUIRE) != 0)
                    self->renderUnlocked();
                levi_log(LOG_INFO_VALUE, LogTag,
                                    "config.json changed on disk; music directories hot reloaded");
            }
        }

        if (++self->mThemePollFrames >= 20) {
            self->mThemePollFrames = 0;
            if (self->mConfig->config().uiMode == 0) {
                bool detectedDark = self->mSystemDarkMode;
                if (jni::querySystemDarkMode(detectedDark) &&
                    detectedDark != self->mSystemDarkMode) {
                    self->mSystemDarkMode = detectedDark;
                    if (__atomic_load_n(&self->mOpen,
                                        __ATOMIC_ACQUIRE) != 0) {
                        ScopedUiBusy guard(&self->mUiBusy, false);
                        if (guard.locked()) self->renderUnlocked();
                    }
                }
            }
        }

        // Playback completion must be monitored even while the player window is
        // hidden. Previously this block ran only while the UI was open, so next,
        // Loop All and Loop One silently stopped working after the Mod Menu panel
        // was closed.
        {
            ScopedUiBusy guard(&self->mUiBusy, false);
            if (guard.locked()) {
                self->pollOnline();
                self->tickPlaybackUnlocked();
                if (__atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) != 0) {
                    if (self->mFilterDirty) self->rebuildVisibleIndex();
                    self->renderUnlocked();
                }
            }
        }
        nanosleep(&frameDelay, nullptr);
    }
    return nullptr;
}

void NativeMusicUi::startAnimationThread() {
    if (mAnimationStarted) return;
    __atomic_store_n(&mAnimationStop, 0, __ATOMIC_RELEASE);
    if (pthread_create(&mAnimationThread, nullptr,
                       &NativeMusicUi::animationThreadThunk, this) == 0)
        mAnimationStarted = true;
}

void NativeMusicUi::stopAnimationThread() {
    if (!mAnimationStarted) return;
    __atomic_store_n(&mAnimationStop, 1, __ATOMIC_RELEASE);
    pthread_join(mAnimationThread, nullptr);
    mAnimationStarted = false;
    mAnimationThread = 0;
}

void NativeMusicUi::handleModuleToggle(bool enabled) {
    levi_log(LOG_INFO_VALUE, LogTag, "module toggle -> %d: begin",
             enabled ? 1 : 0);
    setOpenInternal(enabled);
    levi_log(LOG_INFO_VALUE, LogTag, "module toggle -> %d: done",
             enabled ? 1 : 0);
}

void NativeMusicUi::forceDisabledDefault() {
    levi_log(LOG_INFO_VALUE, LogTag, "forceDisabledDefault: begin");
    setOpenInternal(false);
    if (mApi && mRegistered && mApi->setModuleEnabled) {
        levi_log(LOG_INFO_VALUE, LogTag, "forceDisabledDefault: setModuleEnabled(false)");
        mApi->setModuleEnabled(ModuleId, strlen(ModuleId), false);
    }
    levi_log(LOG_INFO_VALUE, LogTag, "forceDisabledDefault: done");
}

void NativeMusicUi::refreshViewport() {
    int width = 0;
    int height = 0;
    if (jni::queryDisplaySize(width, height) && width >= 320 && height >= 240) {
        mViewportWidth = width;
        mViewportHeight = height;
    } else {
        mViewportWidth = mConfig->config().uiWidth;
        mViewportHeight = mConfig->config().uiHeight;
        if (mViewportWidth < 320) mViewportWidth = 1280;
        if (mViewportHeight < 240) mViewportHeight = 720;
    }
}

void NativeMusicUi::restorePanelUiState() {
    if (!mConfig || !mLibrary) return;
    const LeviMusicConfig& config = mConfig->config();
    mSortMode = config.uiSortMode;
    mPage = config.uiPage;
    mSourceOffset = config.uiSourceOffset;
    mActiveTab = 0;
    if (config.uiActivePlaylist[0]) {
        for (int i = 0; i < mLibrary->sourceCount(); ++i) {
            const MusicSource* source = mLibrary->source(i);
            if (source && strcmp(source->path, config.uiActivePlaylist) == 0) {
                mActiveTab = i + 1;
                break;
            }
        }
    }
    ensureSourceVisible();
    mPanelPositionValid = false;
}

void NativeMusicUi::savePanelUiState() {
    if (!mConfig) return;
    char activePlaylist[512]{};
    if (mLibrary && mActiveTab > 0) {
        const MusicSource* source = mLibrary->source(mActiveTab - 1);
        if (source) copyString(activePlaylist, sizeof(activePlaylist),
                               source->path);
    }
    const LeviMusicConfig& previous = mConfig->config();
    const int x = mPanelPositionValid
        ? (mPanelX > 0.0f ? static_cast<int>(mPanelX + 0.5f) : 0)
        : previous.uiPanelX;
    const int y = mPanelPositionValid
        ? (mPanelY > 0.0f ? static_cast<int>(mPanelY + 0.5f) : 0)
        : previous.uiPanelY;
    const bool positionSaved = mPanelPositionValid
        ? true : previous.uiPanelPositionSaved;
    mConfig->savePanelUiState(x, y, positionSaved,
                              activePlaylist, mSortMode, mPage,
                              mSourceOffset);
}

void NativeMusicUi::setOpenInternal(bool open) {
    ScopedUiBusy guard(&mUiBusy, true);
    const bool wasOpen = mOpen != 0;
    mOpen = open ? 1 : 0;
    if (!open) {
        if (wasOpen || mPanelPositionValid) savePanelUiState();
        mVisible = 0;
        setSearchActive(false);
    }
    mDragging = false;
    mDragPointerId = -1;
    mPressedControl = 0;
    if (open) {
        refreshViewport();
        mPlayer->snapshot(mSnapshot);
        mPlaybackWasRunning = mSnapshot.playing;
        mCompletionHandled = false;
        mCompletionIdleFrames = 0;
        mLastPlaybackPositionMs = mSnapshot.positionMs;
        mLastPlaybackDurationMs = mSnapshot.durationMs;
        markFilterDirty();
        refreshUnlocked();
        renderUnlocked();
    } else if (mApi && mRegistered) {
        mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands, 0);
    }
}

bool NativeMusicUi::hit(float px, float py, float x, float y,
                        float w, float h) const {
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

void NativeMusicUi::calculateLayout(float& x, float& y, float& w, float& h,
                                    float& scale) {
    const float screenW = static_cast<float>(mViewportWidth);
    const float screenH = static_cast<float>(mViewportHeight);
    const float manualScale = clampFloat(
        mConfig->config().uiScalePercent / 100.0f, 0.40f, 1.50f);
    if (mConfig->config().autoResolutionScale) {
        float fitScale = minFloat((screenW - 8.0f) / BasePanelWidth,
                                  (screenH - 8.0f) / BasePanelHeight);
        fitScale = clampFloat(fitScale, 0.30f, 1.0f);
        scale = minFloat(manualScale, fitScale);
    } else {
        scale = manualScale;
    }
    scale = clampFloat(scale, 0.30f, 1.50f);
    w = BasePanelWidth * scale;
    h = BasePanelHeight * scale;
    if (!mPanelPositionValid) {
        if (mConfig->config().uiPanelPositionSaved) {
            mPanelX = static_cast<float>(mConfig->config().uiPanelX);
            mPanelY = static_cast<float>(mConfig->config().uiPanelY);
        } else {
            mPanelX = maxFloat(0.0f, (screenW - w) * 0.5f);
            mPanelY = maxFloat(0.0f, (screenH - h) * 0.5f);
        }
        mPanelPositionValid = true;
    }
    mPanelX = clampFloat(mPanelX, 0.0f,
                         maxFloat(0.0f, screenW - w));
    mPanelY = clampFloat(mPanelY, 0.0f,
                         maxFloat(0.0f, screenH - h));
    mPanelW = w;
    mPanelH = h;
    x = mPanelX;
    y = mPanelY;
}

int NativeMusicUi::currentSourceFilter() const {
    return mActiveTab <= 0 ? -1 : mActiveTab - 1;
}

bool NativeMusicUi::trackMatches(int globalIndex) const {
    const MusicTrack* track = mLibrary ? mLibrary->track(globalIndex) : nullptr;
    if (!track) return false;
    const int source = currentSourceFilter();
    if (source >= 0 && track->sourceIndex != source) return false;
    if (!mSearchText[0]) return true;
    return containsIgnoreCase(track->title, mSearchText) ||
           containsIgnoreCase(track->artist, mSearchText) ||
           containsIgnoreCase(track->name, mSearchText) ||
           containsIgnoreCase(track->path, mSearchText) ||
           containsIgnoreCase(track->format, mSearchText);
}

int NativeMusicUi::compareTracks(int leftGlobal, int rightGlobal) const {
    const MusicTrack* left = mLibrary ? mLibrary->track(leftGlobal) : nullptr;
    const MusicTrack* right = mLibrary ? mLibrary->track(rightGlobal) : nullptr;
    if (!left || !right) return left ? -1 : (right ? 1 : 0);
    if (mSortMode == 2) {
        int artist = compareIgnoreCase(left->artist, right->artist);
        if (artist != 0) return artist;
    }
    const char* leftTitle = left->title[0] ? left->title : left->name;
    const char* rightTitle = right->title[0] ? right->title : right->name;
    return compareIgnoreCase(leftTitle, rightTitle);
}

void NativeMusicUi::markFilterDirty() { mFilterDirty = true; }

void NativeMusicUi::rebuildVisibleIndex() {
    mVisibleCount = 0;
    if (!mLibrary) {
        mFilterDirty = false;
        return;
    }
    for (int global = 0; global < mLibrary->count(); ++global) {
        if (!trackMatches(global)) continue;
        int insert = mVisibleCount;
        if (mSortMode != 0) {
            while (insert > 0 &&
                   compareTracks(global, mVisibleIndices[insert - 1]) < 0) {
                mVisibleIndices[insert] = mVisibleIndices[insert - 1];
                --insert;
            }
        }
        mVisibleIndices[insert] = global;
        ++mVisibleCount;
    }
    const int pages = (mVisibleCount + RowsPerPage - 1) / RowsPerPage;
    if (pages <= 0) mPage = 0;
    else if (mPage >= pages) mPage = pages - 1;
    mFilterDirty = false;
}

int NativeMusicUi::visibleTrackCount() const { return mVisibleCount; }

int NativeMusicUi::selectedGlobalIndex() const {
    if (!mLibrary || !mConfig || !mConfig->config().selectedAudio[0])
        return -1;
    for (int i = 0; i < mLibrary->count(); ++i) {
        const MusicTrack* track = mLibrary->track(i);
        if (track &&
            strcmp(track->path, mConfig->config().selectedAudio) == 0)
            return i;
    }
    return -1;
}

int NativeMusicUi::globalIndexForRow(int row) const {
    const int visible = mPage * RowsPerPage + row;
    return visible >= 0 && visible < mVisibleCount
        ? mVisibleIndices[visible] : -1;
}

int NativeMusicUi::visibleIndexOfGlobal(int globalIndex) const {
    if (globalIndex < 0) return -1;
    for (int i = 0; i < mVisibleCount; ++i)
        if (mVisibleIndices[i] == globalIndex) return i;
    return -1;
}

void NativeMusicUi::ensureSourceVisible() {
    const int total = mLibrary ? mLibrary->sourceCount() + 1 : 1;
    if (mActiveTab < 0) mActiveTab = 0;
    if (mActiveTab >= total) mActiveTab = total - 1;
    const int activeSource = mActiveTab - 1;
    if (activeSource >= 0) {
        if (activeSource < mSourceOffset) mSourceOffset = activeSource;
        if (activeSource >= mSourceOffset + VisibleSources)
            mSourceOffset = activeSource - VisibleSources + 1;
    }
    const int sourceCount = total - 1;
    const int maxOffset = sourceCount > VisibleSources
        ? sourceCount - VisibleSources : 0;
    if (mSourceOffset > maxOffset) mSourceOffset = maxOffset;
    if (mSourceOffset < 0) mSourceOffset = 0;
}

int NativeMusicUi::classifyControl(float x, float y, float panelX,
                                   float panelY, float panelW, float panelH,
                                   float scale) const {
    const float sidebarX = panelX + 8.0f * scale;
    const float sidebarY = panelY + 8.0f * scale;
    const float sidebarW = 230.0f * scale;
    const float mainX = panelX + 246.0f * scale;
    const float mainY = panelY + 8.0f * scale;
    const float mainW = panelW - 254.0f * scale;
    const float bottomY = panelY + 656.0f * scale;

    if (hit(x, y, sidebarX + 12.0f * scale,
            sidebarY + 70.0f * scale,
            sidebarW - 24.0f * scale, 44.0f * scale))
        return ControlAllSource;

    const float sourceY = sidebarY + 154.0f * scale;
    for (int slot = 0; slot < VisibleSources; ++slot) {
        const int sourceIndex = mSourceOffset + slot;
        if (!mLibrary || sourceIndex >= mLibrary->sourceCount()) break;
        if (hit(x, y, sidebarX + 12.0f * scale,
                sourceY + slot * 52.0f * scale,
                sidebarW - 24.0f * scale, 46.0f * scale))
            return ControlSourceBase + slot;
    }
    if (hit(x, y, sidebarX + 12.0f * scale,
            sidebarY + 580.0f * scale, 94.0f * scale, 38.0f * scale))
        return ControlSourcePrev;
    if (hit(x, y, sidebarX + 124.0f * scale,
            sidebarY + 580.0f * scale, 94.0f * scale, 38.0f * scale))
        return ControlSourceNext;

    const float searchX = mainX + 20.0f * scale;
    const float searchY = mainY + 82.0f * scale;
    const float searchW = mainW - 214.0f * scale;
    if (mSearchText[0] &&
        hit(x, y, searchX + searchW - 42.0f * scale, searchY,
            42.0f * scale, 44.0f * scale))
        return ControlClearSearch;
    if (hit(x, y, searchX, searchY, searchW, 44.0f * scale))
        return ControlSearch;
    if (hit(x, y, mainX + mainW - 176.0f * scale, searchY,
            156.0f * scale, 44.0f * scale))
        return ControlSort;

    const float listX = mainX + 20.0f * scale;
    const float listY = mainY + 170.0f * scale;
    const float rowW = mainW - 40.0f * scale;
    for (int row = 0; row < RowsPerPage; ++row) {
        if (hit(x, y, listX,
                listY + row * 56.0f * scale,
                rowW, 52.0f * scale))
            return ControlRowBase + row;
    }

    const float footerY = mainY + 590.0f * scale;
    if (hit(x, y, mainX + 20.0f * scale, footerY,
            102.0f * scale, 36.0f * scale)) return ControlRescan;
    if (hit(x, y, mainX + mainW - 174.0f * scale, footerY,
            72.0f * scale, 36.0f * scale)) return ControlPagePrev;
    if (hit(x, y, mainX + mainW - 92.0f * scale, footerY,
            72.0f * scale, 36.0f * scale)) return ControlPageNext;

    const float centerX = panelX + panelW * 0.5f;
    const float controlsY = bottomY + 32.0f * scale;
    const float offsets[] = {-122.0f, -62.0f, 0.0f, 62.0f, 122.0f, 178.0f};
    const int ids[] = {ControlShuffle, ControlTrackPrev, ControlPlayPause,
                       ControlTrackNext, ControlLoop, ControlStop};
    const float radii[] = {22.0f, 22.0f, 30.0f, 22.0f, 22.0f, 20.0f};
    for (int i = 0; i < 6; ++i) {
        if (hit(x, y,
                centerX + (offsets[i] - radii[i]) * scale,
                controlsY - radii[i] * scale,
                radii[i] * 2.0f * scale,
                radii[i] * 2.0f * scale)) return ids[i];
    }

    if (hit(x, y, centerX - 250.0f * scale,
            bottomY + 64.0f * scale,
            500.0f * scale, 28.0f * scale)) return ControlSeek;

    const float right = panelX + panelW - 16.0f * scale;
    if (hit(x, y, right - 224.0f * scale,
            bottomY + 44.0f * scale,
            30.0f * scale, 30.0f * scale)) return ControlMute;
    if (hit(x, y, right - 186.0f * scale,
            bottomY + 45.0f * scale,
            170.0f * scale, 28.0f * scale)) return ControlVolumeBar;
    return 0;
}

void NativeMusicUi::seekFromPointer(float x, float panelX, float panelY,
                                    float scale) {
    (void)panelY;
    if (!mPlayer || !mPlayer->hasMedia()) return;
    const float begin = panelX + BasePanelWidth * 0.5f * scale -
                        250.0f * scale;
    const float width = 500.0f * scale;
    const float fraction = clampFloat((x - begin) / width, 0.0f, 1.0f);
    const int duration = mPlayer->durationMs();
    if (duration > 0)
        mPlayer->seekToMs(static_cast<int>(duration * fraction));
}

void NativeMusicUi::volumeFromPointer(float x, float panelX, float panelY,
                                      float scale) {
    (void)panelY;
    const float right = panelX + BasePanelWidth * scale - 16.0f * scale;
    const float begin = right - 186.0f * scale;
    const float width = 170.0f * scale;
    const float fraction = clampFloat((x - begin) / width, 0.0f, 1.0f);
    const int percent = static_cast<int>(fraction * 100.0f + 0.5f);
    if (percent > 0) mVolumeBeforeMute = percent;
    mConfig->setVolumePercent(percent);
    mPlayer->setVolume(fraction);
}

int NativeMusicUi::randomVisibleIndex(int count, int current) {
    if (count <= 1) return count > 0 ? 0 : -1;
    mShuffleSeed = mShuffleSeed * 1664525u + 1013904223u +
                   static_cast<u32>(mSnapshot.positionMs + current * 97);
    int candidate = static_cast<int>(mShuffleSeed %
                                     static_cast<u32>(count));
    if (candidate == current) candidate = (candidate + 1) % count;
    return candidate;
}

void NativeMusicUi::selectTrackInternal(int globalIndex, bool autoplay,
                                        bool renderAfter,
                                        int playbackSourceFilter) {
    MusicTrack* track = mLibrary ? mLibrary->trackMutable(globalIndex) : nullptr;
    if (!track) return;
    if (mSwitchCooldownFrames > 0 && autoplay &&
        mPlayer->hasMedia() &&
        strcmp(mPlayer->currentPath(), track->path) == 0) return;

    mSwitchCooldownFrames = 2;
    mConfig->saveSelected(track->path);
    mMetadata->ensure(*track);
    if (autoplay) {
        if (playbackSourceFilter == -999)
            playbackSourceFilter = currentSourceFilter();
        if (playbackSourceFilter >= mLibrary->sourceCount())
            playbackSourceFilter = -1;
        mPlaybackSourceFilter = playbackSourceFilter;
        mPlaybackSourcePath[0] = 0;
        if (mPlaybackSourceFilter >= 0) {
            const MusicSource* playbackSource =
                mLibrary->source(mPlaybackSourceFilter);
            if (playbackSource)
                copyString(mPlaybackSourcePath, sizeof(mPlaybackSourcePath),
                           playbackSource->path);
        }
        // Native completion handling implements Off / Loop All / Loop One.
        // Android MediaPlayer looping is deliberately disabled because several
        // vendor implementations do not report a reliable looping/completion
        // state after rapid track replacement.
        mPlayer->setLooping(false);
        const bool started = startPlayback(track->path);
        mPlaybackWasRunning = started;
        mCompletionHandled = !started;
        mCompletionIdleFrames = 0;
        mLastPlaybackPositionMs = 0;
        mLastPlaybackDurationMs = 0;
        mPlayer->snapshot(mSnapshot);
        if (started) {
            mLastPlaybackPositionMs = mSnapshot.positionMs;
            mLastPlaybackDurationMs = mSnapshot.durationMs;
        }
    }
    const int visible = visibleIndexOfGlobal(globalIndex);
    if (visible >= 0) mPage = visible / RowsPerPage;
    if (renderAfter) renderUnlocked();
}

bool NativeMusicUi::startPlayback(const char* path) {
    if (isOnlinePath(path)) {
        if (!mConfig->config().onlineEnabled) return false;
        if (online::isLoading(path)) return true;   // already on its way
        // Network + download happen on the online worker; the player is stopped
        // now so the previous song does not keep playing while this one loads.
        mPlayer->stop();
        online::requestPlay(path);
        return true;
    }
    online::cancel();
    return mPlayer->play(path);
}

bool NativeMusicUi::shouldSearchOnline() {
    if (!mConfig->config().onlineEnabled || !mSearchText[0]) return false;
    if (strncmp(mSearchText, "http://", 7) == 0 ||
        strncmp(mSearchText, "https://", 8) == 0) return true;
    if (mFilterDirty) rebuildVisibleIndex();
    const int onlineSource = mLibrary->onlineSourceIndex();
    if (onlineSource >= 0 && mActiveTab == onlineSource + 1) return true;
    return mVisibleCount == 0;   // nothing local matches: look online
}

void NativeMusicUi::pollOnline() {
    static OnlineEntry entries[MusicLibrary::MaxOnlineEntries];
    int count = 0;
    if (!online::takeResults(entries, MusicLibrary::MaxOnlineEntries, &count)) return;
    mLibrary->setOnlineEntries(entries, count);
    const int source = mLibrary->onlineSourceIndex();
    if (source >= 0) {
        mActiveTab = source + 1;
        ensureSourceVisible();
    } else if (mActiveTab > mLibrary->sourceCount()) {
        mActiveTab = 0;
    }
    mPage = 0;
    markFilterDirty();
    rebuildVisibleIndex();
}

void NativeMusicUi::advanceTrack(int direction, bool fromCompletion) {
    int sourceFilter = -1;
    if (mSnapshot.hasMedia) {
        sourceFilter = mPlaybackSourceFilter;
        if (sourceFilter >= 0 && mPlaybackSourcePath[0]) {
            const int remapped = mLibrary->sourceIndexForPath(mPlaybackSourcePath);
            if (remapped >= 0) {
                sourceFilter = remapped;
                mPlaybackSourceFilter = remapped;
            }
        }
    } else {
        sourceFilter = currentSourceFilter();
    }
    if (sourceFilter >= mLibrary->sourceCount()) sourceFilter = -1;

    const int count = mLibrary ? mLibrary->filteredCount(sourceFilter) : 0;
    if (count <= 0) return;

    // Resolve the actually playing file rather than the UI selection. This keeps
    // completion/loop behavior stable after searches, tab changes, config hot
    // reloads, and source-index remaps.
    int currentGlobal = -1;
    if (mSnapshot.path[0])
        currentGlobal = mLibrary->globalIndexForPath(mSnapshot.path);
    if (currentGlobal < 0) currentGlobal = selectedGlobalIndex();
    int current = mLibrary->filteredIndexOfGlobal(sourceFilter, currentGlobal);

    // Loop One always reopens the exact playing file. If the library was rescanned
    // while it was playing, fall back to the current path so the loop still works.
    if (fromCompletion && mConfig->config().loopMode == 2) {
        if (currentGlobal >= 0) {
            selectTrackInternal(currentGlobal, true, false, sourceFilter);
        } else if (mSnapshot.path[0]) {
            const bool started = startPlayback(mSnapshot.path);
            mPlaybackWasRunning = started;
            mCompletionHandled = !started;
            mCompletionIdleFrames = 0;
            mLastPlaybackPositionMs = 0;
            mLastPlaybackDurationMs = 0;
        }
        return;
    }

    int next = 0;
    if (mConfig->config().shuffleEnabled) {
        next = randomVisibleIndex(count, current < 0 ? 0 : current);
    } else if (current < 0) {
        next = direction < 0 ? count - 1 : 0;
    } else {
        next = current + direction;
        if (next < 0) next = count - 1;
        if (next >= count) {
            // At playlist end, Loop All wraps. Loop Off stops.
            if (fromCompletion && mConfig->config().loopMode == 0) return;
            next = 0;
        }
    }

    if (fromCompletion && count == 1 &&
        mConfig->config().loopMode == 0) return;

    const int nextGlobal = mLibrary->filteredGlobalIndex(sourceFilter, next);
    if (nextGlobal >= 0)
        selectTrackInternal(nextGlobal, true, false, sourceFilter);
}

void NativeMusicUi::setSearchActive(bool active) {
    mSearchActive = active;
    if (!mApi || !mApi->input) return;
    if (active) {
        if (mApi->input->showKeyboard) mApi->input->showKeyboard();
    } else {
        if (mApi->input->hideKeyboard) mApi->input->hideKeyboard();
    }
}

void NativeMusicUi::clearSearch() {
    mSearchText[0] = 0;
    mPage = 0;
    markFilterDirty();
    rebuildVisibleIndex();
}

void NativeMusicUi::eraseLastSearchGlyph() {
    usize length = strlen(mSearchText);
    if (length == 0) return;
    --length;
    while (length > 0 &&
           (static_cast<u8>(mSearchText[length]) & 0xC0u) == 0x80u)
        --length;
    mSearchText[length] = 0;
    mPage = 0;
    markFilterDirty();
    rebuildVisibleIndex();
}

void NativeMusicUi::appendSearchText(const char* text, usize size) {
    if (!text || size == 0) return;
    usize at = strlen(mSearchText);
    for (usize i = 0; i < size && at + 1 < sizeof(mSearchText); ++i) {
        const unsigned char value = static_cast<unsigned char>(text[i]);
        if (value == '\r' || value == '\n' || value == '\t') continue;
        if (value < 0x20u && value != ' ') continue;
        mSearchText[at++] = static_cast<char>(value);
    }
    mSearchText[at] = 0;
    mPage = 0;
    markFilterDirty();
    rebuildVisibleIndex();
}

bool NativeMusicUi::onKey(int keyCode, unsigned int, bool isKeyDown) {
    if (!mSearchActive) return false;
    if (!isKeyDown) return true;
    ScopedUiBusy guard(&mUiBusy, true);
    if (keyCode == 67) {
        eraseLastSearchGlyph();
    } else if (keyCode == 66) {
        if (shouldSearchOnline()) {
            online::search(mSearchText);
            clearSearch();
        }
        setSearchActive(false);
    } else if (keyCode == 4 || keyCode == 111) {
        setSearchActive(false);
    }
    renderUnlocked();
    return true;
}

bool NativeMusicUi::onText(const char* text, usize size) {
    if (!mSearchActive) return false;
    ScopedUiBusy guard(&mUiBusy, true);
    appendSearchText(text, size);
    renderUnlocked();
    return true;
}

void NativeMusicUi::activateControl(int control) {
    if (control == ControlRescan) {
        refreshUnlocked();
        return;
    }
    if (control == ControlPagePrev) {
        if (mPage > 0) --mPage;
    } else if (control == ControlPageNext) {
        const int pages = (mVisibleCount + RowsPerPage - 1) / RowsPerPage;
        if (mPage + 1 < pages) ++mPage;
    } else if (control == ControlTrackPrev) {
        advanceTrack(-1, false);
    } else if (control == ControlTrackNext) {
        advanceTrack(1, false);
    } else if (control == ControlShuffle) {
        mConfig->setShuffleEnabled(!mConfig->config().shuffleEnabled);
    } else if (control == ControlLoop) {
        mConfig->setLoopMode((mConfig->config().loopMode + 1) % 3);
        mPlayer->setLooping(false);
    } else if (control == ControlPlayPause) {
        int selected = selectedGlobalIndex();
        if (visibleIndexOfGlobal(selected) < 0 && mVisibleCount > 0)
            selected = mVisibleIndices[0];
        const MusicTrack* track = mLibrary->track(selected);
        if (track) {
            if (mPlayer->hasMedia() &&
                strcmp(mPlayer->currentPath(), track->path) == 0) {
                if (mPlayer->isPlaying()) mPlayer->pause();
                else if (mPlayer->paused()) mPlayer->resume();
                else selectTrackInternal(selected, true, false,
                                         currentSourceFilter());
            } else {
                selectTrackInternal(selected, true, false,
                                    currentSourceFilter());
            }
        } else if (mVisibleCount > 0) {
            selectTrackInternal(mVisibleIndices[0], true, false,
                                currentSourceFilter());
        }
    } else if (control == ControlStop) {
        online::cancel();
        mPlayer->stop();
        mPlaybackWasRunning = false;
        mCompletionHandled = false;
        mCompletionIdleFrames = 0;
        mLastPlaybackPositionMs = 0;
        mLastPlaybackDurationMs = 0;
    } else if (control == ControlSearch) {
        setSearchActive(true);
    } else if (control == ControlClearSearch) {
        clearSearch();
        setSearchActive(true);
    } else if (control == ControlSort) {
        mSortMode = (mSortMode + 1) % 3;
        mPage = 0;
        markFilterDirty();
        rebuildVisibleIndex();
    } else if (control == ControlMute) {
        const int current = mConfig->config().volumePercent;
        if (current > 0) {
            mVolumeBeforeMute = current;
            mConfig->setVolumePercent(0);
            mPlayer->setVolume(0.0f);
        } else {
            const int restored = mVolumeBeforeMute > 0
                ? mVolumeBeforeMute : 80;
            mConfig->setVolumePercent(restored);
            mPlayer->setVolume(restored / 100.0f);
        }
    } else if (control == ControlAllSource) {
        mActiveTab = 0;
        mPage = 0;
        markFilterDirty();
        rebuildVisibleIndex();
    } else if (control == ControlSourcePrev) {
        if (mSourceOffset > 0) --mSourceOffset;
    } else if (control == ControlSourceNext) {
        const int maxOffset = mLibrary->sourceCount() > VisibleSources
            ? mLibrary->sourceCount() - VisibleSources : 0;
        if (mSourceOffset < maxOffset) ++mSourceOffset;
    } else if (control >= ControlSourceBase &&
               control < ControlSourceBase + VisibleSources) {
        const int sourceIndex = mSourceOffset +
                                (control - ControlSourceBase);
        if (sourceIndex < mLibrary->sourceCount()) {
            mActiveTab = sourceIndex + 1;
            mPage = 0;
            markFilterDirty();
            rebuildVisibleIndex();
            ensureSourceVisible();
        }
    } else if (control >= ControlRowBase &&
               control < ControlRowBase + RowsPerPage) {
        selectTrackInternal(globalIndexForRow(control - ControlRowBase),
                            true, false, currentSourceFilter());
    }
    // Navigation/panel state is auto-saved alongside config changes. Seek and
    // volume drags use their dedicated setters and avoid extra disk writes.
    savePanelUiState();
    renderUnlocked();
}

bool NativeMusicUi::onTouch(const leviabi::TouchEvent& event) {
    ScopedUiBusy guard(&mUiBusy, true);
    if (!mOpen) return false;
    float panelX{}, panelY{}, panelW{}, panelH{}, scale{};
    calculateLayout(panelX, panelY, panelW, panelH, scale);
    const bool inside = hit(event.x, event.y, panelX, panelY,
                            panelW, panelH);

    if (event.action == 0 || event.action == 5) {
        if (!inside) return false;
        mPressedControl = classifyControl(event.x, event.y, panelX,
                                          panelY, panelW, panelH, scale);
        if (mPressedControl != 0) {
            renderUnlocked();
            return true;
        }
        const bool dragRegion = event.y <= panelY + 72.0f * scale;
        if (dragRegion) {
            mDragging = true;
            mDragPointerId = event.pointerId;
            mDragOffsetX = event.x - panelX;
            mDragOffsetY = event.y - panelY;
        }
        return true;
    }

    if (event.action == 2) {
        if (mDragging && event.pointerId == mDragPointerId) {
            mPanelX = event.x - mDragOffsetX;
            mPanelY = event.y - mDragOffsetY;
            calculateLayout(panelX, panelY, panelW, panelH, scale);
            renderUnlocked();
            return true;
        }
        if (mPressedControl == ControlSeek) {
            seekFromPointer(event.x, panelX, panelY, scale);
            renderUnlocked();
            return true;
        }
        if (mPressedControl == ControlVolumeBar) {
            volumeFromPointer(event.x, panelX, panelY, scale);
            renderUnlocked();
            return true;
        }
        return inside || mPressedControl != 0;
    }

    if (event.action == 1 || event.action == 6) {
        if (mDragging && event.pointerId == mDragPointerId) {
            mDragging = false;
            mDragPointerId = -1;
            savePanelUiState();
            renderUnlocked();
            return true;
        }
        const int pressed = mPressedControl;
        const int released = classifyControl(event.x, event.y, panelX,
                                             panelY, panelW, panelH, scale);
        mPressedControl = 0;
        if (pressed == ControlSeek) {
            seekFromPointer(event.x, panelX, panelY, scale);
            renderUnlocked();
            return true;
        }
        if (pressed == ControlVolumeBar) {
            volumeFromPointer(event.x, panelX, panelY, scale);
            renderUnlocked();
            return true;
        }
        if (pressed != 0 && pressed == released) {
            activateControl(pressed);
            return true;
        }
        renderUnlocked();
        return inside || pressed != 0;
    }

    if (event.action == 3) {
        mDragging = false;
        mDragPointerId = -1;
        mPressedControl = 0;
        savePanelUiState();
        renderUnlocked();
        return true;
    }
    return inside;
}

void NativeMusicUi::rescanLibraryPreservingState() {
    char activeSourcePath[512]{};
    char playbackSourcePath[512]{};
    if (mActiveTab > 0) {
        const MusicSource* source = mLibrary->source(mActiveTab - 1);
        if (source) copyString(activeSourcePath, sizeof(activeSourcePath),
                               source->path);
    }
    if (mPlaybackSourceFilter >= 0) {
        const MusicSource* source = mLibrary->source(mPlaybackSourceFilter);
        if (source) copyString(playbackSourcePath,
                               sizeof(playbackSourcePath), source->path);
    }

    mLibrary->scan(mConfig->config());

    mActiveTab = 0;
    if (activeSourcePath[0]) {
        for (int i = 0; i < mLibrary->sourceCount(); ++i) {
            const MusicSource* source = mLibrary->source(i);
            if (source && strcmp(source->path, activeSourcePath) == 0) {
                mActiveTab = i + 1;
                break;
            }
        }
    }

    if (mPlaybackSourceFilter >= 0) {
        mPlaybackSourceFilter = -1;
        if (playbackSourcePath[0]) {
            for (int i = 0; i < mLibrary->sourceCount(); ++i) {
                const MusicSource* source = mLibrary->source(i);
                if (source && strcmp(source->path,
                                     playbackSourcePath) == 0) {
                    mPlaybackSourceFilter = i;
                    break;
                }
            }
        }
        if (mPlaybackSourceFilter < 0) {
            const int selected = selectedGlobalIndex();
            const MusicTrack* track = mLibrary->track(selected);
            if (track) mPlaybackSourceFilter = track->sourceIndex;
        }
    }

    ensureSourceVisible();
    markFilterDirty();
    rebuildVisibleIndex();
    const int pages = (mVisibleCount + RowsPerPage - 1) / RowsPerPage;
    if (pages <= 0) mPage = 0;
    else if (mPage >= pages) mPage = pages - 1;
}

void NativeMusicUi::refreshUnlocked() {
    rescanLibraryPreservingState();
    renderUnlocked();
}

void NativeMusicUi::refresh() {
    ScopedUiBusy guard(&mUiBusy, true);
    refreshUnlocked();
}

void NativeMusicUi::redraw() {
    ScopedUiBusy guard(&mUiBusy, true);
    renderUnlocked();
}

void NativeMusicUi::tickPlaybackUnlocked() {
    mPlayer->snapshot(mSnapshot);
    const bool uiOpen = __atomic_load_n(&mOpen, __ATOMIC_ACQUIRE) != 0;
    if (uiOpen) mPlayer->captureSpectrum(mSpectrum, 24);

    if (mSnapshot.playing) {
        mPlaybackWasRunning = true;
        mCompletionHandled = false;
        mCompletionIdleFrames = 0;
        mLastPlaybackPositionMs = mSnapshot.positionMs;
        mLastPlaybackDurationMs = mSnapshot.durationMs;
        return;
    }

    if (!mSnapshot.hasMedia) {
        mPlaybackWasRunning = false;
        mCompletionHandled = false;
        mCompletionIdleFrames = 0;
        mLastPlaybackPositionMs = 0;
        mLastPlaybackDurationMs = 0;
        return;
    }

    // A paused track is intentional and must never be interpreted as complete.
    if (mSnapshot.paused || !mPlaybackWasRunning || mCompletionHandled) {
        return;
    }

    // Android/vendor MediaPlayer implementations disagree about the final
    // position after EOS. Some report duration, some reset to zero, and some keep
    // a stale position well before the end. Once this player has definitely been
    // observed running, any sustained non-playing state that is not an explicit
    // pause is therefore treated as completion. Strong end-of-stream evidence gets
    // a short debounce; otherwise use a longer debounce to reject transient
    // isPlaying() glitches.
    const int currentDuration = mSnapshot.durationMs > 0
        ? mSnapshot.durationMs : mLastPlaybackDurationMs;
    const int currentNearEndAt = currentDuration > 2000
        ? currentDuration - 1200 : (currentDuration * 3) / 4;
    const int previousNearEndAt = mLastPlaybackDurationMs > 2000
        ? mLastPlaybackDurationMs - 1600
        : (mLastPlaybackDurationMs * 3) / 4;
    const bool currentNearEnd = currentDuration > 0 &&
        mSnapshot.positionMs >= currentNearEndAt;
    const bool previousNearEnd = mLastPlaybackDurationMs > 0 &&
        mLastPlaybackPositionMs >= previousNearEndAt;
    const bool resetAfterProgress = mLastPlaybackPositionMs > 1500 &&
        mSnapshot.positionMs + 1000 < mLastPlaybackPositionMs;
    const bool strongCompletionEvidence =
        currentNearEnd || previousNearEnd || resetAfterProgress;

    ++mCompletionIdleFrames;
    const int requiredIdleFrames = strongCompletionEvidence ? 2 : 8;
    if (mCompletionIdleFrames < requiredIdleFrames) return;

    // Mark handled before advancing so a failed restart cannot spin every frame.
    // selectTrackInternal() clears this flag again whenever the next/looped track
    // successfully starts.
    mCompletionHandled = true;
    mCompletionIdleFrames = 0;
    advanceTrack(1, true);
    mPlayer->snapshot(mSnapshot);
}

void NativeMusicUi::formatTime(int ms, char* out, usize capacity) const {
    if (!out || capacity == 0) return;
    if (ms < 0) ms = 0;
    const int seconds = ms / 1000;
    snprintf(out, capacity, "%d:%02d", seconds / 60, seconds % 60);
}

u32 NativeMusicUi::trackAccent(const char* text) const {
    u32 hash = 2166136261u;
    if (text) {
        for (int i = 0; text[i]; ++i) {
            hash ^= static_cast<u8>(text[i]);
            hash *= 16777619u;
        }
    }
    static constexpr u32 colors[] = {
        0xFF1DB954u, 0xFF2D9CDBu, 0xFF9B51E0u,
        0xFFF2994Au, 0xFFEB5757u, 0xFF56CCF2u
    };
    return colors[hash % 6u];
}

bool NativeMusicUi::useDarkTheme() const {
    if (!mConfig) return true;
    const int mode = mConfig->config().uiMode;
    if (mode == 1) return true;
    if (mode == 2) return false;
    return mSystemDarkMode;
}

u32 NativeMusicUi::applyUiOpacity(u32 color) const {
    int percent = mConfig ? mConfig->config().uiOpacityPercent : 100;
    if (percent < 20) percent = 20;
    if (percent > 100) percent = 100;
    const u32 alpha = (color >> 24) & 0xFFu;
    const u32 scaled = (alpha * static_cast<u32>(percent) + 50u) / 100u;
    return (color & 0x00FFFFFFu) | (scaled << 24);
}

void NativeMusicUi::addRect(float x, float y, float w, float h,
                            u32 color, float radius) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::RectFilled;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.x3 = radius;
    c.color = applyUiOpacity(color);
}

void NativeMusicUi::addCircle(float x, float y, float radius, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::CircleFilled;
    c.x = x;
    c.y = y;
    c.size = radius;
    c.color = applyUiOpacity(color);
}

void NativeMusicUi::addTriangle(float x1, float y1, float x2, float y2,
                                float x3, float y3, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::TriangleFilled;
    c.x = x1;
    c.y = y1;
    c.w = x2;
    c.h = y2;
    c.x3 = x3;
    c.y3 = y3;
    c.color = applyUiOpacity(color);
}

void NativeMusicUi::addLine(float x1, float y1, float x2, float y2,
                            float width, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::Line;
    c.x = x1;
    c.y = y1;
    c.w = x2 - x1;
    c.h = y2 - y1;
    c.size = width;
    c.color = applyUiOpacity(color);
}

void NativeMusicUi::addText(float x, float y, float size,
                            const char* text, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::Text;
    c.x = x;
    c.y = y + size * 0.82f;
    c.size = size;
    c.color = applyUiOpacity(color);
    leviabi::setShortString(c.text, text);
}

void NativeMusicUi::addTextCentered(float x, float y, float w, float h,
                                    float size, const char* text, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::Text;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.size = size;
    c.color = applyUiOpacity(color);
    leviabi::setShortString(c.text, text);
}

void NativeMusicUi::addTextRight(float rightX, float y, float size,
                                 const char* text, u32 color) {
    if (mCommandCount >= 768) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::Text;
    c.x = rightX;
    c.y = y + size * 0.82f;
    c.w = -1.0f;
    c.size = size;
    c.color = applyUiOpacity(color);
    leviabi::setShortString(c.text, text);
}

void NativeMusicUi::addTextSegment(float x, float y, float size,
                                   const char* text, int startGlyph,
                                   int maxBytes, u32 color) {
    char segment[23]{};
    if (maxBytes > 22) maxBytes = 22;
    if (text && startGlyph >= 0) {
        usize cursor = 0;
        int glyph = 0;
        while (text[cursor] && glyph < startGlyph) {
            const u8 lead = static_cast<u8>(text[cursor]);
            usize width = (lead & 0x80u) == 0 ? 1 :
                          ((lead & 0xE0u) == 0xC0u ? 2 :
                          ((lead & 0xF0u) == 0xE0u ? 3 : 4));
            cursor += width;
            ++glyph;
        }
        int out = 0;
        while (text[cursor] && out < maxBytes) {
            const u8 lead = static_cast<u8>(text[cursor]);
            int width = (lead & 0x80u) == 0 ? 1 :
                        ((lead & 0xE0u) == 0xC0u ? 2 :
                        ((lead & 0xF0u) == 0xE0u ? 3 : 4));
            if (out + width > maxBytes) break;
            for (int i = 0; i < width && text[cursor + i]; ++i)
                segment[out++] = text[cursor + i];
            cursor += width;
        }
        segment[out] = 0;
    }
    addText(x, y, size, segment, color);
}


float NativeMusicUi::estimateTextWidth(const char* text, float size) const {
    if (!text || !*text || size <= 0.0f) return 0.0f;
    float width = 0.0f;
    usize cursor = 0;
    while (text[cursor]) {
        const u8 lead = static_cast<u8>(text[cursor]);
        if ((lead & 0x80u) != 0) {
            usize bytes = (lead & 0xE0u) == 0xC0u ? 2 :
                          ((lead & 0xF0u) == 0xE0u ? 3 : 4);
            width += size * 0.92f;
            cursor += bytes;
            continue;
        }
        const char c = text[cursor++];
        float factor = 0.55f;
        if (c == ' ') factor = 0.30f;
        else if (c == 'i' || c == 'l' || c == 'I' || c == '!' ||
                 c == '|' || c == '.' || c == ',' || c == ':' ||
                 c == ';' || c == '\'' || c == '`') factor = 0.27f;
        else if (c == 'm' || c == 'w' || c == 'M' || c == 'W' ||
                 c == '@' || c == '#' || c == '%' || c == '&')
            factor = 0.82f;
        else if (c >= 'A' && c <= 'Z') factor = 0.61f;
        width += size * factor;
    }
    return width;
}

void NativeMusicUi::addTextFlow(float x, float y, float size,
                                const char* text, float maxWidth,
                                u32 color) {
    if (!text || !*text || maxWidth <= 0.0f) return;
    usize cursor = 0;
    float drawX = x;
    float remaining = maxWidth;
    while (text[cursor] && remaining > size * 0.28f) {
        char segment[23]{};
        int out = 0;
        float segmentWidth = 0.0f;
        while (text[cursor] && out < 22) {
            const u8 lead = static_cast<u8>(text[cursor]);
            const int byteWidth = (lead & 0x80u) == 0 ? 1 :
                                  ((lead & 0xE0u) == 0xC0u ? 2 :
                                  ((lead & 0xF0u) == 0xE0u ? 3 : 4));
            if (out + byteWidth > 22) break;

            char glyph[5]{};
            for (int i = 0; i < byteWidth && text[cursor + i]; ++i)
                glyph[i] = text[cursor + i];
            const float glyphWidth = estimateTextWidth(glyph, size);
            if (out > 0 && segmentWidth + glyphWidth > remaining) break;
            if (out == 0 && glyphWidth > remaining) return;

            for (int i = 0; i < byteWidth && text[cursor + i]; ++i)
                segment[out++] = text[cursor + i];
            cursor += static_cast<usize>(byteWidth);
            segmentWidth += glyphWidth;
        }
        if (out <= 0) break;
        segment[out] = 0;
        addText(drawX, y, size, segment, color);

        // Slight overlap compensates for the small difference between the
        // native estimate and Android Paint's real glyph advance. This removes
        // the visible blank seams produced by the old fixed-position chunks.
        float advance = segmentWidth - size * 0.015f;
        if (advance < size * 0.20f) advance = size * 0.20f;
        drawX += advance;
        remaining -= advance;
    }
}

void NativeMusicUi::addTextWrapped(float x, float y, float size,
                                   const char* text, float maxWidth,
                                   float lineHeight, int maxLines,
                                   u32 color) {
    if (!text || !*text || maxWidth <= 0.0f || maxLines <= 0) return;
    usize cursor = 0;
    for (int line = 0; line < maxLines && text[cursor]; ++line) {
        char lineText[257]{};
        int out = 0;
        int lastSpaceOut = -1;
        usize lastSpaceCursor = 0;
        float width = 0.0f;

        while (text[cursor] && out < 255) {
            const u8 lead = static_cast<u8>(text[cursor]);
            const int byteWidth = (lead & 0x80u) == 0 ? 1 :
                                  ((lead & 0xE0u) == 0xC0u ? 2 :
                                  ((lead & 0xF0u) == 0xE0u ? 3 : 4));
            char glyph[5]{};
            for (int i = 0; i < byteWidth && text[cursor + i]; ++i)
                glyph[i] = text[cursor + i];
            const float glyphWidth = estimateTextWidth(glyph, size);
            if (out > 0 && width + glyphWidth > maxWidth) break;
            if (out + byteWidth >= 256) break;

            for (int i = 0; i < byteWidth && text[cursor + i]; ++i)
                lineText[out++] = text[cursor + i];
            cursor += static_cast<usize>(byteWidth);
            width += glyphWidth;
            if (byteWidth == 1 && glyph[0] == ' ') {
                lastSpaceOut = out - 1;
                lastSpaceCursor = cursor;
            }
        }

        if (text[cursor] && lastSpaceOut > 0 && line + 1 < maxLines) {
            cursor = lastSpaceCursor;
            out = lastSpaceOut;
            while (text[cursor] == ' ') ++cursor;
        }
        while (out > 0 && lineText[out - 1] == ' ') --out;
        lineText[out] = 0;
        if (out > 0)
            addTextFlow(x, y + line * lineHeight, size,
                        lineText, maxWidth, color);
    }
}

void NativeMusicUi::addImage(float x, float y, float w, float h,
                             const char* imageId, u32 color) {
    if (mCommandCount >= 768 || !imageId || !*imageId) return;
    auto& c = mCommands[mCommandCount++];
    memset(&c, 0, sizeof(c));
    c.type = leviabi::DrawCommandType::Image;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.color = applyUiOpacity(color);
    leviabi::setShortString(c.imageId, imageId);
}

void NativeMusicUi::drawPreviousIcon(float x, float y, float s, u32 color) {
    addRect(x - 11 * s, y - 9 * s, 3 * s, 18 * s, color, 1 * s);
    addTriangle(x - 7 * s, y, x + 7 * s, y - 10 * s,
                x + 7 * s, y + 10 * s, color);
}

void NativeMusicUi::drawNextIcon(float x, float y, float s, u32 color) {
    addRect(x + 8 * s, y - 9 * s, 3 * s, 18 * s, color, 1 * s);
    addTriangle(x + 7 * s, y, x - 7 * s, y - 10 * s,
                x - 7 * s, y + 10 * s, color);
}

void NativeMusicUi::drawPlayPauseIcon(float x, float y, float s,
                                      bool playing, u32 color) {
    if (playing) {
        addRect(x - 8 * s, y - 11 * s, 5 * s, 22 * s, color, 1 * s);
        addRect(x + 3 * s, y - 11 * s, 5 * s, 22 * s, color, 1 * s);
    } else {
        addTriangle(x - 7 * s, y - 12 * s, x - 7 * s, y + 12 * s,
                    x + 13 * s, y, color);
    }
}

void NativeMusicUi::drawStopIcon(float x, float y, float s, u32 color) {
    addRect(x - 8 * s, y - 8 * s, 16 * s, 16 * s, color, 2 * s);
}

void NativeMusicUi::drawShuffleIcon(float x, float y, float s, u32 color) {
    addLine(x - 11 * s, y - 7 * s, x - 3 * s, y - 7 * s, 2 * s, color);
    addLine(x - 3 * s, y - 7 * s, x + 8 * s, y + 7 * s, 2 * s, color);
    addTriangle(x + 8 * s, y + 7 * s, x + 3 * s, y + 2 * s,
                x + 13 * s, y + 2 * s, color);
    addLine(x - 11 * s, y + 7 * s, x - 3 * s, y + 7 * s, 2 * s, color);
    addLine(x - 3 * s, y + 7 * s, x + 8 * s, y - 7 * s, 2 * s, color);
    addTriangle(x + 8 * s, y - 7 * s, x + 3 * s, y - 2 * s,
                x + 13 * s, y - 2 * s, color);
}

void NativeMusicUi::drawLoopIcon(float x, float y, float s,
                                 int mode, u32 color) {
    addLine(x - 10 * s, y - 6 * s, x + 8 * s, y - 6 * s, 2 * s, color);
    addLine(x + 10 * s, y - 4 * s, x + 10 * s, y + 5 * s, 2 * s, color);
    addTriangle(x + 10 * s, y - 9 * s, x + 5 * s, y - 4 * s,
                x + 15 * s, y - 4 * s, color);
    addLine(x + 10 * s, y + 6 * s, x - 8 * s, y + 6 * s, 2 * s, color);
    addLine(x - 10 * s, y + 4 * s, x - 10 * s, y - 5 * s, 2 * s, color);
    addTriangle(x - 10 * s, y + 9 * s, x - 5 * s, y + 4 * s,
                x - 15 * s, y + 4 * s, color);
    if (mode == 2)
        addTextCentered(x - 7 * s, y - 7 * s, 14 * s, 14 * s,
                        8 * s, "1", color);
}

void NativeMusicUi::drawSearchIcon(float x, float y, float s, u32 color) {
    addCircle(x - 2 * s, y - 2 * s, 7 * s, 0x00000000u);
    addLine(x + 3 * s, y + 3 * s, x + 10 * s, y + 10 * s,
            2 * s, color);
    addCircle(x - 2 * s, y - 2 * s, 7 * s, color);
    addCircle(x - 2 * s, y - 2 * s, 4.5f * s, 0xFF242424u);
}

void NativeMusicUi::drawCloseIcon(float x, float y, float s, u32 color) {
    addLine(x - 6 * s, y - 6 * s, x + 6 * s, y + 6 * s, 2 * s, color);
    addLine(x + 6 * s, y - 6 * s, x - 6 * s, y + 6 * s, 2 * s, color);
}

void NativeMusicUi::drawSortIcon(float x, float y, float s, u32 color) {
    addLine(x - 9 * s, y - 7 * s, x + 9 * s, y - 7 * s, 2 * s, color);
    addLine(x - 6 * s, y, x + 6 * s, y, 2 * s, color);
    addLine(x - 3 * s, y + 7 * s, x + 3 * s, y + 7 * s, 2 * s, color);
}

void NativeMusicUi::drawVolumeIcon(float x, float y, float s,
                                   bool muted, u32 color) {
    addRect(x - 10 * s, y - 5 * s, 6 * s, 10 * s, color, 1 * s);
    addTriangle(x - 4 * s, y - 5 * s, x + 3 * s, y - 11 * s,
                x + 3 * s, y + 11 * s, color);
    if (muted) {
        addLine(x + 7 * s, y - 7 * s, x + 16 * s, y + 7 * s, 2 * s, color);
        addLine(x + 16 * s, y - 7 * s, x + 7 * s, y + 7 * s, 2 * s, color);
    } else {
        addLine(x + 7 * s, y - 7 * s, x + 12 * s, y - 3 * s, 2 * s, color);
        addLine(x + 12 * s, y - 3 * s, x + 12 * s, y + 3 * s, 2 * s, color);
        addLine(x + 12 * s, y + 3 * s, x + 7 * s, y + 7 * s, 2 * s, color);
    }
}

void NativeMusicUi::renderUnlocked() {
    if (!mApi || !mRegistered || !mConfig) return;
    mCommandCount = 0;
    if (!mOpen) {
        mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands, 0);
        mVisible = 0;
        return;
    }
    if (mFilterDirty) rebuildVisibleIndex();

    float panelX{}, panelY{}, panelW{}, panelH{}, scale{};
    calculateLayout(panelX, panelY, panelW, panelH, scale);
    ensureSourceVisible();

    const bool darkTheme = useDarkTheme();
    const u32 black = darkTheme ? 0xFA000000u : 0xFFF1F1F1u;
    const u32 sidebar = darkTheme ? 0xFC121212u : 0xFFFFFFFFu;
    const u32 mainSurface = darkTheme ? 0xFC181818u : 0xFFF8F8F8u;
    const u32 playerSurface = darkTheme ? 0xFC181818u : 0xFFFFFFFFu;
    const u32 elevated = darkTheme ? 0xFF242424u : 0xFFE9E9E9u;
    const u32 hover = darkTheme ? 0xFF2A2A2Au : 0xFFDEDEDEu;
    const u32 selectedSurface = darkTheme ? 0xFF303030u : 0xFFD5D5D5u;
    const u32 green = 0xFF1DB954u;
    const u32 greenBright = darkTheme ? 0xFF1ED760u : 0xFF11883Du;
    const u32 white = darkTheme ? 0xFFFFFFFFu : 0xFF111111u;
    const u32 softWhite = darkTheme ? 0xFFE7E7E7u : 0xFF242424u;
    const u32 muted = darkTheme ? 0xFFB3B3B3u : 0xFF5E5E5Eu;
    const u32 dim = darkTheme ? 0xFF787878u : 0xFF777777u;
    const u32 divider = darkTheme ? 0xFF353535u : 0xFFD0D0D0u;
    const u32 danger = 0xFFFF5A5Fu;

    const float titleFont = maxFloat(14.0f, 24.0f * scale);
    const float sectionFont = maxFloat(11.0f, 17.0f * scale);
    const float bodyFont = maxFloat(9.0f, 14.0f * scale);
    const float smallFont = maxFloat(8.0f, 11.0f * scale);

    addRect(panelX + 10 * scale, panelY + 12 * scale,
            panelW, panelH, 0x66000000u, 22 * scale);
    addRect(panelX, panelY, panelW, panelH, black, 20 * scale);

    const float sidebarX = panelX + 8 * scale;
    const float sidebarY = panelY + 8 * scale;
    const float sidebarW = 230 * scale;
    const float sidebarH = 640 * scale;
    const float mainX = panelX + 246 * scale;
    const float mainY = panelY + 8 * scale;
    const float mainW = panelW - 254 * scale;
    const float mainH = 640 * scale;
    const float bottomX = panelX + 8 * scale;
    const float bottomY = panelY + 656 * scale;
    const float bottomW = panelW - 16 * scale;
    const float bottomH = 96 * scale;

    addRect(sidebarX, sidebarY, sidebarW, sidebarH,
            sidebar, 16 * scale);
    addRect(mainX, mainY, mainW, mainH,
            mainSurface, 16 * scale);
    addRect(bottomX, bottomY, bottomW, bottomH,
            playerSurface, 14 * scale);

    addCircle(sidebarX + 30 * scale, sidebarY + 30 * scale,
              18 * scale, green);
    addLine(sidebarX + 20 * scale, sidebarY + 28 * scale,
            sidebarX + 40 * scale, sidebarY + 28 * scale,
            3 * scale, 0xFF06240Fu);
    addLine(sidebarX + 23 * scale, sidebarY + 35 * scale,
            sidebarX + 37 * scale, sidebarY + 35 * scale,
            3 * scale, 0xFF06240Fu);
    addText(sidebarX + 58 * scale, sidebarY + 15 * scale,
            sectionFont, "LeviMusic", white);
    addText(sidebarX + 58 * scale, sidebarY + 37 * scale,
            smallFont, "LOCAL PLAYER", muted);

    const bool allActive = mActiveTab == 0;
    addRect(sidebarX + 12 * scale, sidebarY + 70 * scale,
            sidebarW - 24 * scale, 44 * scale,
            allActive ? selectedSurface : sidebar, 8 * scale);
    addCircle(sidebarX + 31 * scale, sidebarY + 92 * scale,
              8 * scale, allActive ? green : muted);
    addCircle(sidebarX + 31 * scale, sidebarY + 92 * scale,
              3 * scale, sidebar);
    addText(sidebarX + 50 * scale, sidebarY + 82 * scale,
            bodyFont, "All Music", allActive ? white : softWhite);

    addText(sidebarX + 16 * scale, sidebarY + 128 * scale,
            smallFont, "YOUR LIBRARY", muted);
    const float sourceY = sidebarY + 154 * scale;
    for (int slot = 0; slot < VisibleSources; ++slot) {
        const int sourceIndex = mSourceOffset + slot;
        const MusicSource* source = mLibrary->source(sourceIndex);
        if (!source) break;
        const float y = sourceY + slot * 52 * scale;
        const bool active = mActiveTab == sourceIndex + 1;
        const bool pressed = mPressedControl == ControlSourceBase + slot;
        addRect(sidebarX + 12 * scale, y,
                sidebarW - 24 * scale, 46 * scale,
                pressed ? hover : (active ? selectedSurface : sidebar),
                8 * scale);
        addRect(sidebarX + 20 * scale, y + 8 * scale,
                30 * scale, 30 * scale,
                active ? green : elevated, 6 * scale);
        addLine(sidebarX + 27 * scale, y + 17 * scale,
                sidebarX + 43 * scale, y + 17 * scale,
                2 * scale, active ? 0xFF06240Fu : muted);
        addLine(sidebarX + 27 * scale, y + 24 * scale,
                sidebarX + 39 * scale, y + 24 * scale,
                2 * scale, active ? 0xFF06240Fu : muted);
        addTextFlow(sidebarX + 60 * scale, y + 8 * scale,
                    bodyFont, source->label,
                    sidebarW - 82 * scale,
                    active ? white : softWhite);
        char sourceCount[16]{};
        snprintf(sourceCount, sizeof(sourceCount), "%d songs",
                 source->trackCount);
        addText(sidebarX + 60 * scale, y + 27 * scale,
                smallFont, sourceCount, muted);
    }

    addRect(sidebarX + 12 * scale, sidebarY + 580 * scale,
            94 * scale, 38 * scale, elevated, 10 * scale);
    addRect(sidebarX + 124 * scale, sidebarY + 580 * scale,
            94 * scale, 38 * scale, elevated, 10 * scale);
    addTextCentered(sidebarX + 12 * scale, sidebarY + 580 * scale,
                    94 * scale, 38 * scale, smallFont, "PREVIOUS", muted);
    addTextCentered(sidebarX + 124 * scale, sidebarY + 580 * scale,
                    94 * scale, 38 * scale, smallFont, "NEXT", muted);

    const char* activeLabel = "All Music";
    const char* activePath = "Every configured music directory";
    if (mActiveTab > 0) {
        const MusicSource* source = mLibrary->source(mActiveTab - 1);
        if (source) {
            activeLabel = source->label;
            activePath = source->path;
        }
    }
    addText(mainX + 22 * scale, mainY + 18 * scale,
            titleFont, "Your Library", white);
    addTextFlow(mainX + 22 * scale, mainY + 51 * scale,
                smallFont, activePath, mainW - 220 * scale, muted);
    char trackCountText[32]{};
    snprintf(trackCountText, sizeof(trackCountText), "%d songs",
             mVisibleCount);
    addTextRight(mainX + mainW - 22 * scale, mainY + 24 * scale,
                 bodyFont, trackCountText, muted);

    const float searchX = mainX + 20 * scale;
    const float searchY = mainY + 82 * scale;
    const float searchW = mainW - 214 * scale;
    const bool searchPressed = mPressedControl == ControlSearch;
    addRect(searchX, searchY, searchW, 44 * scale,
            searchPressed || mSearchActive ? selectedSurface : elevated,
            22 * scale);
    drawSearchIcon(searchX + 24 * scale, searchY + 22 * scale,
                   0.75f * scale, muted);
    if (mSearchText[0]) {
        addTextFlow(searchX + 48 * scale, searchY + 12 * scale,
                    bodyFont, mSearchText, searchW - 92 * scale, white);
        drawCloseIcon(searchX + searchW - 22 * scale,
                      searchY + 22 * scale, 0.75f * scale, muted);
    } else {
        char onlineStatus[96];
        online::statusText(onlineStatus, sizeof(onlineStatus));
        addText(searchX + 48 * scale, searchY + 12 * scale, bodyFont,
                onlineStatus[0] ? onlineStatus
                                : (mConfig->config().onlineEnabled
                                       ? "Search songs, artists, online"
                                       : "Search songs, artists"),
                onlineStatus[0] ? green : muted);
    }
    if (mSearchActive && ((mAnimationFrame / 8) & 1) == 0)
        addRect(searchX + 18 * scale,
                searchY + 41 * scale, searchW - 36 * scale,
                2 * scale, green, 1 * scale);

    const float sortX = mainX + mainW - 176 * scale;
    addRect(sortX, searchY, 156 * scale, 44 * scale,
            mPressedControl == ControlSort ? hover : elevated,
            22 * scale);
    drawSortIcon(sortX + 24 * scale, searchY + 22 * scale,
                 0.7f * scale, muted);
    const char* sortText = mSortMode == 0 ? "Custom order" :
                           (mSortMode == 1 ? "Title A-Z" : "Artist A-Z");
    addText(sortX + 44 * scale, searchY + 12 * scale,
            bodyFont, sortText, softWhite);

    const float listX = mainX + 20 * scale;
    const float listY = mainY + 170 * scale;
    const float rowW = mainW - 40 * scale;
    addText(listX + 14 * scale, mainY + 145 * scale,
            smallFont, "#", dim);
    addText(listX + 54 * scale, mainY + 145 * scale,
            smallFont, "TITLE", dim);
    addText(listX + rowW - 250 * scale, mainY + 145 * scale,
            smallFont, "FOLDER", dim);
    addTextRight(listX + rowW - 18 * scale, mainY + 145 * scale,
                 smallFont, "FORMAT", dim);
    addLine(listX, mainY + 164 * scale,
            listX + rowW, mainY + 164 * scale,
            1 * scale, divider);

    const int selected = selectedGlobalIndex();
    for (int row = 0; row < RowsPerPage; ++row) {
        const int globalIndex = globalIndexForRow(row);
        MusicTrack* mutableTrack = mLibrary->trackMutable(globalIndex);
        const MusicTrack* track = mutableTrack;
        if (!track) break;
        const float y = listY + row * 56 * scale;
        const bool selectedRow = globalIndex == selected;
        const bool pressed = mPressedControl == ControlRowBase + row;
        if (selectedRow && mutableTrack) mMetadata->ensure(*mutableTrack);
        addRect(listX, y, rowW, 52 * scale,
                pressed ? hover : (selectedRow ? selectedSurface : mainSurface),
                7 * scale);
        if (selectedRow)
            addRect(listX, y + 6 * scale, 3 * scale,
                    40 * scale, green, 2 * scale);

        char number[12]{};
        snprintf(number, sizeof(number), "%d",
                 mPage * RowsPerPage + row + 1);
        if (selectedRow && mSnapshot.playing) {
            for (int bar = 0; bar < 4; ++bar) {
                const float height = (5.0f + mSpectrum[bar] * 15.0f) * scale;
                addRect(listX + (14 + bar * 5) * scale,
                        y + 38 * scale - height,
                        3 * scale, height, green, 1 * scale);
            }
        } else {
            addTextCentered(listX + 4 * scale, y,
                            38 * scale, 52 * scale,
                            smallFont, number, dim);
        }

        const char* rowTitle = track->title[0] ? track->title : track->name;
        addTextFlow(listX + 54 * scale, y + 7 * scale,
                    bodyFont, rowTitle, rowW - 340 * scale,
                    selectedRow ? greenBright : white);
        const char* artist = track->artist[0] ? track->artist : "Unknown artist";
        addTextFlow(listX + 54 * scale, y + 29 * scale,
                    smallFont, artist, rowW - 340 * scale, muted);
        const MusicSource* source = mLibrary->source(track->sourceIndex);
        if (source)
            addTextFlow(listX + rowW - 250 * scale,
                        y + 17 * scale, smallFont,
                        source->label, 150 * scale, muted);
        addRect(listX + rowW - 78 * scale, y + 12 * scale,
                60 * scale, 28 * scale, elevated, 14 * scale);
        addTextCentered(listX + rowW - 78 * scale, y + 12 * scale,
                        60 * scale, 28 * scale, smallFont,
                        track->format, selectedRow ? greenBright : muted);
    }

    if (mVisibleCount == 0) {
        addCircle(listX + rowW * 0.5f,
                  listY + 140 * scale, 30 * scale, elevated);
        drawSearchIcon(listX + rowW * 0.5f,
                       listY + 140 * scale, 1.2f * scale, muted);
        addTextCentered(listX, listY + 184 * scale,
                        rowW, 42 * scale, sectionFont,
                        mSearchText[0] ? "No matching songs" : "No audio found",
                        softWhite);
        addTextCentered(listX, listY + 222 * scale,
                        rowW, 32 * scale, smallFont,
                        mSearchText[0] ? (mConfig->config().onlineEnabled
                                              ? "Press Enter to search online"
                                              : "Try another search")
                                      : "Check music_directories",
                        muted);
    }

    const float footerY = mainY + 590 * scale;
    addRect(mainX + 20 * scale, footerY,
            102 * scale, 36 * scale, elevated, 18 * scale);
    addTextCentered(mainX + 20 * scale, footerY,
                    102 * scale, 36 * scale,
                    smallFont, "REFRESH", softWhite);
    addCircle(mainX + 140 * scale, footerY + 18 * scale,
              4 * scale, green);
    addText(mainX + 152 * scale, footerY + 10 * scale,
            smallFont, "LIVE LIBRARY", muted);
    char resultsText[32]{};
    snprintf(resultsText, sizeof(resultsText), "%d results", mVisibleCount);
    addTextCentered(mainX + mainW * 0.5f - 90 * scale,
                    footerY, 180 * scale, 36 * scale,
                    smallFont, resultsText, muted);
    addRect(mainX + mainW - 174 * scale, footerY,
            72 * scale, 36 * scale, elevated, 18 * scale);
    addRect(mainX + mainW - 92 * scale, footerY,
            72 * scale, 36 * scale, elevated, 18 * scale);
    drawPreviousIcon(mainX + mainW - 138 * scale,
                     footerY + 18 * scale, 0.65f * scale, muted);
    drawNextIcon(mainX + mainW - 56 * scale,
                 footerY + 18 * scale, 0.65f * scale, muted);
    int pages = (mVisibleCount + RowsPerPage - 1) / RowsPerPage;
    if (pages <= 0) pages = 1;
    char pageText[24]{};
    snprintf(pageText, sizeof(pageText), "%d / %d", mPage + 1, pages);
    addTextCentered(mainX + mainW - 260 * scale,
                    footerY, 76 * scale, 36 * scale,
                    smallFont, pageText, muted);

    const MusicTrack* selectedTrack = mLibrary->track(selected);
    const char* songTitle = selectedTrack
        ? (selectedTrack->title[0] ? selectedTrack->title : selectedTrack->name)
        : "Choose a song";
    const char* artist = selectedTrack && selectedTrack->artist[0]
        ? selectedTrack->artist : "LeviMusic";
    if (selectedTrack) {
        MusicTrack* mutableTrack = mLibrary->trackMutable(selected);
        if (mutableTrack) mMetadata->ensure(*mutableTrack);
    }

    const float artX = bottomX + 12 * scale;
    const float artY = bottomY + 10 * scale;
    const float artSize = 76 * scale;
    if (selectedTrack && selectedTrack->metadataState == 2 &&
        selectedTrack->artworkId[0]) {
        addImage(artX, artY, artSize, artSize,
                 selectedTrack->artworkId, white);
    } else {
        const u32 accent = trackAccent(songTitle);
        addRect(artX, artY, artSize, artSize, accent, 8 * scale);
        addCircle(artX + 38 * scale, artY + 38 * scale,
                  24 * scale, darkTheme ? 0xCC101010u : 0xCCFFFFFFu);
        addCircle(artX + 38 * scale, artY + 38 * scale,
                  7 * scale, accent);
        addCircle(artX + 38 * scale, artY + 38 * scale,
                  3 * scale, darkTheme ? 0xFF101010u : 0xFFFFFFFFu);
    }

    addTextWrapped(bottomX + 102 * scale, bottomY + 12 * scale,
                   bodyFont, songTitle, 350 * scale,
                   22 * scale, 2, white);
    addTextFlow(bottomX + 102 * scale, bottomY + 61 * scale,
                smallFont, artist, 350 * scale, muted);

    const float centerX = panelX + panelW * 0.5f;
    const float controlsY = bottomY + 32 * scale;
    const float offsets[] = {-122, -62, 0, 62, 122, 178};
    const int controlIds[] = {ControlShuffle, ControlTrackPrev,
                              ControlPlayPause, ControlTrackNext,
                              ControlLoop, ControlStop};
    const float radii[] = {22, 22, 30, 22, 22, 20};
    for (int i = 0; i < 6; ++i) {
        const bool active =
            (controlIds[i] == ControlShuffle &&
             mConfig->config().shuffleEnabled) ||
            (controlIds[i] == ControlLoop &&
             mConfig->config().loopMode != 0);
        u32 fill = controlIds[i] == ControlPlayPause ? greenBright :
                   (active ? 0xFF233A2Au : playerSurface);
        if (mPressedControl == controlIds[i]) fill = hover;
        addCircle(centerX + offsets[i] * scale, controlsY,
                  radii[i] * scale, fill);
    }
    drawShuffleIcon(centerX + offsets[0] * scale, controlsY,
                    0.8f * scale,
                    mConfig->config().shuffleEnabled ? greenBright : muted);
    drawPreviousIcon(centerX + offsets[1] * scale, controlsY,
                     0.9f * scale, softWhite);
    drawPlayPauseIcon(centerX, controlsY, 1.0f * scale,
                      mSnapshot.playing, 0xFF000000u);
    drawNextIcon(centerX + offsets[3] * scale, controlsY,
                 0.9f * scale, softWhite);
    drawLoopIcon(centerX + offsets[4] * scale, controlsY,
                 0.8f * scale, mConfig->config().loopMode,
                 mConfig->config().loopMode != 0 ? greenBright : muted);
    drawStopIcon(centerX + offsets[5] * scale, controlsY,
                 0.8f * scale, muted);

    const float progressX = centerX - 250 * scale;
    const float progressY = bottomY + 72 * scale;
    const float progressW = 500 * scale;
    const float progress = mSnapshot.durationMs > 0
        ? clampFloat(static_cast<float>(mSnapshot.positionMs) /
                     static_cast<float>(mSnapshot.durationMs), 0.0f, 1.0f)
        : 0.0f;
    addRect(progressX, progressY, progressW, 4 * scale,
            divider, 2 * scale);
    addRect(progressX, progressY, progressW * progress,
            4 * scale, mPressedControl == ControlSeek ? greenBright : white,
            2 * scale);
    if (mPressedControl == ControlSeek)
        addCircle(progressX + progressW * progress,
                  progressY + 2 * scale, 6 * scale, white);
    char elapsed[16]{};
    char duration[16]{};
    formatTime(mSnapshot.positionMs, elapsed, sizeof(elapsed));
    formatTime(mSnapshot.durationMs, duration, sizeof(duration));
    addTextRight(progressX - 10 * scale, progressY - 4 * scale,
                 smallFont, elapsed, muted);
    addText(progressX + progressW + 10 * scale,
            progressY - 4 * scale, smallFont, duration, muted);

    const float visualX = bottomX + bottomW - 210 * scale;
    const float visualY = bottomY + 13 * scale;
    for (int bar = 0; bar < 16; ++bar) {
        const float height = (3.0f + mSpectrum[bar] * 20.0f) * scale;
        addRect(visualX + bar * 9 * scale,
                visualY + 23 * scale - height,
                5 * scale, height,
                mSnapshot.playing ? green : dim, 2 * scale);
    }
    const float right = panelX + panelW - 16 * scale;
    drawVolumeIcon(right - 209 * scale, bottomY + 59 * scale,
                   0.75f * scale,
                   mConfig->config().volumePercent == 0, muted);
    const float volumeX = right - 186 * scale;
    const float volumeY = bottomY + 57 * scale;
    const float volumeW = 170 * scale;
    const float volume = mConfig->config().volumePercent / 100.0f;
    addRect(volumeX, volumeY, volumeW, 4 * scale,
            divider, 2 * scale);
    addRect(volumeX, volumeY, volumeW * volume,
            4 * scale, mPressedControl == ControlVolumeBar
                ? greenBright : white, 2 * scale);
    if (mPressedControl == ControlVolumeBar)
        addCircle(volumeX + volumeW * volume,
                  volumeY + 2 * scale, 5 * scale, white);

    mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands,
                             static_cast<usize>(mCommandCount));
    mVisible = mCommandCount > 0 ? 1 : 0;
}
