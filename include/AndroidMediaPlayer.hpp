#pragma once

#include "JniBridge.hpp"
#include "Platform.hpp"

class AndroidMediaPlayer {
public:
    struct Snapshot {
        bool hasMedia{};
        bool playing{};
        bool paused{};
        int durationMs{};
        int positionMs{};
        float volume{};
        char path[512]{};
    };

    bool initialize(jni::Vm* vm);
    void shutdown();
    bool play(const char* path);
    bool pause();
    bool resume();
    void stop();
    bool seekToMs(int positionMs);
    bool setVolume(float volume);
    bool setLooping(bool enabled);
    bool captureSpectrum(float* bars, int count);
    void snapshot(Snapshot& out);

    bool isPlaying();
    int durationMs();
    int positionMs();
    bool available() const { return mReady; }
    bool hasMedia() const { return mPrepared; }
    bool paused() const { return mPaused; }
    float volume() const { return mVolume; }
    const char* currentPath() const { return mCurrentPath; }

private:
    bool ensurePlayer(jni::Env* env);
    bool ensureVisualizer(jni::Env* env);
    void releaseVisualizer(jni::Env* env);
    bool invoke(jni::Env* env, jni::JMethodId method,
                const jni::JValue* arguments, const char* operation);
    bool queryBoolean(jni::Env* env, jni::JMethodId method,
                      const char* operation, bool fallback);
    int queryInteger(jni::Env* env, jni::JMethodId method,
                     const char* operation, int fallback);
    void resetPlayer(jni::Env* env);
    void releasePlayerObject(jni::Env* env);
    void clearMethodCache();
    void lockState();
    void unlockState();

    jni::Vm* mVm{};
    jni::JObject mPlayer{};
    jni::JObject mVisualizer{};
    jni::JObject mFftArray{};
    jni::JMethodId mReset{};
    jni::JMethodId mSetDataSource{};
    jni::JMethodId mPrepare{};
    jni::JMethodId mStart{};
    jni::JMethodId mPause{};
    jni::JMethodId mIsPlaying{};
    jni::JMethodId mGetDuration{};
    jni::JMethodId mGetCurrentPosition{};
    jni::JMethodId mSeekTo{};
    jni::JMethodId mSetVolume{};
    jni::JMethodId mSetLooping{};
    jni::JMethodId mGetAudioSessionId{};
    jni::JMethodId mRelease{};
    jni::JMethodId mVisualizerSetEnabled{};
    jni::JMethodId mVisualizerGetFft{};
    jni::JMethodId mVisualizerRelease{};
    int mCaptureSize{};
    bool mReady{};
    bool mPrepared{};
    bool mPaused{};
    bool mLooping{};
    bool mVisualizerReady{};
    bool mVisualizerPermissionChecked{};
    bool mRealVisualizerAllowed{};
    bool mFakeVisualizerActive{};
    bool mVisualizerFallbackLogged{};
    float mVolume{0.8f};
    float mSpectrum[32]{};
    char mCurrentPath[512]{};
    volatile int mStateLock{};
};
