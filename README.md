# LeviMusic

In-game local music player for LeviLaunchroid (Minecraft Bedrock, Android, ARM64).
Built on the OfflineMusicPlayer V1.6.0 engine and UI, rebranded and verified
against the current `libpreloader.so`.

## Features
- Native in-game player panel (drag to move, scale, opacity, dark/light/system theme)
- Folder scanning: `Music`, `Download`, `Downloads` + your own `music_directories`;
  every non-empty folder becomes a playlist tab; hot-reloads when the config changes
- Play / pause / stop / next / previous, seek bar, volume + mute
- Shuffle, loop off / all / one (scoped to the playlist playback started in)
- Search, sort (scan order, title, artist), paging
- Embedded album artwork and ID3 title/artist
- Real FFT spectrum (needs RECORD_AUDIO) or a permission-free animated fallback
- Mod Menu settings: volume, UI opacity/scale, auto-resolution scale, UI mode,
  library limit, loop, shuffle, Pause/Stop buttons
- Panel position, page, sort and playlist are remembered
- Never autoplays on game start

## Config
`config/config.json` inside the mod folder (schema: `config.schema.json`):

```json
{
  "music_directories": ["/storage/emulated/0/Music", "/storage/emulated/0/Download"],
  "ui_mode": 0,
  "volume_percent": 80
}
```

## Build
Needs the Android NDK r28 (28.2.13676358).

```sh
export ANDROID_NDK_ROOT=/path/to/ndk/28.2.13676358
./package.sh          # -> LeviMusic_V1.0.0.levipack
```

GitHub Actions (`.github/workflows/android.yml`) does the same and uploads
the `.levipack` and the raw `.so`.

## Preloader compatibility
The mod mirrors a few preloader structs in `include/LeviAbi.hpp` and talks to
`libpreloader.so` through `dlsym`, so it has no SDK or libminecraftpe link-time
dependency. After a LeviLaunchroid update run:

```sh
tools/verify_preloader.sh /path/to/libpreloader.so
```

Last verified: build id `5790e73466e108098540d65efd7898e2c1a0490a`
(see `compat/verified-target.json`).

## Layout
- `src/main.cpp` – mod entry (`PLGetModRegistration`)
- `src/LeviMusicMod.cpp` – lifecycle and Mod Menu setting handling
- `src/AndroidMediaPlayer.cpp`, `src/JniBridge.cpp` – JNI MediaPlayer/Visualizer
- `src/MusicLibrary.cpp`, `src/MediaMetadata.cpp` – scanning, tags, artwork
- `src/NativeMusicUi.cpp` – HUD, input, playback queue
- `src/LeviAbi.cpp`, `src/Mod*Hook.cpp`, `src/Compatibility.cpp` – preloader glue
- `resources/ui/*.rgba` – panel textures (packed into the `.levipack`)

## Credits
Engine and UI derived from OfflineMusicPlayer by TheRealBatu.

## License
License information will be added after reviewing reused components.
