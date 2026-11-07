#include "AndroidMediaPlayer.hpp"

static constexpr const char* LogTag = "LeviMusic";

static int absInt(int value) { return value < 0 ? -value : value; }

void AndroidMediaPlayer::lockState() {
    while (__atomic_exchange_n(&mStateLock, 1, __ATOMIC_ACQUIRE) != 0) {
        const TimeSpec delay{0, 500000L};
        nanosleep(&delay, nullptr);
    }
}

void AndroidMediaPlayer::unlockState() {
    __atomic_store_n(&mStateLock, 0, __ATOMIC_RELEASE);
}

bool AndroidMediaPlayer::initialize(jni::Vm* vm) {
    lockState();
    if (mReady) { unlockState(); return true; }
    if (!vm) {
        unlockState();
        __android_log_print(LOG_ERROR_VALUE, LogTag,
                            "Android MediaPlayer unavailable: JavaVM not captured");
        return false;
    }
    mVm = vm;
    mVisualizerPermissionChecked = true;
    mRealVisualizerAllowed = jni::hasRecordAudioPermission();
    mFakeVisualizerActive = !mRealVisualizerAllowed;
    mReady = true;
    unlockState();
    if (mRealVisualizerAllowed) {
        __android_log_print(LOG_INFO_VALUE, LogTag,
                            "Android MediaPlayer backend selected; real FFT permission granted");
    } else {
        __android_log_print(LOG_INFO_VALUE, LogTag,
                            "RECORD_AUDIO not granted; fake FFT visualizer selected");
    }
    return true;
}

void AndroidMediaPlayer::clearMethodCache() {
    mReset = nullptr;
    mSetDataSource = nullptr;
    mPrepare = nullptr;
    mStart = nullptr;
    mPause = nullptr;
    mIsPlaying = nullptr;
    mGetDuration = nullptr;
    mGetCurrentPosition = nullptr;
    mSeekTo = nullptr;
    mSetVolume = nullptr;
    mSetLooping = nullptr;
    mGetAudioSessionId = nullptr;
    mRelease = nullptr;
    mVisualizerSetEnabled = nullptr;
    mVisualizerGetFft = nullptr;
    mVisualizerRelease = nullptr;
}

bool AndroidMediaPlayer::ensurePlayer(jni::Env* env) {
    if (mPlayer) return true;
    if (!env) return false;
    jni::JClass playerClass = jni::findClass(env, "android/media/MediaPlayer");
    if (!playerClass || jni::checkAndClearException(env, "FindClass(MediaPlayer)")) {
        if (playerClass) jni::deleteLocalRef(env, playerClass);
        return false;
    }
    const jni::JMethodId constructor = jni::getMethodId(env, playerClass, "<init>", "()V");
    mReset = jni::getMethodId(env, playerClass, "reset", "()V");
    mSetDataSource = jni::getMethodId(env, playerClass, "setDataSource", "(Ljava/lang/String;)V");
    mPrepare = jni::getMethodId(env, playerClass, "prepare", "()V");
    mStart = jni::getMethodId(env, playerClass, "start", "()V");
    mPause = jni::getMethodId(env, playerClass, "pause", "()V");
    mIsPlaying = jni::getMethodId(env, playerClass, "isPlaying", "()Z");
    mGetDuration = jni::getMethodId(env, playerClass, "getDuration", "()I");
    mGetCurrentPosition = jni::getMethodId(env, playerClass, "getCurrentPosition", "()I");
    mSeekTo = jni::getMethodId(env, playerClass, "seekTo", "(I)V");
    mSetVolume = jni::getMethodId(env, playerClass, "setVolume", "(FF)V");
    mSetLooping = jni::getMethodId(env, playerClass, "setLooping", "(Z)V");
    mGetAudioSessionId = jni::getMethodId(env, playerClass, "getAudioSessionId", "()I");
    mRelease = jni::getMethodId(env, playerClass, "release", "()V");
    if (jni::checkAndClearException(env, "GetMethodID(MediaPlayer)") ||
        !constructor || !mReset || !mSetDataSource || !mPrepare || !mStart ||
        !mPause || !mIsPlaying || !mGetDuration || !mGetCurrentPosition ||
        !mSeekTo || !mSetVolume || !mSetLooping || !mGetAudioSessionId || !mRelease) {
        jni::deleteLocalRef(env, playerClass);
        clearMethodCache();
        return false;
    }
    jni::JObject localPlayer = jni::newObjectA(env, playerClass, constructor, nullptr);
    if (!localPlayer || jni::checkAndClearException(env, "new MediaPlayer")) {
        if (localPlayer) jni::deleteLocalRef(env, localPlayer);
        jni::deleteLocalRef(env, playerClass);
        clearMethodCache();
        return false;
    }
    mPlayer = jni::newGlobalRef(env, localPlayer);
    const bool failed = !mPlayer || jni::checkAndClearException(env, "NewGlobalRef(MediaPlayer)");
    jni::deleteLocalRef(env, localPlayer);
    jni::deleteLocalRef(env, playerClass);
    if (failed) { mPlayer = nullptr; clearMethodCache(); return false; }
    return true;
}

