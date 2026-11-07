#pragma once

#include "JniBridge.hpp"
#include "LeviAbi.hpp"
#include "MusicLibrary.hpp"

class MediaMetadata {
public:
    bool initialize(jni::Vm* vm, leviabi::Api* api);
    void shutdown();
    void ensure(MusicTrack& track);

private:
    bool extractString(jni::Env* env, jni::JObject retriever,
                       jni::JMethodId extractMethod, int key,
                       char* out, usize outSize);
    bool decodeArtwork(jni::Env* env, jni::JByteArray picture,
                       const char* imageId);
    bool knownArtwork(u32 hash) const;
    void rememberArtwork(u32 hash);
    u32 hashPath(const char* path) const;

    jni::Vm* mVm{};
    leviabi::Api* mApi{};
    u32 mRegisteredHashes[128]{};
    int mRegisteredCount{};
    bool mReady{};
};
