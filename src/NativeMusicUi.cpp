#include "NativeMusicUi.hpp"
#include "FilePicker.hpp"
#include "Config.hpp"
#include "JniBridge.hpp"

static NativeMusicUi* gNativeMusicUi = nullptr;
static constexpr const char* ModuleId = "levi_music";
static constexpr const char* LogTag = "LeviMusic";

// Compact single-card layout. All numbers are base units (scale == 1.0 -> px).
static constexpr float BasePanelWidth = 560.0f;
static constexpr float BasePanelHeight = 410.0f;
static constexpr int VisibleSources = 7;
static constexpr float HeaderH = 60.0f;
static constexpr float SearchRowY = 68.0f;
static constexpr float SearchRowH = 38.0f;
static constexpr float ListX = 14.0f;
static constexpr float ListY = 116.0f;
static constexpr float ListW = 532.0f;
static constexpr float ListH = 192.0f;
static constexpr float RowPitch = 48.0f;
static constexpr float RowHeight = 44.0f;
static constexpr float ScrollBarGutter = 12.0f;
static constexpr float PlayerX = 10.0f;
static constexpr float PlayerY = 316.0f;
static constexpr float PlayerW = 540.0f;
static constexpr float PlayerH = 82.0f;
static constexpr float SeekX = 62.0f;
static constexpr float SeekW = 248.0f;
static constexpr float VolX = 398.0f;
static constexpr float VolW = 136.0f;

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
static constexpr int ControlSourceCycle = 19;
static constexpr int ControlListArea = 20;
static constexpr int ControlSourceBase = 40;
static constexpr int ControlRowBase = 100;

static long long monotonicMs() {
    TimeSpec ts{};
    clock_gettime(1, &ts);   // CLOCK_MONOTONIC
    return static_cast<long long>(ts.tv_sec) * 1000LL +
           static_cast<long long>(ts.tv_nsec / 1000000L);
}

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
    requestRescan();
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
    setConfigEntry(mSettings[mSettingCount++], "show_floating_button",
                   "Show Floating Button", leviabi::ConfigType::Toggle,
                   mConfig->config().showFloatingButton ? "true" : "false");
    setConfigEntry(mSettings[mSettingCount++], "pause_playback",
                   "Pause Playback", leviabi::ConfigType::Button, "");
    setConfigEntry(mSettings[mSettingCount++], "stop_playback",
                   "Stop Playback", leviabi::ConfigType::Button, "");
    setConfigEntry(mSettings[mSettingCount++], "import_songs",
                   "Import Songs (Google Files)",
                   leviabi::ConfigType::Button, "");
}

void NativeMusicUi::shutdown() {
    __atomic_store_n(&mScanAbort, 1, __ATOMIC_RELEASE);
    if (mScanThreadStarted) {
        pthread_join(mScanThread, nullptr);
        mScanThreadStarted = false;
    }
    if (mScanStaging) {
        free(mScanStaging);
        mScanStaging = nullptr;
    }
    mInitialScanApplied = false;
    stopAnimationThread();
    ScopedUiBusy guard(&mUiBusy, true);
    savePanelUiState();
    setSearchActive(false);
    mOpen = 0;
    mPanelShown = 0;
    mVisible = 0;
    mBtnPointerId = -1;
    mBtnPressed = false;
    mBtnMoved = false;
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
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0)
        return false;
    // The floating button sits on top of everything, so it sees touches first.
    if (self->handleButtonTouch(action, pointerId, x, y)) return true;
    if (__atomic_load_n(&self->mPanelShown, __ATOMIC_ACQUIRE) == 0 ||
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
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0 ||
        __atomic_load_n(&self->mPanelShown, __ATOMIC_ACQUIRE) == 0)
        return false;
    return self->onKey(keyCode, unicodeChar, isKeyDown);
}

bool NativeMusicUi::textThunk(const char* text, usize size) {
    NativeMusicUi* self = gNativeMusicUi;
    if (!self || __atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) == 0 ||
        __atomic_load_n(&self->mPanelShown, __ATOMIC_ACQUIRE) == 0)
        return false;
    return self->onText(text, size);
}