bool AndroidMediaPlayer::invoke(jni::Env* env, jni::JMethodId method,
                                const jni::JValue* arguments,
                                const char* operation) {
    if (!env || !mPlayer || !method) return false;
    jni::callVoidMethodA(env, mPlayer, method, arguments);
    return !jni::checkAndClearException(env, operation);
}

bool AndroidMediaPlayer::queryBoolean(jni::Env* env, jni::JMethodId method,
                                      const char* operation, bool fallback) {
    if (!env || !mPlayer || !method) return fallback;
    const bool value = jni::callBooleanMethodA(env, mPlayer, method, nullptr) != 0;
    return jni::checkAndClearException(env, operation) ? fallback : value;
}

int AndroidMediaPlayer::queryInteger(jni::Env* env, jni::JMethodId method,
                                     const char* operation, int fallback) {
    if (!env || !mPlayer || !method) return fallback;
    const int value = jni::callIntMethodA(env, mPlayer, method, nullptr);
    return jni::checkAndClearException(env, operation) ? fallback : value;
}

void AndroidMediaPlayer::releaseVisualizer(jni::Env* env) {
    if (!env) return;
    if (mVisualizer && mVisualizerSetEnabled) {
        jni::JValue arg{}; arg.z = 0;
        jni::callIntMethodA(env, mVisualizer, mVisualizerSetEnabled, &arg);
        jni::checkAndClearException(env, "Visualizer.setEnabled(false)");
    }
    if (mVisualizer && mVisualizerRelease) {
        jni::callVoidMethodA(env, mVisualizer, mVisualizerRelease, nullptr);
        jni::checkAndClearException(env, "Visualizer.release");
    }
    if (mFftArray) jni::deleteGlobalRef(env, mFftArray);
    if (mVisualizer) jni::deleteGlobalRef(env, mVisualizer);
    mFftArray = nullptr;
    mVisualizer = nullptr;
    mVisualizerReady = false;
    mCaptureSize = 0;
    memset(mSpectrum, 0, sizeof(mSpectrum));
}

