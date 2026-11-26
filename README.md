# LeviMusic

In-game local music player for LeviLaunchroid (Minecraft Bedrock, Android, ARM64).
Built on the OfflineMusicPlayer V1.6.0 engine and UI, rebranded and verified
against the current `libpreloader.so`.

## Features
- Compact single-card player (560x410 base units) in the LeviVC style: soft dark
  surfaces, mint accent, status chip, drag the header to move it
- Scrollable song list (drag to scroll, momentum fling, scroll indicator); the
  source chip cycles All music / each folder, the sort chip cycles the order
- Redraws only when something changes (about 7 fps while playing, once on
  state changes) instead of 20 times a second, so the game keeps its FPS
- Theme, scale and opacity options (dark/light/system theme)
- Folder scanning: `Music`, `Download`, `Downloads` + your own `music_directories`;
  every non-empty folder becomes a playlist tab; hot-reloads when the config changes
- Play / pause / stop / next / previous, seek bar, volume + mute
- Shuffle, loop off / all / one (scoped to the playlist playback started in)
- Search, sort (scan order, title, artist), paging
- Embedded album artwork and ID3 title/artist
- Real FFT spectrum (needs RECORD_AUDIO) or a permission-free animated fallback
- Mod Menu settings: volume, UI opacity/scale, auto-resolution scale, UI mode,
  library limit, loop, shuffle, Pause/Stop and Import Songs buttons
- Panel position, page, sort and playlist are remembered
- Never autoplays on game start

## Importing songs
Open the **LeviMusic module** in the Mod Menu and press **Import Songs (Google
Files)**. Android's own file picker (Google Files / Documents UI) opens on top of
the game:

- Pick one or many audio files (mp3, m4a, aac, ogg, opus, wav, flac, ...).
- They are copied into `/storage/emulated/0/Android/media/<launcher package>/LeviMusic`
  (falls back to `<mod folder>/music`). That folder is always scanned, needs no
  storage permission, and survives mod re-imports and launcher updates.
- When the copy finishes the library refreshes by itself; check `levimusic.log`
  for `import finished`. Existing files are never overwritten (copies get `(2)`).

How it works: a tiny Java helper (`java/com/levimusic/picker`) is compiled to a
dex during the build, embedded into the `.so`, and loaded at runtime with
`InMemoryDexClassLoader`. It adds a headless Fragment to the running activity to
receive the picker result. If the dex is missing the module still works and the
log says `system file picker unavailable`.

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

## Troubleshooting
The mod writes a log next to its library, in the same folder as `config/`:
`<mod folder>/levimusic.log` (also sent to logcat under the tag `LeviMusic`).
The first lines of a session show the build time (`build <date> <time>`), so
you can confirm which build is installed. Every load/enable step is logged, so the last line shows where something
failed or hung. Send that file when reporting a problem.

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
