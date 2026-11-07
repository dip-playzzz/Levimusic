#pragma once

#include "Config.hpp"
#include "LeviAbi.hpp"

class VanillaUiAssets {
public:
    bool initialize(leviabi::Api* api, const ConfigManager& config);
    bool ready() const { return mReady; }

    static constexpr const char* PanelId = "lm_panel";
    static constexpr const char* TitlebarId = "lm_titlebar";
    static constexpr const char* RowId = "lm_row";
    static constexpr const char* ButtonId = "lm_button";
    static constexpr const char* FocusRowId = "lm_focus";
    static constexpr const char* FocusButtonId = "lm_focus_btn";
    static constexpr const char* ChevronLeftId = "lm_left";
    static constexpr const char* ChevronRightId = "lm_right";

private:
    bool registerRaw(const char* relativePath, const char* imageId,
                     int width, int height);

    leviabi::Api* mApi{};
    char mResourceRoot[512]{};
    bool mReady{};
};