bool AndroidMediaPlayer::ensureVisualizer(jni::Env* env) {
    if (mVisualizerReady) return true;
    if (!mRealVisualizerAllowed) return false;
    if (!env || !mPlayer || !mGetAudioSessionId) return false;

    jni::JClass visualizerClass = jni::findClass(env, "android/media/audiofx/Visualizer");
    if (!visualizerClass || jni::checkAndClearException(env, "FindClass(Visualizer)")) {
        if (visualizerClass) jni::deleteLocalRef(env, visualizerClass);
        return false;
    }
    jni::JMethodId ctor = jni::getMethodId(env, visualizerClass, "<init>", "(I)V");
    jni::JMethodId setCaptureSize = jni::getMethodId(env, visualizerClass, "setCaptureSize", "(I)I");
    mVisualizerSetEnabled = jni::getMethodId(env, visualizerClass, "setEnabled", "(Z)I");
    mVisualizerGetFft = jni::getMethodId(env, visualizerClass, "getFft", "([B)I");
    mVisualizerRelease = jni::getMethodId(env, visualizerClass, "release", "()V");
    if (!ctor || !setCaptureSize || !mVisualizerSetEnabled || !mVisualizerGetFft ||
        !mVisualizerRelease || jni::checkAndClearException(env, "resolve Visualizer methods")) {
        jni::deleteLocalRef(env, visualizerClass);
        return false;
    }
    const int session = queryInteger(env, mGetAudioSessionId,
                                     "MediaPlayer.getAudioSessionId", 0);
    if (session <= 0) { jni::deleteLocalRef(env, visualizerClass); return false; }

    jni::JValue ctorArg{}; ctorArg.i = session;
    jni::JObject localVisualizer = jni::newObjectA(env, visualizerClass, ctor, &ctorArg);
    if (!localVisualizer || jni::checkAndClearException(env, "new Visualizer")) {
        if (localVisualizer) jni::deleteLocalRef(env, localVisualizer);
        jni::deleteLocalRef(env, visualizerClass);
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        if (!mVisualizerFallbackLogged) {
            __android_log_print(LOG_INFO_VALUE, LogTag,
                                "real FFT unavailable; switched to fake FFT visualizer");
            mVisualizerFallbackLogged = true;
        }
        return false;
    }
    mVisualizer = jni::newGlobalRef(env, localVisualizer);
    jni::deleteLocalRef(env, localVisualizer);
    jni::deleteLocalRef(env, visualizerClass);
    if (!mVisualizer || jni::checkAndClearException(env, "NewGlobalRef(Visualizer)")) {
        mVisualizer = nullptr;
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        return false;
    }

    mCaptureSize = 256;
    jni::JValue sizeArg{}; sizeArg.i = mCaptureSize;
    const int sizeResult = jni::callIntMethodA(env, mVisualizer, setCaptureSize, &sizeArg);
    if (jni::checkAndClearException(env, "Visualizer.setCaptureSize") || sizeResult != 0) {
        releaseVisualizer(env);
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        return false;
    }
    jni::JByteArray localFft = jni::newByteArray(env, mCaptureSize);
    if (!localFft || jni::checkAndClearException(env, "NewByteArray(FFT)")) {
        if (localFft) jni::deleteLocalRef(env, localFft);
        releaseVisualizer(env);
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        return false;
    }
    mFftArray = jni::newGlobalRef(env, localFft);
    jni::deleteLocalRef(env, localFft);
    if (!mFftArray || jni::checkAndClearException(env, "NewGlobalRef(FFT)")) {
        releaseVisualizer(env);
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        return false;
    }
    jni::JValue enableArg{}; enableArg.z = 1;
    const int enableResult = jni::callIntMethodA(env, mVisualizer, mVisualizerSetEnabled, &enableArg);
    if (jni::checkAndClearException(env, "Visualizer.setEnabled(true)") || enableResult != 0) {
        releaseVisualizer(env);
        mRealVisualizerAllowed = false;
        mFakeVisualizerActive = true;
        return false;
    }
    mVisualizerReady = true;
    __android_log_print(LOG_INFO_VALUE, LogTag,
                        "real FFT visualizer attached to MediaPlayer audio session");
    return true;
}

void AndroidMediaPlayer::resetPlayer(jni::Env* env) {
    if (!env || !mPlayer || !mReset) return;
    releaseVisualizer(env);
    jni::callVoidMethodA(env, mPlayer, mReset, nullptr);
    jni::checkAndClearException(env, "MediaPlayer.reset");
    mPrepared = false;
    mPaused = false;
}

void AndroidMediaPlayer::releasePlayerObject(jni::Env* env) {
    if (!env) return;
    releaseVisualizer(env);
    if (mPlayer && mRelease) {
        jni::callVoidMethodA(env, mPlayer, mRelease, nullptr);
        jni::checkAndClearException(env, "MediaPlayer.release(track switch)");
    }
    if (mPlayer) {
        jni::deleteGlobalRef(env, mPlayer);
        jni::checkAndClearException(env, "DeleteGlobalRef(MediaPlayer track switch)");
    }
    mPlayer = nullptr;
    mPrepared = false;
    mPaused = false;
    mCurrentPath[0] = 0;
    clearMethodCache();
}

