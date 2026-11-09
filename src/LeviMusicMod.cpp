#include "LeviMusicMod.hpp"
#include "JniBridge.hpp"

static LeviMusicMod gLeviMusicMod;
static constexpr const char* LogTag = "LeviMusic";

LeviMusicMod& leviMusicMod() { return gLeviMusicMod; }

static int parseSettingInt(const char* value, int fallback) {
    if (!value || !*value) return fallback;
    int result = 0;
    bool any = false;
    for (int i = 0; value[i] >= '0' && value[i] <= '9'; ++i) {
        any = true;
        result = result * 10 + (value[i] - '0');
    }
    return any ? result : fallback;
}

static bool parseSettingBool(const char* value, bool fallback) {
    if (!value) return fallback;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        strcmp(value, "on") == 0 || strcmp(value, "enabled") == 0) return true;
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "disabled") == 0) return false;
    return fallback;
}


void LeviMusicMod::settingsChanged(const char* key, const char* value) {
    leviMusicMod().applySetting(key, value);
}

void LeviMusicMod::applyPlaybackConfiguration() {
    const LeviMusicConfig& config = mConfig.config();
    mPlayer.setVolume(config.volumePercent / 100.0f);
    mPlayer.setLooping(false);
}

void LeviMusicMod::applySetting(const char* key, const char* value) {
    if (!key || !mEnabled) return;

    if (strcmp(key, "volume_percent") == 0) {
        mConfig.setVolumePercent(parseSettingInt(
            value, mConfig.config().volumePercent));
        mPlayer.setVolume(mConfig.config().volumePercent / 100.0f);
    } else if (strcmp(key, "ui_opacity_percent") == 0) {
        mConfig.setUiOpacityPercent(parseSettingInt(
            value, mConfig.config().uiOpacityPercent));
        mUi.redraw();
    } else if (strcmp(key, "ui_scale_percent") == 0) {
        mConfig.setUiScalePercent(parseSettingInt(
            value, mConfig.config().uiScalePercent));
        mUi.redraw();
    } else if (strcmp(key, "auto_resolution_scale") == 0) {
        mConfig.setAutoResolutionScale(parseSettingBool(
            value, mConfig.config().autoResolutionScale));
        mUi.redraw();
    } else if (strcmp(key, "ui_mode") == 0) {
        mConfig.setUiMode(parseSettingInt(value, mConfig.config().uiMode));
        mUi.redraw();
    } else if (strcmp(key, "max_tracks") == 0) {
        mConfig.setMaxTracks(parseSettingInt(
            value, mConfig.config().maxTracks));
        mUi.refresh();
    } else if (strcmp(key, "loop_mode") == 0) {
        mConfig.setLoopMode(parseSettingInt(value, mConfig.config().loopMode));
        mPlayer.setLooping(false);
        mUi.redraw();
    } else if (strcmp(key, "shuffle_enabled") == 0) {
        mConfig.setShuffleEnabled(parseSettingBool(
            value, mConfig.config().shuffleEnabled));
        mUi.redraw();
    } else if (strcmp(key, "pause_playback") == 0) {
        mPlayer.pause();
        mUi.redraw();
    } else if (strcmp(key, "stop_playback") == 0) {
        mPlayer.stop();
        mUi.redraw();
    }
}

// Reads a libc++ std::string (short or long form) without linking libc++.
static bool readStdString(const void* address, char* out, usize capacity) {
    if (!address || capacity < 2) return false;
    const u8* bytes = static_cast<const u8*>(address);
    usize size = 0;
    const char* data = nullptr;
    if (bytes[0] & 1u) {              // long form: {cap|1, size, data*}
        memcpy(&size, bytes + 8, sizeof(size));
        memcpy(&data, bytes + 16, sizeof(data));
    } else {                          // short form: size in the first byte
        size = bytes[0] >> 1;
        data = reinterpret_cast<const char*>(bytes + 1);
    }
    if (!data || size == 0 || size >= capacity || size > 46) return false;
    memcpy(out, data, size);
    out[size] = 0;
    return true;
}

