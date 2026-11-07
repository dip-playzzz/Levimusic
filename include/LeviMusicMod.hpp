#pragma once

#include "AndroidMediaPlayer.hpp"
#include "Compatibility.hpp"
#include "Config.hpp"
#include "LeviAbi.hpp"
#include "MediaMetadata.hpp"
#include "ModMenuConfigHook.hpp"
#include "ModMenuToggleHook.hpp"
#include "MusicLibrary.hpp"
#include "NativeMusicUi.hpp"
#include "VanillaUiAssets.hpp"

class LeviMusicMod {
public:
    bool load(void* context);
    bool enable(void* context);
    bool disable(void* context);
    bool unload(void* context);

private:
    static void settingsChanged(const char* key, const char* value);
    void applySetting(const char* key, const char* value);
    void applyPlaybackConfiguration();

    leviabi::Api mApi{};
    ConfigManager mConfig{};
    MusicLibrary mLibrary{};
    AndroidMediaPlayer mPlayer{};
    MediaMetadata mMetadata{};
    VanillaUiAssets mUiAssets{};
    NativeMusicUi mUi{};
    ModMenuToggleHook mToggleHook{};
    ModMenuConfigHook mConfigHook{};
    bool mLoaded{};
    bool mEnabled{};
};

LeviMusicMod& leviMusicMod();