void* NativeMusicUi::animationThreadThunk(void* pointer) {
    auto* self = static_cast<NativeMusicUi*>(pointer);
    if (!self) return nullptr;
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
                self->mBtnPositionValid = false;
                if (__atomic_load_n(&self->mOpen,
                                    __ATOMIC_ACQUIRE) != 0)
                    self->renderUnlocked();
                levi_log(LOG_INFO_VALUE, LogTag,
                                    "config.json changed on disk; music directories hot reloaded");
            }
        }

        if (__atomic_load_n(&self->mScanFinished, __ATOMIC_ACQUIRE) != 0) {
            ScopedUiBusy guard(&self->mUiBusy, false);
            if (guard.locked()) self->finishScan();
        }
        if (++self->mPickerPollFrames >= 20) {
            self->mPickerPollFrames = 0;
            if (filepicker::isReady() && filepicker::takeFinished()) {
                const filepicker::Status result = filepicker::poll();
                levi_log(LOG_INFO_VALUE, LogTag,
                         "import finished: %d of %d imported, %d failed",
                         result.total - result.failed, result.total,
                         result.failed);
                ScopedUiBusy guard(&self->mUiBusy, true);
                self->requestRescan();
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
                self->tickPlaybackUnlocked();
                if (__atomic_load_n(&self->mOpen, __ATOMIC_ACQUIRE) != 0 &&
                    __atomic_load_n(&self->mPanelShown,
                                    __ATOMIC_ACQUIRE) == 0 &&
                    self->mConfig->config().showFloatingButton) {
                    // Panel hidden: only the button's "playing" dot can change.
                    const int btnSig = (self->mSnapshot.playing ? 1 : 0) |
                                       (self->mSnapshot.paused ? 2 : 0) |
                                       (self->mSnapshot.hasMedia ? 4 : 0);
                    if (btnSig != self->mLastBtnSig) {
                        self->mLastBtnSig = btnSig;
                        self->renderUnlocked();
                    }
                } else if (__atomic_load_n(&self->mOpen,
                                           __ATOMIC_ACQUIRE) != 0) {
                    // Redraw only when something visible can have changed.
                    bool redraw = false;
                    if (self->mFilterDirty) {
                        self->rebuildVisibleIndex();
                        redraw = true;
                    }
                    const int sig = (self->mSnapshot.playing ? 1 : 0) |
                                    (self->mSnapshot.paused ? 2 : 0) |
                                    (self->mSnapshot.hasMedia ? 4 : 0) |
                                    ((self->mSnapshot.positionMs / 1000) << 3);
                    if (sig != self->mLastUiSig) {
                        self->mLastUiSig = sig;
                        redraw = true;
                    }
                    if (self->mSnapshot.playing &&
                        (self->mAnimationFrame % 3) == 0) redraw = true;
                    if (self->mSearchActive &&
                        (self->mAnimationFrame % 8) == 0) redraw = true;
                    if (redraw) self->renderUnlocked();
                }
            }
        }
        // Sleep in ~16 ms slices so list momentum scrolling stays smooth
        // while the rest of the loop keeps its original ~50 ms cadence.
        const TimeSpec sliceDelay{0, 16000000L};
        for (int slice = 0; slice < 3; ++slice) {
            nanosleep(&sliceDelay, nullptr);
            if (self->mFlingVel != 0.0f) {
                ScopedUiBusy guard(&self->mUiBusy, false);
                if (guard.locked()) self->stepFling();
            }
        }
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
    mPage = config.uiPage < 0 ? 0 : config.uiPage;
    mScrollPx = static_cast<float>(mPage) * RowPitch;
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
    // Enabling the module in the Mod Menu shows the panel (as before); the
    // floating button can then hide/show it without touching the Mod Menu.
    mPanelShown = open ? 1 : 0;
    mBtnPointerId = -1;
    mBtnPressed = false;
    mBtnMoved = false;
    mLastBtnSig = -1;
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
        fitScale = clampFloat(fitScale, 0.30f, 1.50f);
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
    clampScroll();
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
    const int visible = row;   // absolute row in the filtered list
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


// Player-strip control centres (panel base units) and radii.
static constexpr float ControlCenterX[5] = {318.0f, 360.0f, 412.0f, 464.0f, 506.0f};
static constexpr float ControlRadius[5] = {15.0f, 16.0f, 21.0f, 16.0f, 15.0f};
static constexpr float ControlCenterY = PlayerY + 27.0f;

int NativeMusicUi::classifyControl(float x, float y, float panelX,
                                   float panelY, float panelW, float panelH,
                                   float scale) const {
    (void)panelW; (void)panelH;
    const float rowY = panelY + SearchRowY * scale;
    const float rowH = SearchRowH * scale;
    const float searchX = panelX + 14.0f * scale;
    const float searchW = 258.0f * scale;
    if (mSearchText[0] &&
        hit(x, y, searchX + searchW - 40.0f * scale, rowY,
            40.0f * scale, rowH))
        return ControlClearSearch;
    if (hit(x, y, searchX, rowY, searchW, rowH)) return ControlSearch;
    if (hit(x, y, panelX + 280.0f * scale, rowY, 140.0f * scale, rowH))
        return ControlSourceCycle;
    if (hit(x, y, panelX + 428.0f * scale, rowY, 118.0f * scale, rowH))
        return ControlSort;

    const float listX = panelX + ListX * scale;
    const float listY = panelY + ListY * scale;
    const float listW = ListW * scale;
    const float listH = ListH * scale;
    if (hit(x, y, listX, listY, listW, listH)) {
        const float rel = (y - listY) / scale + mScrollPx;
        const int index = static_cast<int>(rel / RowPitch);
        const float within = rel - static_cast<float>(index) * RowPitch;
        if (rel >= 0.0f && index >= 0 && index < mVisibleCount &&
            within <= RowHeight &&
            x <= listX + (ListW - ScrollBarGutter) * scale)
            return ControlRowBase + index;
        return ControlListArea;
    }

    static const int ids[5] = {ControlShuffle, ControlTrackPrev,
                               ControlPlayPause, ControlTrackNext,
                               ControlLoop};
    for (int i = 0; i < 5; ++i) {
        const float r = ControlRadius[i] + 5.0f;
        if (hit(x, y, panelX + (ControlCenterX[i] - r) * scale,
                panelY + (ControlCenterY - r) * scale,
                r * 2.0f * scale, r * 2.0f * scale)) return ids[i];
    }

    const float barY = panelY + (PlayerY + 62.0f) * scale;
    if (hit(x, y, panelX + (SeekX - 8.0f) * scale, barY - 12.0f * scale,
            (SeekW + 16.0f) * scale, 26.0f * scale)) return ControlSeek;
    if (hit(x, y, panelX + 362.0f * scale, barY - 12.0f * scale,
            28.0f * scale, 26.0f * scale)) return ControlMute;
    if (hit(x, y, panelX + (VolX - 6.0f) * scale, barY - 12.0f * scale,
            (VolW + 12.0f) * scale, 26.0f * scale)) return ControlVolumeBar;
    return 0;
}

void NativeMusicUi::seekFromPointer(float x, float panelX, float panelY,
                                    float scale) {
    (void)panelY;
    if (!mPlayer || !mPlayer->hasMedia()) return;
    const float begin = panelX + SeekX * scale;
    const float width = SeekW * scale;
    const float fraction = clampFloat((x - begin) / width, 0.0f, 1.0f);
    const int duration = mPlayer->durationMs();
    if (duration > 0)
        mPlayer->seekToMs(static_cast<int>(duration * fraction));
}

void NativeMusicUi::volumeFromPointer(float x, float panelX, float panelY,
                                      float scale) {
    (void)panelY;
    const float begin = panelX + VolX * scale;
    const float width = VolW * scale;
    const float fraction = clampFloat((x - begin) / width, 0.0f, 1.0f);
    const int percent = static_cast<int>(fraction * 100.0f + 0.5f);
    if (percent > 0) mVolumeBeforeMute = percent;
    mConfig->setVolumePercent(percent);
    mPlayer->setVolume(fraction);
}