bool AndroidMediaPlayer::play(const char* path) {
    if (!path || !*path) return false;
    lockState();
    if (!mReady || !mVm) { unlockState(); return false; }
    jni::EnvScope scope(mVm);
    jni::Env* env = scope.get();
    if (!env) { unlockState(); return false; }

    // Some vendor MediaPlayer implementations become unstable after repeated
    // reset/prepare/start cycles. Use a fresh Java MediaPlayer for every track
    // switch, and release the previous player before creating the next one.
    // The state lock keeps snapshots and FFT capture away from the handoff.
    if (mPlayer) releasePlayerObject(env);
    if (!ensurePlayer(env)) { unlockState(); return false; }

    jni::JString javaPath = jni::newStringUtf(env, path);
    if (!javaPath || jni::checkAndClearException(env, "NewStringUTF(audio path)")) {
        if (javaPath) jni::deleteLocalRef(env, javaPath);
        releasePlayerObject(env);
        unlockState();
        return false;
    }
    jni::JValue sourceArg{};
    sourceArg.l = javaPath;
    const bool sourceReady = invoke(env, mSetDataSource, &sourceArg,
                                    "MediaPlayer.setDataSource");
    jni::deleteLocalRef(env, javaPath);
    if (!sourceReady || !invoke(env, mPrepare, nullptr, "MediaPlayer.prepare")) {
        releasePlayerObject(env);
        unlockState();
        return false;
    }
    mPrepared = true;

    jni::JValue volumeArgs[2]{};
    volumeArgs[0].f = mVolume;
    volumeArgs[1].f = mVolume;
    jni::JValue loopArg{};
    loopArg.z = mLooping ? 1 : 0;
    if (!invoke(env, mSetVolume, volumeArgs, "MediaPlayer.setVolume") ||
        !invoke(env, mSetLooping, &loopArg, "MediaPlayer.setLooping") ||
        !invoke(env, mStart, nullptr, "MediaPlayer.start")) {
        releasePlayerObject(env);
        unlockState();
        return false;
    }
    mPaused = false;
    copyString(mCurrentPath, sizeof(mCurrentPath), path);
    if (mRealVisualizerAllowed) ensureVisualizer(env);
    unlockState();
    return true;
}

bool AndroidMediaPlayer::pause() {
    lockState();
    if (!mReady || !mVm || !mPrepared || mPaused || !mPlayer) { unlockState(); return false; }
    jni::EnvScope scope(mVm);
    if (!scope.valid() || !invoke(scope.get(), mPause, nullptr, "MediaPlayer.pause")) {
        unlockState(); return false;
    }
    mPaused = true;
    unlockState();
    return true;
}

bool AndroidMediaPlayer::resume() {
    lockState();
    if (!mReady || !mVm || !mPrepared || !mPaused || !mPlayer) { unlockState(); return false; }
    jni::EnvScope scope(mVm);
    if (!scope.valid() || !invoke(scope.get(), mStart, nullptr, "MediaPlayer.start")) {
        unlockState(); return false;
    }
    mPaused = false;
    unlockState();
    return true;
}

void AndroidMediaPlayer::stop() {
    lockState();
    mCurrentPath[0] = 0;
    if (mReady && mVm && mPlayer) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) releasePlayerObject(scope.get());
        else { mPrepared = false; mPaused = false; }
    } else {
        mPrepared = false;
        mPaused = false;
    }
    unlockState();
}

