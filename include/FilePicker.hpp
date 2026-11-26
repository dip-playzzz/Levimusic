#pragma once
#include "Platform.hpp"

// Opens Android's system file picker (Google Files) from the native mod and
// copies the chosen songs into the import folder. The Java side lives in
// java/com/levimusic/picker and is embedded as a dex (see tools/build_picker_dex.sh).
namespace filepicker {

enum State {
    StateIdle = 0,
    StatePicking = 1,
    StateCopying = 2,
    StateFinished = 3,
    StateError = 4,
    StateUnavailable = 5,   // dex missing or could not be loaded
};

struct Status {
    int state;
    int done;
    int total;
    int failed;
};

// Loads the embedded dex and starts tracking the foreground activity.
bool init(const char* importDirectory);
bool isReady();
// Opens the system picker; false if it could not be started.
bool open();
Status poll();
// True exactly once after an import has finished.
bool takeFinished();

} // namespace filepicker