float NativeMusicUi::maxScroll() const {
    const float content = static_cast<float>(mVisibleCount) * RowPitch;
    return maxFloat(0.0f, content - ListH);
}

void NativeMusicUi::clampScroll() {
    if (mVisibleCount <= 0) return;   // keep the restored position until a scan lands
    mScrollPx = clampFloat(mScrollPx, 0.0f, maxScroll());
    syncPageFromScroll();
}

void NativeMusicUi::syncPageFromScroll() {
    mPage = static_cast<int>(mScrollPx / RowPitch + 0.5f);
    if (mPage < 0) mPage = 0;
}

void NativeMusicUi::scrollToVisibleIndex(int visibleIndex) {
    if (visibleIndex < 0) return;
    const float top = static_cast<float>(visibleIndex) * RowPitch;
    if (top < mScrollPx) mScrollPx = top;
    else if (top + RowHeight > mScrollPx + ListH)
        mScrollPx = top + RowHeight - ListH;
    mFlingVel = 0.0f;
    clampScroll();
}

void NativeMusicUi::stepFling() {
    if (mFlingVel == 0.0f || !mOpen) {
        mFlingVel = 0.0f;
        return;
    }
    mScrollPx += mFlingVel;
    const float limit = maxScroll();
    if (mScrollPx <= 0.0f) { mScrollPx = 0.0f; mFlingVel = 0.0f; }
    else if (mScrollPx >= limit) { mScrollPx = limit; mFlingVel = 0.0f; }
    mFlingVel *= 0.93f;
    if (mFlingVel > -0.35f && mFlingVel < 0.35f) mFlingVel = 0.0f;
    if (mFlingVel == 0.0f) syncPageFromScroll();
    renderUnlocked();
}

void NativeMusicUi::addRaised(float x, float y, float w, float h,
                              float radius, float scale, u32 face,
                              u32 darkEdge, u32 lightEdge) {
    addRect(x + 2.0f * scale, y + 3.0f * scale, w, h, darkEdge, radius);
    addRect(x - 1.5f * scale, y - 1.5f * scale, w, h, lightEdge, radius);
    addRect(x, y, w, h, face, radius);
}

void NativeMusicUi::addInset(float x, float y, float w, float h,
                             float radius, float scale, u32 face,
                             u32 darkEdge, u32 lightEdge) {
    addRect(x, y, w, h, face, radius);
    const float inner = maxFloat(radius * 0.8f, 2.0f * scale);
    addRect(x + inner, y + 1.0f * scale, w - inner * 2.0f, 1.6f * scale,
            darkEdge, 0.8f * scale);
    addRect(x + inner, y + h - 2.0f * scale, w - inner * 2.0f, 1.0f * scale,
            lightEdge, 0.5f * scale);
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
    if (visible >= 0) scrollToVisibleIndex(visible);
    if (renderAfter) renderUnlocked();
}

bool NativeMusicUi::startPlayback(const char* path) {
    return mPlayer->play(path);
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
    mScrollPx = 0.0f; mFlingVel = 0.0f; mPage = 0;
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
    mScrollPx = 0.0f; mFlingVel = 0.0f; mPage = 0;
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
    mScrollPx = 0.0f; mFlingVel = 0.0f; mPage = 0;
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
    if (control == ControlTrackPrev) {
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
        mScrollPx = 0.0f; mFlingVel = 0.0f; mPage = 0;
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
    } else if (control == ControlSourceCycle || control == ControlAllSource) {
        // The source chip cycles: All music -> folder 1 -> folder 2 -> ...
        const int total = mLibrary ? mLibrary->sourceCount() + 1 : 1;
        mActiveTab = control == ControlAllSource
            ? 0 : (mActiveTab + 1) % (total > 0 ? total : 1);
        mScrollPx = 0.0f; mFlingVel = 0.0f; mPage = 0;
        markFilterDirty();
        rebuildVisibleIndex();
        ensureSourceVisible();
    } else if (control >= ControlRowBase) {
        const int visible = control - ControlRowBase;
        if (visible >= 0 && visible < mVisibleCount)
            selectTrackInternal(mVisibleIndices[visible],
                                true, false, currentSourceFilter());
    }
    // Navigation/panel state is auto-saved alongside config changes. Seek and
    // volume drags use their dedicated setters and avoid extra disk writes.
    savePanelUiState();
    renderUnlocked();
}