bool LeviMusicMod::load(void* context) {
    if (mLoaded) return true;
    if (!jni::captureFromModContext(context)) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "failed to capture JavaVM from Levi ModContext");
        return false;
    }

    mConfig.initializePaths();
    {
        char logPath[640]{};
        copyString(logPath, sizeof(logPath), mConfig.modRoot());
        appendString(logPath, sizeof(logPath), "/levimusic.log");
        levi_log_set_path(logPath);
    }
    levi_log(LOG_INFO_VALUE, LogTag, "load: mod root %s", mConfig.modRoot());
    {
        // ModContext layout (verified from the preloader's DWARF):
        // mJavaVm @0, mInfo @8, ModInfo::id (std::string) @ mInfo+0.
        char ownerId[48]{};
        if (readStdString(static_cast<const u8*>(context) + 8, ownerId,
                          sizeof(ownerId))) {
            leviabi::setOwnerModId(ownerId);
            levi_log(LOG_INFO_VALUE, LogTag, "load: owner mod id '%s'", ownerId);
        } else {
            // Fall back to the install folder name, which LeviLaunchroid uses
            // as the mod id for this package layout.
            leviabi::setOwnerModId("LeviMusic");
            levi_log(LOG_ERROR_VALUE, LogTag,
                     "load: could not read owner mod id from context; using fallback 'LeviMusic'");
        }
    }
    mConfig.load();
    levi_log(LOG_INFO_VALUE, LogTag, "load: scanning library");
    mLibrary.scan(mConfig.config());
    levi_log(LOG_INFO_VALUE, LogTag, "load: library has %d tracks",
             mLibrary.count());
    mLoaded = true;
    levi_log(LOG_INFO_VALUE, LogTag,
                        "loaded config: %s", mConfig.configPath());
    return true;
}

bool LeviMusicMod::enable(void* context) {
    if (mEnabled) return true;
    if (!mLoaded && !load(context)) return false;

    levi_log(LOG_INFO_VALUE, LogTag, "enable: begin");
    if (!leviabi::resolveApi(mApi)) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "required Levi preloader APIs unavailable");
        return false;
    }
    const char* missingRuntimeSymbol = nullptr;
    if (!compatibility::validateRuntime(mApi.preloader, missingRuntimeSymbol)) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "preloader ABI validation failed, missing export: %s",
                            missingRuntimeSymbol ? missingRuntimeSymbol : "unknown symbol");
        if (mApi.preloader) dlclose(mApi.preloader);
        memset(&mApi, 0, sizeof(mApi));
        return false;
    }
    levi_log(LOG_INFO_VALUE, LogTag,
                        "runtime preloader ABI validation passed");
    levi_log(LOG_INFO_VALUE, LogTag, "enable: registering UI textures");
    if (!mUiAssets.initialize(&mApi, mConfig)) {
        levi_log(LOG_ERROR_VALUE, LogTag,
                            "failed to initialize vanilla UI texture assets");
        return false;
    }
    levi_log(LOG_INFO_VALUE, LogTag, "enable: media player");
    if (!mPlayer.initialize(jni::javaVm())) return false;
    // Never restore or autoplay a previous track when Minecraft starts.
    mPlayer.stop();
    applyPlaybackConfiguration();
    levi_log(LOG_INFO_VALUE, LogTag, "enable: metadata");
    if (!mMetadata.initialize(jni::javaVm(), &mApi)) {
        mPlayer.shutdown();
        return false;
    }
    levi_log(LOG_INFO_VALUE, LogTag, "enable: ui");
    if (!mUi.initialize(&mApi, &mConfig, &mLibrary, &mPlayer,
                        &mMetadata, &mUiAssets)) {
        mMetadata.shutdown();
        mPlayer.shutdown();
        return false;
    }
    levi_log(LOG_INFO_VALUE, LogTag, "enable: hooks");
    if (!mToggleHook.initialize(&mApi, &mUi)) {
        mUi.shutdown();
        mMetadata.shutdown();
        mPlayer.shutdown();
        return false;
    }
    if (!mConfigHook.initialize(&mApi, &LeviMusicMod::settingsChanged)) {
        mToggleHook.shutdown();
        mUi.shutdown();
        mMetadata.shutdown();
        mPlayer.shutdown();
        return false;
    }

    mEnabled = true;
    mUi.forceDisabledDefault();
    levi_log(LOG_INFO_VALUE, LogTag,
                        "enabled; module is disabled by default and UI touch capture is active only while visible");
    return true;
}

bool LeviMusicMod::disable(void*) {
    if (!mEnabled) return true;
    mConfigHook.shutdown();
    mToggleHook.shutdown();
    mUi.shutdown();
    mConfig.flushAutoSave();
    mPlayer.stop();
    mMetadata.shutdown();
    mPlayer.shutdown();
    if (mApi.preloader) dlclose(mApi.preloader);
    memset(&mApi, 0, sizeof(mApi));
    mEnabled = false;
    levi_log(LOG_INFO_VALUE, LogTag,
                        "disabled and released media-player resources");
    return true;
}

bool LeviMusicMod::unload(void* context) {
    disable(context);
    mLoaded = false;
    return true;
}
