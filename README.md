# LeviMusic

An in-game music player mod for LeviLauncher on Android.

## Planned Features
- Local MP3 playback
- Online music search and playback
- Play, pause, next and previous
- Volume and seek controls
- Song title, artist and album artwork
- Playback queue and playlists
- Floating in-game music HUD
- Android audio integration

## Project Structure

- `.github/workflows/android.yml` — Android build workflow
- `CMakeLists.txt` — Native build configuration
- `manifest.json` — LeviLauncher mod manifest
- `src/LeviMusic.cpp` — Native mod implementation
- `src/MusicConfig.hpp` — Configuration definition
- `src/GenerateConfig.cpp` — Config generator

## Technology

- C++20
- Android NDK 28.2.13676358
- CMake and Ninja
- LeviLauncher Preloader SDK
- GitHub Actions

## Build Target

- ABI: `arm64-v8a`
- Minimum Android API: 28
- Output: `liblevi_music.so`

## Development Status

Native project scaffold and build pipeline preparation.

Music playback, Android audio integration, online streaming and the in-game HUD are not implemented yet.

## License

License information will be added after reviewing the project dependencies and reused components.
