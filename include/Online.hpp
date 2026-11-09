#pragma once

#include "AndroidMediaPlayer.hpp"
#include "MusicLibrary.hpp"

// Online playback for LeviMusic.
//
//  * Search: YouTube Music-style InnerTube /search (WEB client) -> "Online"
//    playlist of "yt:<videoId>" tracks. Typing an http(s) URL adds it directly.
//  * Play:   InnerTube /player (VISIONOS client, pre-signed URLs, no JS/PO
//    token) -> audio is downloaded in bounded Range chunks into a cache file
//    and handed to the normal Android MediaPlayer, so seeking, duration and
//    loop logic behave exactly like local files. "url:" tracks stream directly.
//
// All network work runs on one worker thread; every call here returns at once.
namespace online {

bool start(AndroidMediaPlayer* player, const char* cacheDirectory);
void shutdown();

void search(const char* query);
void requestPlay(const char* path);   // path = "yt:<id>" or "url:<url>"
void cancel();                        // drop any pending/in-flight play request
bool isLoading(const char* path);     // true while `path` is being resolved/downloaded

// UI polling (cheap, thread-safe).
bool takeResults(OnlineEntry* out, int capacity, int* count);
void statusText(char* out, usize capacity);   // "" when idle

} // namespace online