bool NativeMusicUi::onTouch(const leviabi::TouchEvent& event) {
    ScopedUiBusy guard(&mUiBusy, true);
    if (!mOpen || !mPanelShown) return false;
    float panelX{}, panelY{}, panelW{}, panelH{}, scale{};
    calculateLayout(panelX, panelY, panelW, panelH, scale);
    const bool inside = hit(event.x, event.y, panelX, panelY,
                            panelW, panelH);

    if (event.action == 0 || event.action == 5) {
        if (!inside) return false;
        mFlingVel = 0.0f;   // a touch stops momentum scrolling
        mPressedControl = classifyControl(event.x, event.y, panelX,
                                          panelY, panelW, panelH, scale);
        if (mPressedControl >= ControlRowBase ||
            mPressedControl == ControlListArea) {
            // Possible tap or scroll: decide once the finger moves.
            mScrollTouching = true;
            mScrolling = false;
            mScrollPointerId = event.pointerId;
            mTouchStartY = event.y;
            mLastTouchY = event.y;
            mLastTouchMs = monotonicMs();
            mScrollVelPerMs = 0.0f;
            mScrollStartPx = mScrollPx;
            if (mPressedControl == ControlListArea) mPressedControl = 0;
            renderUnlocked();
            return true;
        }
        if (mPressedControl != 0) {
            renderUnlocked();
            return true;
        }
        const bool dragRegion = event.y <= panelY + HeaderH * scale;
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
        if (mScrollTouching && event.pointerId == mScrollPointerId) {
            const float dy = event.y - mTouchStartY;
            if (!mScrolling && (dy > 10.0f * scale || dy < -10.0f * scale)) {
                mScrolling = true;
                mPressedControl = 0;   // it is a scroll, not a tap
            }
            if (mScrolling) {
                const long long now = monotonicMs();
                const float dt = static_cast<float>(now - mLastTouchMs);
                if (dt >= 1.0f) {
                    const float instant =
                        -(event.y - mLastTouchY) / scale / dt;
                    mScrollVelPerMs = 0.7f * mScrollVelPerMs +
                                      0.3f * instant;
                    mLastTouchMs = now;
                    mLastTouchY = event.y;
                }
                mScrollPx = clampFloat(mScrollStartPx - dy / scale,
                                       0.0f, maxScroll());
                renderUnlocked();
            }
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
        if (mScrollTouching && event.pointerId == mScrollPointerId) {
            mScrollTouching = false;
            mScrollPointerId = -1;
            if (mScrolling) {
                mScrolling = false;
                mPressedControl = 0;
                const long long idle = monotonicMs() - mLastTouchMs;
                float fling = idle > 90 ? 0.0f : mScrollVelPerMs * 16.0f;
                if (fling > 60.0f) fling = 60.0f;
                if (fling < -60.0f) fling = -60.0f;
                mFlingVel = (fling > -2.0f && fling < 2.0f) ? 0.0f : fling;
                syncPageFromScroll();
                if (mFlingVel == 0.0f) savePanelUiState();
                renderUnlocked();
                return true;
            }
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
        mScrollTouching = false;
        mScrolling = false;
        mScrollPointerId = -1;
        savePanelUiState();
        renderUnlocked();
        return true;
    }
    return inside;
}

void NativeMusicUi::applyScannedLibrary() {
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

    // The very first scan finishes after the saved panel state was loaded, so
    // restore the remembered playlist tab from the config instead.
    if (!mInitialScanApplied) {
        mInitialScanApplied = true;
        copyString(activeSourcePath, sizeof(activeSourcePath),
                   mConfig->config().uiActivePlaylist);
    }

    *mLibrary = *mScanStaging;

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
    clampScroll();
}

void NativeMusicUi::rescanLibraryPreservingState() {
    requestRescan();
}

void NativeMusicUi::requestRescan() {
    if (mScanRunning) {
        mScanQueued = 1;   // run once more when the current scan ends
        return;
    }
    if (!mScanStaging) {
        mScanStaging = static_cast<MusicLibrary*>(malloc(sizeof(MusicLibrary)));
        if (!mScanStaging) return;
    }
    memset(static_cast<void*>(mScanStaging), 0, sizeof(MusicLibrary));
    mScanStaging->setAbortFlag(&mScanAbort);
    mScanConfig = mConfig->config();
    mScanAbort = 0;
    mScanQueued = 0;
    mScanFinished = 0;
    mScanRunning = 1;
    if (pthread_create(&mScanThread, nullptr, &scanThreadThunk, this) != 0) {
        mScanRunning = 0;
        levi_log(LOG_ERROR_VALUE, LogTag, "could not start the library scan thread");
        return;
    }
    mScanThreadStarted = true;
}

void* NativeMusicUi::scanThreadThunk(void* self) {
    static_cast<NativeMusicUi*>(self)->scanWorker();
    return nullptr;
}

void NativeMusicUi::scanWorker() {
    mScanStaging->scan(mScanConfig);
    __atomic_store_n(&mScanRunning, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mScanFinished, 1, __ATOMIC_RELEASE);
}

// UI side: swaps in the finished scan.
void NativeMusicUi::finishScan() {
    if (mScanThreadStarted) {
        pthread_join(mScanThread, nullptr);
        mScanThreadStarted = false;
    }
    __atomic_store_n(&mScanFinished, 0, __ATOMIC_RELEASE);
    if (!mScanAbort && mScanStaging) {
        applyScannedLibrary();
        levi_log(LOG_INFO_VALUE, LogTag, "library scan finished: %d tracks in %d folders",
                 mLibrary->count(), mLibrary->sourceCount());
        if (__atomic_load_n(&mOpen, __ATOMIC_ACQUIRE) != 0) renderUnlocked();
    }
    if (mScanQueued) requestRescan();
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
    const bool uiOpen = __atomic_load_n(&mOpen, __ATOMIC_ACQUIRE) != 0 &&
                        __atomic_load_n(&mPanelShown, __ATOMIC_ACQUIRE) != 0;
    // The visualizer is polled over JNI, so only do it at the redraw rate
    // while something is actually playing.
    if (uiOpen && mSnapshot.playing && (mAnimationFrame % 3) == 0)
        mPlayer->captureSpectrum(mSpectrum, 24);

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

void NativeMusicUi::drawSearchIcon(float x, float y, float s, u32 color,
                                   u32 hole) {
    addLine(x + 3 * s, y + 3 * s, x + 10 * s, y + 10 * s,
            2 * s, color);
    addCircle(x - 2 * s, y - 2 * s, 7 * s, color);
    addCircle(x - 2 * s, y - 2 * s, 4.5f * s, hole);
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
    if (!mPanelShown) {
        // Panel hidden: draw only the floating button (if enabled).
        drawFloatingButton();
        mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands,
                                 static_cast<usize>(mCommandCount));
        mVisible = 0;
        return;
    }
    if (mFilterDirty) rebuildVisibleIndex();

    float panelX{}, panelY{}, panelW{}, panelH{}, s{};
    calculateLayout(panelX, panelY, panelW, panelH, s);
    ensureSourceVisible();
    clampScroll();

    // --- palette (LeviVC-style soft dark surfaces, mint accent) -----------
    const bool dark = useDarkTheme();
    const u32 cardC     = dark ? 0xFA1B1E25u : 0xFAE9ECF2u;
    const u32 raisedC   = dark ? 0xFF21252Du : 0xFFF3F5F9u;
    const u32 insetC    = dark ? 0xFF15181Eu : 0xFFDCE0E8u;
    const u32 selC      = dark ? 0xFF272C36u : 0xFFFFFFFFu;
    const u32 hoverC    = dark ? 0xFF20242Cu : 0xFFE0E4EBu;
    const u32 textC     = dark ? 0xFFF2F4F8u : 0xFF1B1E25u;
    const u32 softC     = dark ? 0xFFD5D9E2u : 0xFF2B303Au;
    const u32 mutedC    = dark ? 0xFF8B92A3u : 0xFF5B6272u;
    const u32 dimC      = dark ? 0xFF5B6272u : 0xFF8B92A3u;
    const u32 accent    = dark ? 0xFF52E3A4u : 0xFF12A56Eu;
    const u32 accentDn  = dark ? 0xFF3FC48Cu : 0xFF0E8A5Cu;
    const u32 onAccent  = dark ? 0xFF08261Au : 0xFFFFFFFFu;
    const u32 amber     = dark ? 0xFFF2B84Au : 0xFFC98A12u;
    const u32 edgeDark  = dark ? 0x99050709u : 0x33000000u;
    const u32 edgeLight = dark ? 0x12FFFFFFu : 0xAAFFFFFFu;
    const u32 accentSoft = (accent & 0x00FFFFFFu) | 0x30000000u;

    const float titleFont = maxFloat(14.0f, 22.0f * s);
    const float bodyFont  = maxFloat(10.0f, 14.0f * s);
    const float smallFont = maxFloat(9.0f, 11.0f * s);

    const bool playing = mSnapshot.playing;
    const int selected = selectedGlobalIndex();

    // --- card -------------------------------------------------------------
    addRect(panelX + 3 * s, panelY + 7 * s, panelW, panelH,
            dark ? 0x55000000u : 0x33000000u, 24 * s);
    addRect(panelX, panelY, panelW, panelH, cardC, 22 * s);
    addRect(panelX + 24 * s, panelY + 1 * s, panelW - 48 * s, 1 * s,
            edgeLight, 0.5f * s);

    // --- header -----------------------------------------------------------
    addRect(panelX + 14 * s, panelY + 15 * s, 30 * s, 30 * s, accent, 9 * s);
    static constexpr float idleBars[3] = {7.0f, 13.0f, 9.0f};
    for (int bar = 0; bar < 3; ++bar) {
        const float level = clampFloat(mSpectrum[bar], 0.0f, 1.0f);
        const float height = playing ? 6.0f + level * 14.0f : idleBars[bar];
        addRect(panelX + (22 + bar * 6.5f) * s,
                panelY + (38 - height) * s, 4 * s, height * s,
                onAccent, 1.5f * s);
    }
    addText(panelX + 54 * s, panelY + 10 * s, titleFont, "LeviMusic", textC);
    addText(panelX + 54 * s, panelY + 38 * s, smallFont, "LOCAL PLAYER", mutedC);

    {
        const float chipW = 104 * s, chipH = 28 * s;
        const float chipX = panelX + panelW - 14 * s - chipW;
        const float chipY = panelY + 16 * s;
        const char* status = "Idle";
        u32 statusColor = mutedC;
        u32 dotColor = dimC;
        if (playing) { status = "Playing"; statusColor = accent; dotColor = accent; }
        else if (mSnapshot.hasMedia) { status = "Paused"; statusColor = amber; dotColor = amber; }
        addInset(chipX, chipY, chipW, chipH, 14 * s, s,
                 insetC, edgeDark, edgeLight);
        addCircle(chipX + 17 * s, chipY + 14 * s, 4 * s, dotColor);
        addTextCentered(chipX + 26 * s, chipY, chipW - 32 * s, chipH,
                        smallFont + 1 * s, status, statusColor);
    }

    // --- search / source / sort row ---------------------------------------
    const float rowY = panelY + SearchRowY * s;
    const float rowH = SearchRowH * s;
    {
        const float searchX = panelX + 14 * s;
        const float searchW = 258 * s;
        const bool searchOn = mSearchActive || mPressedControl == ControlSearch;
        if (searchOn)
            addRect(searchX - 1.5f * s, rowY - 1.5f * s,
                    searchW + 3 * s, rowH + 3 * s, accentSoft, 20.5f * s);
        addInset(searchX, rowY, searchW, rowH, 19 * s, s,
                 insetC, edgeDark, edgeLight);
        drawSearchIcon(searchX + 21 * s, rowY + 18 * s, 0.7f * s, mutedC, insetC);
        const float textX = searchX + 42 * s;
        float caretX = textX;
        if (mSearchText[0]) {
            addTextFlow(textX, rowY + 10 * s, bodyFont, mSearchText,
                        searchW - 84 * s, textC);
            caretX += minFloat(estimateTextWidth(mSearchText, bodyFont),
                               searchW - 84 * s);
            drawCloseIcon(searchX + searchW - 20 * s, rowY + 19 * s,
                          0.55f * s, mutedC);
        } else {
            addText(textX, rowY + 10 * s, bodyFont,
                    "Search songs, artists", mutedC);
        }
        if (mSearchActive && ((mAnimationFrame / 8) & 1) == 0)
            addRect(caretX + 1 * s, rowY + 9 * s, 1.5f * s, 20 * s,
                    accent, 0.75f * s);

        // Source chip: tap to cycle All music -> each folder.
        const float chipX = panelX + 280 * s;
        const float chipW = 140 * s;
        if (mPressedControl == ControlSourceCycle)
            addInset(chipX, rowY, chipW, rowH, 19 * s, s,
                     insetC, edgeDark, edgeLight);
        else
            addRaised(chipX, rowY, chipW, rowH, 19 * s, s,
                      raisedC, edgeDark, edgeLight);
        const char* sourceLabel = "All music";
        if (mActiveTab > 0) {
            const MusicSource* source = mLibrary->source(mActiveTab - 1);
            if (source) sourceLabel = source->label;
        }
        addCircle(chipX + 17 * s, rowY + 19 * s, 4 * s,
                  mActiveTab > 0 ? accent : mutedC);
        addTextFlow(chipX + 30 * s, rowY + 10 * s, bodyFont, sourceLabel,
                    chipW - 58 * s, softC);
        addTriangle(chipX + chipW - 23 * s, rowY + 16 * s,
                    chipX + chipW - 13 * s, rowY + 16 * s,
                    chipX + chipW - 18 * s, rowY + 23 * s, mutedC);

        // Sort chip.
        const float sortX = panelX + 428 * s;
        const float sortW = 118 * s;
        if (mPressedControl == ControlSort)
            addInset(sortX, rowY, sortW, rowH, 19 * s, s,
                     insetC, edgeDark, edgeLight);
        else
            addRaised(sortX, rowY, sortW, rowH, 19 * s, s,
                      raisedC, edgeDark, edgeLight);
        drawSortIcon(sortX + 18 * s, rowY + 19 * s, 0.6f * s, mutedC);
        const char* sortText = mSortMode == 0 ? "Custom" :
                               (mSortMode == 1 ? "Title A-Z" : "Artist A-Z");
        addText(sortX + 34 * s, rowY + 10 * s, bodyFont, sortText, softC);
    }

    // --- scrolling song list ----------------------------------------------
    const float listX = panelX + ListX * s;
    const float listY = panelY + ListY * s;
    const float listW = ListW * s;
    const float listH = ListH * s;
    const float rowW = (ListW - ScrollBarGutter) * s;
    addInset(panelX + 10 * s, listY - 5 * s, panelW - 20 * s, listH + 10 * s,
             16 * s, s, insetC, edgeDark, edgeLight);

    const float viewTop = listY;
    const float viewBottom = listY + listH;
    int firstRow = static_cast<int>(mScrollPx / RowPitch);
    if (firstRow < 0) firstRow = 0;
    for (int index = firstRow; index < mVisibleCount; ++index) {
        const float top = listY + (static_cast<float>(index) * RowPitch -
                                   mScrollPx) * s;
        if (top >= viewBottom) break;
        const float bottom = top + RowHeight * s;
        if (bottom <= viewTop) continue;
        const int globalIndex = mVisibleIndices[index];
        MusicTrack* mutableTrack = mLibrary->trackMutable(globalIndex);
        const MusicTrack* track = mutableTrack;
        if (!track) continue;
        const bool full = top >= viewTop && bottom <= viewBottom;
        const bool selectedRow = globalIndex == selected;
        const bool pressed = mPressedControl == ControlRowBase + index;

        // Rows that are cut by the list edge only show their background.
        const float clipTop = maxFloat(top, viewTop);
        const float clipBottom = minFloat(bottom, viewBottom);
        if (pressed || selectedRow)
            addRect(listX, clipTop, rowW, clipBottom - clipTop,
                    pressed ? hoverC : selC,
                    minFloat(12 * s, (clipBottom - clipTop) * 0.5f));
        if (!full) continue;
        if (selectedRow && mutableTrack) mMetadata->ensure(*mutableTrack);

        if (selectedRow)
            addRect(listX + 1 * s, top + 10 * s, 3 * s, 24 * s,
                    accent, 1.5f * s);
        if (selectedRow && playing) {
            for (int bar = 0; bar < 3; ++bar) {
                const float level = clampFloat(mSpectrum[bar], 0.0f, 1.0f);
                const float height = (5.0f + level * 14.0f) * s;
                addRect(listX + (14 + bar * 6) * s,
                        top + 30 * s - height, 3.5f * s, height,
                        accent, 1.5f * s);
            }
        } else {
            char number[12]{};
            snprintf(number, sizeof(number), "%d", index + 1);
            addTextCentered(listX + 4 * s, top, 34 * s, RowHeight * s,
                            smallFont, number, selectedRow ? accent : dimC);
        }
        const char* rowTitle = track->title[0] ? track->title : track->name;
        addTextFlow(listX + 46 * s, top + 6 * s, bodyFont, rowTitle,
                    rowW - 60 * s, selectedRow ? accent : textC);
        const char* rowArtist = track->artist[0] ? track->artist
                                                 : "Unknown artist";
        addTextFlow(listX + 46 * s, top + 25 * s, smallFont, rowArtist,
                    rowW - 60 * s, mutedC);
    }

    if (mVisibleCount == 0) {
        addTextCentered(listX, listY + 62 * s, listW, 30 * s, bodyFont + 2 * s,
                        mScanRunning ? "Scanning music folders..."
                                     : (mSearchText[0] ? "No matching songs"
                                                       : "No audio found"),
                        softC);
        addTextCentered(listX, listY + 96 * s, listW, 24 * s, smallFont,
                        mSearchText[0] ? "Try another search"
                                       : "Use IMPORT to add songs",
                        mutedC);
    } else if (maxScroll() > 0.5f) {
        const float content = static_cast<float>(mVisibleCount) * RowPitch;
        const float thumbH = minFloat(ListH, maxFloat(28.0f,
                                                      ListH * ListH / content));
        const float thumbY = (ListH - thumbH) * (mScrollPx / maxScroll());
        addRect(listX + listW - 6 * s, listY + thumbY * s, 3 * s,
                thumbH * s, (mScrolling || mFlingVel != 0.0f) ? accent : dimC,
                1.5f * s);
    }

    // --- player strip -----------------------------------------------------
    const float stripX = panelX + PlayerX * s;
    const float stripY = panelY + PlayerY * s;
    addRaised(stripX, stripY, PlayerW * s, PlayerH * s, 18 * s, s,
              raisedC, edgeDark, edgeLight);

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

    const float artX = stripX + 10 * s;
    const float artY = stripY + 9 * s;
    const float artSize = 44 * s;
    if (selectedTrack && selectedTrack->metadataState == 2 &&
        selectedTrack->artworkId[0]) {
        addImage(artX, artY, artSize, artSize, selectedTrack->artworkId,
                 0xFFFFFFFFu);
    } else {
        const u32 tile = trackAccent(songTitle);
        addRect(artX, artY, artSize, artSize, tile, 10 * s);
        addCircle(artX + 22 * s, artY + 22 * s, 15 * s,
                  dark ? 0xCC101010u : 0xCCFFFFFFu);
        addCircle(artX + 22 * s, artY + 22 * s, 4.5f * s, tile);
        addCircle(artX + 22 * s, artY + 22 * s, 2 * s,
                  dark ? 0xFF101010u : 0xFFFFFFFFu);
    }
    addTextFlow(stripX + 64 * s, stripY + 9 * s, bodyFont + 1 * s, songTitle,
                226 * s, textC);
    addTextFlow(stripX + 64 * s, stripY + 31 * s, smallFont, artist,
                226 * s, mutedC);

    // Transport controls.
    static const int controlIds[5] = {ControlShuffle, ControlTrackPrev,
                                      ControlPlayPause, ControlTrackNext,
                                      ControlLoop};
    const float controlY = panelY + ControlCenterY * s;
    const bool shuffleOn = mConfig->config().shuffleEnabled;
    const int loopMode = mConfig->config().loopMode;
    for (int i = 0; i < 5; ++i) {
        const float cx = panelX + ControlCenterX[i] * s;
        const float r = ControlRadius[i] * s;
        const bool pressed = mPressedControl == controlIds[i];
        const bool active = (controlIds[i] == ControlShuffle && shuffleOn) ||
                            (controlIds[i] == ControlLoop && loopMode != 0);
        if (controlIds[i] == ControlPlayPause) {
            addCircle(cx + 1 * s, controlY + 2.5f * s, r, edgeDark);
            addCircle(cx, controlY, r, pressed ? accentDn : accent);
        } else if (pressed) {
            addCircle(cx, controlY, r, insetC);
        } else if (active) {
            addCircle(cx, controlY, r, accentSoft);
        }
    }
    drawShuffleIcon(panelX + ControlCenterX[0] * s, controlY, 0.7f * s,
                    shuffleOn ? accent : softC);
    drawPreviousIcon(panelX + ControlCenterX[1] * s, controlY, 0.72f * s, softC);
    drawPlayPauseIcon(panelX + ControlCenterX[2] * s, controlY, 0.85f * s,
                      playing, onAccent);
    drawNextIcon(panelX + ControlCenterX[3] * s, controlY, 0.72f * s, softC);
    drawLoopIcon(panelX + ControlCenterX[4] * s, controlY, 0.7f * s, loopMode,
                 loopMode != 0 ? accent : softC);

    // Seek bar with elapsed / total time.
    const float barY = panelY + (PlayerY + 62.0f) * s;
    const float seekX = panelX + SeekX * s;
    const float seekW = SeekW * s;
    const float progress = mSnapshot.durationMs > 0
        ? clampFloat(static_cast<float>(mSnapshot.positionMs) /
                     static_cast<float>(mSnapshot.durationMs), 0.0f, 1.0f)
        : 0.0f;
    char elapsed[16]{};
    char duration[16]{};
    formatTime(mSnapshot.positionMs, elapsed, sizeof(elapsed));
    formatTime(mSnapshot.durationMs, duration, sizeof(duration));
    addText(stripX + 10 * s, barY - 7 * s, smallFont, elapsed, mutedC);
    addText(seekX + seekW + 8 * s, barY - 7 * s, smallFont, duration, mutedC);
    addRect(seekX, barY - 2 * s, seekW, 4 * s, insetC, 2 * s);
    if (progress > 0.0f)
        addRect(seekX, barY - 2 * s, maxFloat(4 * s, seekW * progress),
                4 * s, accent, 2 * s);
    addCircle(seekX + seekW * progress, barY,
              (mPressedControl == ControlSeek ? 7.0f : 5.0f) * s, textC);

    // Volume.
    const int volumePercent = mConfig->config().volumePercent;
    const float volume = volumePercent / 100.0f;
    const float volX = panelX + VolX * s;
    const float volW = VolW * s;
    drawVolumeIcon(panelX + 376 * s, barY, 0.7f * s, volumePercent == 0,
                   mPressedControl == ControlMute ? accent : mutedC);
    addRect(volX, barY - 2 * s, volW, 4 * s, insetC, 2 * s);
    if (volume > 0.0f)
        addRect(volX, barY - 2 * s, volW * volume, 4 * s,
                mPressedControl == ControlVolumeBar ? accent : softC, 2 * s);
    addCircle(volX + volW * volume, barY,
              (mPressedControl == ControlVolumeBar ? 6.0f : 4.0f) * s, textC);

    // The floating button is drawn last so it stays on top of the panel.
    drawFloatingButton();

    mApi->submitDrawCommands(ModuleId, strlen(ModuleId), mCommands,
                             static_cast<usize>(mCommandCount));
    mVisible = mCommandCount > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Floating headphone button
// ---------------------------------------------------------------------------

void NativeMusicUi::calculateButtonLayout(float& x, float& y, float& d) {
    const float screenW = static_cast<float>(mViewportWidth);
    const float screenH = static_cast<float>(mViewportHeight);
    const float scale = clampFloat(
        mConfig->config().uiScalePercent / 100.0f, 0.5f, 1.4f);
    d = maxFloat(56.0f, 68.0f * scale);
    if (!mBtnPositionValid) {
        if (mConfig->config().uiButtonPositionSaved) {
            mBtnX = static_cast<float>(mConfig->config().uiButtonX);
            mBtnY = static_cast<float>(mConfig->config().uiButtonY);
        } else {
            // Default: right edge, below the launcher's own round button.
            mBtnX = screenW * 0.957f - d * 0.5f;
            mBtnY = screenH * 0.26f - d * 0.5f;
        }
        mBtnPositionValid = true;
    }
    mBtnX = clampFloat(mBtnX, 0.0f, maxFloat(0.0f, screenW - d));
    mBtnY = clampFloat(mBtnY, 0.0f, maxFloat(0.0f, screenH - d));
    x = mBtnX;
    y = mBtnY;
}

void NativeMusicUi::saveButtonPosition() {
    mConfig->saveButtonPosition(static_cast<int>(mBtnX),
                                static_cast<int>(mBtnY), true);
}

void NativeMusicUi::drawHeadphoneIcon(float cx, float cy, float s, u32 color) {
    // Headband: upper half circle (radius 11) as 8 rounded segments.
    static const float cosv[9] = {-1.0f, -0.9239f, -0.7071f, -0.3827f, 0.0f,
                                  0.3827f, 0.7071f, 0.9239f, 1.0f};
    static const float sinv[9] = {0.0f, 0.3827f, 0.7071f, 0.9239f, 1.0f,
                                  0.9239f, 0.7071f, 0.3827f, 0.0f};
    const float R = 11.0f * s;
    const float top = cy - 1.0f * s;          // icon is ~23 units tall
    float px = 0.0f, py = 0.0f;
    for (int i = 0; i < 9; ++i) {
        const float x = cx + R * cosv[i];
        const float y = top - R * sinv[i] + 1.0f * s;
        addCircle(x, y, 1.6f * s, color);
        if (i > 0) addLine(px, py, x, y, 3.2f * s, color);
        px = x;
        py = y;
    }
    // Ear cups.
    addRect(cx - R - 3.6f * s, top + 0.5f * s, 7.2f * s, 11.0f * s, color,
            3.2f * s);
    addRect(cx + R - 3.6f * s, top + 0.5f * s, 7.2f * s, 11.0f * s, color,
            3.2f * s);
}

void NativeMusicUi::drawFloatingButton() {
    if (!mConfig || !mConfig->config().showFloatingButton) return;
    float bx{}, by{}, d{};
    calculateButtonLayout(bx, by, d);
    const bool dark = useDarkTheme();
    const float r = d * 0.5f;
    const float cx = bx + r;
    const float cy = by + r;
    const bool active = mPanelShown != 0;

    const u32 faceC   = dark ? 0xF21B1E25u : 0xF2E9ECF2u;
    const u32 ringC   = dark ? 0x30FFFFFFu : 0x40000000u;
    const u32 shadowC = dark ? 0x66000000u : 0x33000000u;
    const u32 textC   = dark ? 0xFFF2F4F8u : 0xFF1B1E25u;
    const u32 accent  = dark ? 0xFF52E3A4u : 0xFF12A56Eu;
    const u32 pressC  = dark ? 0x26FFFFFFu : 0x26000000u;

    addCircle(cx + 1.5f, cy + 3.0f, r + 1.0f, shadowC);
    addCircle(cx, cy, r, ringC);
    addCircle(cx, cy, r - 1.5f, faceC);
    if (mBtnPressed) addCircle(cx, cy, r - 1.5f, pressC);
    drawHeadphoneIcon(cx, cy + d * 0.02f, d * 0.02f,
                      active ? accent : textC);
    // Small dot when music is playing while the panel is hidden.
    if (!active && mSnapshot.playing)
        addCircle(cx + r * 0.62f, cy - r * 0.62f, d * 0.085f, accent);
}

bool NativeMusicUi::handleButtonTouch(int action, int pointerId,
                                      float x, float y) {
    if (!mConfig || !mConfig->config().showFloatingButton) return false;
    const bool isDown = action == 0 || action == 5;
    // Fast path without locking: only a finger-down can start a button
    // gesture, afterwards only that pointer's events matter.
    if (!isDown && (mBtnPointerId < 0 ||
                    (action != 3 && pointerId != mBtnPointerId)))
        return false;

    ScopedUiBusy guard(&mUiBusy, true);
    if (!mOpen) return false;
    float bx{}, by{}, d{};
    calculateButtonLayout(bx, by, d);
    const float r = d * 0.5f;
    const float cx = bx + r;
    const float cy = by + r;
    const float dx = x - cx;
    const float dy = y - cy;
    const bool inside = dx * dx + dy * dy <= r * r * 1.21f;

    if (isDown) {
        if (mBtnPointerId >= 0 || !inside) return false;
        mBtnPointerId = pointerId;
        mBtnPressed = true;
        mBtnMoved = false;
        mBtnDownX = x;
        mBtnDownY = y;
        mBtnDragOffX = x - bx;
        mBtnDragOffY = y - by;
        renderUnlocked();
        return true;
    }
    if (mBtnPointerId < 0) return false;

    if (action == 3) {   // cancelled: forget the gesture, let others see it
        const bool moved = mBtnMoved;
        mBtnPointerId = -1;
        mBtnPressed = false;
        mBtnMoved = false;
        if (moved) saveButtonPosition();
        renderUnlocked();
        return false;
    }
    if (pointerId != mBtnPointerId) return false;

    if (action == 2) {
        const float mx = x - mBtnDownX;
        const float my = y - mBtnDownY;
        const float threshold = maxFloat(12.0f, d * 0.2f);
        if (!mBtnMoved && mx * mx + my * my > threshold * threshold)
            mBtnMoved = true;
        if (mBtnMoved) {
            mBtnX = x - mBtnDragOffX;
            mBtnY = y - mBtnDragOffY;
            renderUnlocked();
        }
        return true;
    }
    if (action == 1 || action == 6) {
        const bool moved = mBtnMoved;
        mBtnPointerId = -1;
        mBtnPressed = false;
        mBtnMoved = false;
        if (moved) {
            saveButtonPosition();
            renderUnlocked();
        } else if (inside) {
            setPanelShownUnlocked(mPanelShown == 0);
        } else {
            renderUnlocked();
        }
        return true;
    }
    return true;
}

// Caller holds mUiBusy.
void NativeMusicUi::setPanelShownUnlocked(bool shown) {
    if (!mOpen) return;
    if (shown == (mPanelShown != 0)) {
        renderUnlocked();
        return;
    }
    if (!shown) {
        if (mPanelPositionValid) savePanelUiState();
        setSearchActive(false);
        mPanelShown = 0;
        mVisible = 0;
        mDragging = false;
        mDragPointerId = -1;
        mPressedControl = 0;
        mScrollTouching = false;
        mScrolling = false;
        mScrollPointerId = -1;
        mFlingVel = 0.0f;
        mCapturedPointerMask = 0;
        mLastBtnSig = -1;
    } else {
        mPanelShown = 1;
        refreshViewport();
        mPlayer->snapshot(mSnapshot);
        mLastUiSig = -1;
        markFilterDirty();
    }
    renderUnlocked();
}

void NativeMusicUi::onFloatingButtonSettingChanged() {
    ScopedUiBusy guard(&mUiBusy, true);
    if (!mOpen) return;
    // Without the button there would be no way to bring a hidden panel back.
    if (!mConfig->config().showFloatingButton && !mPanelShown) {
        setPanelShownUnlocked(true);
        return;
    }
    renderUnlocked();
}
