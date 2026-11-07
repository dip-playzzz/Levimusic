#include "VanillaUiAssets.hpp"

static constexpr const char* LogTag = "LeviMusic";

bool VanillaUiAssets::registerRaw(const char* relativePath, const char* imageId,
                                  int width, int height) {
    if (!mApi || !mApi->registerImage || !relativePath || !imageId ||
        width <= 0 || height <= 0) {
        return false;
    }

    char path[768]{};
    copyString(path, sizeof(path), mResourceRoot);
    appendString(path, sizeof(path), "/");
    appendString(path, sizeof(path), relativePath);

    const usize expected = static_cast<usize>(width) *
                           static_cast<usize>(height) * 4u;
    u8* pixels = static_cast<u8*>(malloc(expected));
    if (!pixels) return false;

    const int fd = open(path, O_RDONLY_VALUE);
    if (fd < 0) {
        free(pixels);
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "vanilla UI texture missing: %s", path);
        return false;
    }

    usize offset = 0;
    while (offset < expected) {
        const long got = read(fd, pixels + offset, expected - offset);
        if (got <= 0) break;
        offset += static_cast<usize>(got);
    }
    close(fd);

    bool registered = false;
    if (offset == expected) {
        registered = mApi->registerImage(
            imageId, strlen(imageId), pixels, expected, width, height);
    }
    free(pixels);

    if (!registered) {
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "failed to register vanilla UI texture: %s", imageId);
    }
    return registered;
}

bool VanillaUiAssets::initialize(leviabi::Api* api,
                                 const ConfigManager& config) {
    mApi = api;
    copyString(mResourceRoot, sizeof(mResourceRoot), config.modRoot());
    appendString(mResourceRoot, sizeof(mResourceRoot), "/resources");

    const bool ok =
        registerRaw("ui/panel.rgba", PanelId, 900, 560) &&
        registerRaw("ui/titlebar.rgba", TitlebarId, 864, 54) &&
        registerRaw("ui/row.rgba", RowId, 852, 44) &&
        registerRaw("ui/button.rgba", ButtonId, 128, 40) &&
        registerRaw("ui/focus_row.rgba", FocusRowId, 852, 44) &&
        registerRaw("ui/focus_button.rgba", FocusButtonId, 128, 40) &&
        registerRaw("ui/chevron_left.rgba", ChevronLeftId, 16, 28) &&
        registerRaw("ui/chevron_right.rgba", ChevronRightId, 16, 28);

    mReady = ok;
    if (mReady) {
        __android_log_print(LOG_INFO_VALUE, LogTag,
                            "registered Mojang vanilla UI textures");
    }
    return mReady;
}
