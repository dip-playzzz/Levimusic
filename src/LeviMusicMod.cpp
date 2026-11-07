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

bool LeviMusicMod::load(void* context) {
    if (mLoaded) return true;
    if (!jni::captureFromModContext(context)) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "failed to capture JavaVM from Levi ModContext");
        return false;
    }

    mConfig.initializePaths();
    mConfig.load();
    mLibrary.scan(mConfig.config());
    mLoaded = true;
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "loaded config: %s", mConfig.configPath());
    return true;
}

bool LeviMusicMod::enable(void* context) {
    if (mEnabled) return true;
    if (!mLoaded && !load(context)) return false;

    if (!leviabi::resolveApi(mApi)) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "required Levi preloader APIs unavailable");
        return false;
    }
    const char* missingRuntimeSymbol = nullptr;
    if (!compatibility::validateRuntime(mApi.preloader, missingRuntimeSymbol)) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "preloader ABI validation failed, missing export: %s",
                            missingRuntimeSymbol ? missingRuntimeSymbol : "unknown symbol");
        if (mApi.preloader) dlclose(mApi.preloader);
        memset(&mApi, 0, sizeof(mApi));
        return false;
    }
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "runtime preloader ABI validation passed");
    if (!mUiAssets.initialize(&mApi, mConfig)) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "failed to initialize vanilla UI texture assets");
        return false;
    }
    if (!mPlayer.initialize(jni::javaVm())) return false;
    // Never restore or autoplay a previous track when Minecraft starts.
    mPlayer.stop();
    applyPlaybackConfiguration();
    if (!mMetadata.initialize(jni::javaVm(), &mApi)) {
        mPlayer.shutdown();
        return false;
    }
    if (!mUi.initialize(&mApi, &mConfig, &mLibrary, &mPlayer,
                        &mMetadata, &mUiAssets)) {
        mMetadata.shutdown();
        mPlayer.shutdown();
        return false;
    }
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
    __android_log_print(LOG_INFO_VALUE, LogTag,
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
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "disabled and released media-player resources");
    return true;
}

bool LeviMusicMod::unload(void* context) {
    disable(context);
    mLoaded = false;
    return true;
}