bool AndroidMediaPlayer::seekToMs(int positionMs) {
    lockState();
    if (!mReady || !mVm || !mPlayer || !mPrepared) { unlockState(); return false; }
    if (positionMs < 0) positionMs = 0;
    jni::EnvScope scope(mVm);
    if (!scope.valid()) { unlockState(); return false; }
    const int duration = queryInteger(scope.get(), mGetDuration, "MediaPlayer.getDuration", 0);
    if (duration > 0 && positionMs > duration) positionMs = duration;
    jni::JValue arg{}; arg.i = positionMs;
    const bool result = invoke(scope.get(), mSeekTo, &arg, "MediaPlayer.seekTo");
    unlockState();
    return result;
}

bool AndroidMediaPlayer::setVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    lockState();
    mVolume = volume;
    if (!mReady || !mVm || !mPlayer || !mPrepared) { unlockState(); return true; }
    jni::EnvScope scope(mVm);
    if (!scope.valid()) { unlockState(); return false; }
    jni::JValue args[2]{}; args[0].f = volume; args[1].f = volume;
    const bool result = invoke(scope.get(), mSetVolume, args, "MediaPlayer.setVolume");
    unlockState();
    return result;
}

bool AndroidMediaPlayer::setLooping(bool enabled) {
    lockState();
    mLooping = enabled;
    if (!mReady || !mVm || !mPlayer || !mPrepared) { unlockState(); return true; }
    jni::EnvScope scope(mVm);
    if (!scope.valid()) { unlockState(); return false; }
    jni::JValue arg{}; arg.z = enabled ? 1 : 0;
    const bool result = invoke(scope.get(), mSetLooping, &arg, "MediaPlayer.setLooping");
    unlockState();
    return result;
}

bool AndroidMediaPlayer::captureSpectrum(float* bars, int count) {
    if (!bars || count <= 0) return false;
    if (count > 32) count = 32;
    lockState();
    bool captured = false;
    if (mReady && mVm && mPrepared && mPlayer) {
        jni::EnvScope scope(mVm);
        jni::Env* env = scope.get();
        if (env && (mVisualizerReady || ensureVisualizer(env))) {
            jni::JValue arg{}; arg.l = mFftArray;
            const int result = jni::callIntMethodA(env, mVisualizer, mVisualizerGetFft, &arg);
            if (!jni::checkAndClearException(env, "Visualizer.getFft") && result == 0) {
                jni::JByte raw[256]{};
                jni::getByteArrayRegion(env, reinterpret_cast<jni::JByteArray>(mFftArray),
                                        0, mCaptureSize, raw);
                if (!jni::checkAndClearException(env, "GetByteArrayRegion(FFT)")) {
                    const int bins = mCaptureSize / 2;
                    for (int bar = 0; bar < count; ++bar) {
                        int start = 1 + (bar * bar * (bins - 2)) / (count * count);
                        int end = 1 + ((bar + 1) * (bar + 1) * (bins - 2)) /
                                      (count * count);
                        if (end <= start) end = start + 1;
                        if (end >= bins) end = bins - 1;
                        int peak = 0;
                        for (int bin = start; bin <= end; ++bin) {
                            const int re = static_cast<signed char>(raw[bin * 2]);
                            const int im = static_cast<signed char>(raw[bin * 2 + 1]);
                            const int magnitude = absInt(re) + absInt(im);
                            if (magnitude > peak) peak = magnitude;
                        }
                        float normalized = static_cast<float>(peak) / 150.0f;
                        if (normalized > 1.0f) normalized = 1.0f;
                        mSpectrum[bar] = mSpectrum[bar] * 0.62f + normalized * 0.38f;
                    }
                    captured = true;
                }
            }
        }
    }
    if (!captured) {
        bool playing = false;
        int position = 0;
        if (mReady && mVm && mPrepared && mPlayer) {
            jni::EnvScope scope(mVm);
            if (scope.valid()) {
                playing = queryBoolean(scope.get(), mIsPlaying,
                                       "MediaPlayer.isPlaying(fake FFT)", false);
                position = queryInteger(scope.get(), mGetCurrentPosition,
                                        "MediaPlayer.getCurrentPosition(fake FFT)", 0);
            }
        }
        if (playing) {
            // Permission-free deterministic pseudo spectrum. It follows playback
            // time, track identity and a low-frequency beat envelope, so it
            // animates naturally without pretending to be real audio analysis.
            u32 seed = 2166136261u;
            for (int i = 0; mCurrentPath[i]; ++i) {
                seed ^= static_cast<u8>(mCurrentPath[i]);
                seed *= 16777619u;
            }
            const u32 frame = static_cast<u32>(position / 36);
            const float beat = ((position / 120) % 8 == 0) ? 1.0f :
                               (((position / 120) % 4 == 0) ? 0.78f : 0.52f);
            for (int i = 0; i < count; ++i) {
                u32 value = seed ^ (frame * 747796405u) ^
                            (static_cast<u32>(i + 1) * 2891336453u);
                value ^= value >> 16; value *= 2246822519u; value ^= value >> 13;
                const float noise = static_cast<float>(value & 1023u) / 1023.0f;
                const int phase = (position / 42 + i * 9) & 63;
                const float wave = phase < 32
                    ? static_cast<float>(phase) / 31.0f
                    : static_cast<float>(63 - phase) / 31.0f;
                const float bandFalloff = 1.0f -
                    (static_cast<float>(i) / static_cast<float>(count)) * 0.48f;
                float target = (0.10f + noise * 0.42f + wave * 0.38f) *
                               beat * bandFalloff;
                if (target > 1.0f) target = 1.0f;
                mSpectrum[i] = mSpectrum[i] * 0.58f + target * 0.42f;
            }
            mFakeVisualizerActive = true;
            captured = true;
        } else {
            for (int i = 0; i < count; ++i) mSpectrum[i] *= 0.82f;
        }
    }
    for (int i = 0; i < count; ++i) bars[i] = mSpectrum[i];
    unlockState();
    return captured;
}

