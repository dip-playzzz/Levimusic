#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <pl/Config.hpp>

namespace levimusic {

enum class DisplayMode {
  Compact,
  Detailed,
  Debug,
};

struct MusicConfig {
  int version = 1;
  bool showOverlay = true;
  int opacity = 80;
  double scale = 1.0;
  DisplayMode mode = DisplayMode::Compact;
  std::string accentColor = "#4AE0A0";
};

inline constexpr int kMinOpacity = 0;
inline constexpr int kMaxOpacity = 100;
inline constexpr double kMinScale = 0.5;
inline constexpr double kMaxScale = 2.0;
inline constexpr std::string_view kModeMenuOptions = "Compact,Detailed,Debug";

} // namespace levimusic

namespace pl::config {

template <> struct Schema<levimusic::MusicConfig> {
  static constexpr std::string_view title = "LeviMusic Configuration";
  static constexpr std::string_view description =
      "Configuration shared by LeviMusic and its schema generator.";

  static constexpr FieldSchema field(std::string_view name) {
    if (name == "version") {
      return {"Version", "Config schema version managed by the mod.",
              std::nullopt, std::nullopt, true};
    }
    if (name == "showOverlay") {
      return {"Show Overlay",
              "Controls whether the LeviMusic overlay should be shown.",
              std::nullopt, std::nullopt, false};
    }
    if (name == "opacity") {
      return {"Opacity", "Overlay opacity percentage.",
              levimusic::kMinOpacity, levimusic::kMaxOpacity, false};
    }
    if (name == "scale") {
      return {"Scale", "Overlay scale multiplier.", levimusic::kMinScale,
              levimusic::kMaxScale, false};
    }
    if (name == "mode") {
      return {"Display Mode", "Overlay detail level.", std::nullopt,
              std::nullopt, false};
    }
    if (name == "accentColor") {
      return {"Accent Color",
              "Overlay accent color in #RRGGBB or #AARRGGBB form.",
              std::nullopt, std::nullopt, false};
    }
    return {};
  }
};

} // namespace pl::config
