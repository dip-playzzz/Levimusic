#include "MusicConfig.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: levi_music_config_gen <mod-package-dir>\n";
    return 2;
  }

  const std::filesystem::path packageDir(argv[1]);
  const auto configDir = packageDir / "config";
  pl::config::ConfigFile<levimusic::MusicConfig> config(
      levimusic::MusicConfig{}, configDir / "config.json",
      configDir / "config.schema.json");

  if (!config.save()) {
    std::cerr << "failed to write " << config.configPath() << '\n';
    return 1;
  }
  if (!config.writeSchema()) {
    std::cerr << "failed to write " << config.schemaPath() << '\n';
    return 1;
  }

  std::cout << "generated " << config.configPath() << '\n';
  std::cout << "generated " << config.schemaPath() << '\n';
  return 0;
}