bool AndroidMediaPlayer::isPlaying() {
    lockState();
    bool result = false;
    if (mReady && mVm && mPlayer && mPrepared) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) result = queryBoolean(scope.get(), mIsPlaying,
                                                 "MediaPlayer.isPlaying", false);
    }
    unlockState();
    return result;
}

int AndroidMediaPlayer::durationMs() {
    lockState();
    int result = 0;
    if (mReady && mVm && mPlayer && mPrepared) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) result = queryInteger(scope.get(), mGetDuration,
                                                 "MediaPlayer.getDuration", 0);
    }
    unlockState();
    return result;
}

int AndroidMediaPlayer::positionMs() {
    lockState();
    int result = 0;
    if (mReady && mVm && mPlayer && mPrepared) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) result = queryInteger(scope.get(), mGetCurrentPosition,
                                                 "MediaPlayer.getCurrentPosition", 0);
    }
    unlockState();
    return result;
}

void AndroidMediaPlayer::snapshot(Snapshot& out) {
    memset(&out, 0, sizeof(out));
    lockState();
    out.hasMedia = mPrepared;
    out.paused = mPaused;
    out.volume = mVolume;
    copyString(out.path, sizeof(out.path), mCurrentPath);
    if (mReady && mVm && mPlayer && mPrepared) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) {
            out.playing = queryBoolean(scope.get(), mIsPlaying,
                                       "MediaPlayer.isPlaying", false);
            out.durationMs = queryInteger(scope.get(), mGetDuration,
                                          "MediaPlayer.getDuration", 0);
            out.positionMs = queryInteger(scope.get(), mGetCurrentPosition,
                                           "MediaPlayer.getCurrentPosition", 0);
        }
    }
    unlockState();
}

void AndroidMediaPlayer::shutdown() {
    lockState();
    mCurrentPath[0] = 0;
    if (mVm && mPlayer) {
        jni::EnvScope scope(mVm);
        if (scope.valid()) releasePlayerObject(scope.get());
    }
    mPlayer = nullptr;
    clearMethodCache();
    mPrepared = false;
    mPaused = false;
    mReady = false;
    mVisualizerPermissionChecked = false;
    mRealVisualizerAllowed = false;
    mFakeVisualizerActive = false;
    mVisualizerFallbackLogged = false;
    mVm = nullptr;
    unlockState();
}
