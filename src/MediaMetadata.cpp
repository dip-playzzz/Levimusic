#include "MediaMetadata.hpp"

static constexpr const char* LogTag = "LeviMusic";
static constexpr int ArtworkSize = 256;

bool MediaMetadata::initialize(jni::Vm* vm, leviabi::Api* api) {
    mVm = vm;
    mApi = api;
    mReady = mVm && mApi && mApi->registerImage;
    return mReady;
}

void MediaMetadata::shutdown() {
    mVm = nullptr;
    mApi = nullptr;
    mReady = false;
}

u32 MediaMetadata::hashPath(const char* path) const {
    u32 hash = 2166136261u;
    if (path) for (usize i = 0; path[i]; ++i) {
        hash ^= static_cast<u8>(path[i]);
        hash *= 16777619u;
    }
    return hash;
}

bool MediaMetadata::knownArtwork(u32 hash) const {
    for (int i = 0; i < mRegisteredCount; ++i)
        if (mRegisteredHashes[i] == hash) return true;
    return false;
}

void MediaMetadata::rememberArtwork(u32 hash) {
    if (knownArtwork(hash)) return;
    if (mRegisteredCount < static_cast<int>(sizeof(mRegisteredHashes) / sizeof(mRegisteredHashes[0])))
        mRegisteredHashes[mRegisteredCount++] = hash;
}

bool MediaMetadata::extractString(jni::Env* env, jni::JObject retriever,
                                  jni::JMethodId extractMethod, int key,
                                  char* out, usize outSize) {
    if (!env || !retriever || !extractMethod || !out || outSize == 0) return false;
    jni::JValue arg{};
    arg.i = key;
    jni::JString value = reinterpret_cast<jni::JString>(
        jni::callObjectMethodA(env, retriever, extractMethod, &arg));
    if (!value || jni::checkAndClearException(env, "MediaMetadataRetriever.extractMetadata")) {
        if (value) jni::deleteLocalRef(env, value);
        return false;
    }
    const char* chars = jni::getStringUtfChars(env, value, nullptr);
    if (!chars || jni::checkAndClearException(env, "GetStringUTFChars(metadata)")) {
        if (chars) jni::releaseStringUtfChars(env, value, chars);
        jni::deleteLocalRef(env, value);
        return false;
    }
    copyString(out, outSize, chars);
    jni::releaseStringUtfChars(env, value, chars);
    jni::deleteLocalRef(env, value);
    return out[0] != 0;
}

bool MediaMetadata::decodeArtwork(jni::Env* env, jni::JByteArray picture,
                                  const char* imageId) {
    if (!env || !picture || !imageId || !*imageId) return false;
    const int byteCount = jni::getArrayLength(env, picture);
    if (byteCount <= 0 || byteCount > 8 * 1024 * 1024) return false;

    jni::JClass factoryClass = jni::findClass(env, "android/graphics/BitmapFactory");
    jni::JClass bitmapClass = jni::findClass(env, "android/graphics/Bitmap");
    if (!factoryClass || !bitmapClass ||
        jni::checkAndClearException(env, "FindClass(BitmapFactory/Bitmap)")) {
        if (factoryClass) jni::deleteLocalRef(env, factoryClass);
        if (bitmapClass) jni::deleteLocalRef(env, bitmapClass);
        return false;
    }

    jni::JMethodId decode = jni::getStaticMethodId(
        env, factoryClass, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");
    jni::JMethodId scale = jni::getStaticMethodId(
        env, bitmapClass, "createScaledBitmap",
        "(Landroid/graphics/Bitmap;IIZ)Landroid/graphics/Bitmap;");
    jni::JMethodId getPixels = jni::getMethodId(
        env, bitmapClass, "getPixels", "([IIIIIII)V");
    if (!decode || !scale || !getPixels ||
        jni::checkAndClearException(env, "resolve Bitmap methods")) {
        jni::deleteLocalRef(env, factoryClass);
        jni::deleteLocalRef(env, bitmapClass);
        return false;
    }

    jni::JValue decodeArgs[3]{};
    decodeArgs[0].l = picture;
    decodeArgs[1].i = 0;
    decodeArgs[2].i = byteCount;
    jni::JObject bitmap = jni::callStaticObjectMethodA(env, factoryClass, decode, decodeArgs);
    if (!bitmap || jni::checkAndClearException(env, "BitmapFactory.decodeByteArray")) {
        if (bitmap) jni::deleteLocalRef(env, bitmap);
        jni::deleteLocalRef(env, factoryClass);
        jni::deleteLocalRef(env, bitmapClass);
        return false;
    }

    jni::JValue scaleArgs[4]{};
    scaleArgs[0].l = bitmap;
    scaleArgs[1].i = ArtworkSize;
    scaleArgs[2].i = ArtworkSize;
    scaleArgs[3].z = 1;
    jni::JObject scaled = jni::callStaticObjectMethodA(env, bitmapClass, scale, scaleArgs);
    if (!scaled || jni::checkAndClearException(env, "Bitmap.createScaledBitmap")) {
        if (scaled) jni::deleteLocalRef(env, scaled);
        jni::deleteLocalRef(env, bitmap);
        jni::deleteLocalRef(env, factoryClass);
        jni::deleteLocalRef(env, bitmapClass);
        return false;
    }

    const int pixelCount = ArtworkSize * ArtworkSize;
    jni::JIntArray pixelsArray = jni::newIntArray(env, pixelCount);
    jni::JInt* argb = static_cast<jni::JInt*>(malloc(pixelCount * sizeof(jni::JInt)));
    u8* rgba = static_cast<u8*>(malloc(pixelCount * 4u));
    bool ok = pixelsArray && argb && rgba;
    if (ok) {
        jni::JValue pixelArgs[7]{};
        pixelArgs[0].l = pixelsArray;
        pixelArgs[1].i = 0;
        pixelArgs[2].i = ArtworkSize;
        pixelArgs[3].i = 0;
        pixelArgs[4].i = 0;
        pixelArgs[5].i = ArtworkSize;
        pixelArgs[6].i = ArtworkSize;
        jni::callVoidMethodA(env, scaled, getPixels, pixelArgs);
        ok = !jni::checkAndClearException(env, "Bitmap.getPixels");
    }
    if (ok) {
        jni::getIntArrayRegion(env, pixelsArray, 0, pixelCount, argb);
        ok = !jni::checkAndClearException(env, "GetIntArrayRegion(artwork)");
    }
    if (ok) {
        for (int i = 0; i < pixelCount; ++i) {
            const u32 pixel = static_cast<u32>(argb[i]);
            rgba[i * 4 + 0] = static_cast<u8>((pixel >> 16) & 0xFFu);
            rgba[i * 4 + 1] = static_cast<u8>((pixel >> 8) & 0xFFu);
            rgba[i * 4 + 2] = static_cast<u8>(pixel & 0xFFu);
            rgba[i * 4 + 3] = static_cast<u8>((pixel >> 24) & 0xFFu);
        }
        ok = mApi->registerImage(imageId, strlen(imageId), rgba,
                                 static_cast<usize>(pixelCount) * 4u,
                                 ArtworkSize, ArtworkSize);
    }

    if (pixelsArray) jni::deleteLocalRef(env, pixelsArray);
    if (argb) free(argb);
    if (rgba) free(rgba);
    jni::deleteLocalRef(env, scaled);
    jni::deleteLocalRef(env, bitmap);
    jni::deleteLocalRef(env, factoryClass);
    jni::deleteLocalRef(env, bitmapClass);
    return ok;
}

void MediaMetadata::ensure(MusicTrack& track) {
    if (track.metadataState != 0 || !mReady || !track.path[0] ||
        isOnlinePath(track.path)) return;
    track.metadataState = 1;

    jni::EnvScope scope(mVm);
    jni::Env* env = scope.get();
    if (!env) return;
    jni::JClass retrieverClass = jni::findClass(env, "android/media/MediaMetadataRetriever");
    if (!retrieverClass || jni::checkAndClearException(env, "FindClass(MediaMetadataRetriever)")) {
        if (retrieverClass) jni::deleteLocalRef(env, retrieverClass);
        return;
    }
    jni::JMethodId ctor = jni::getMethodId(env, retrieverClass, "<init>", "()V");
    jni::JMethodId setSource = jni::getMethodId(
        env, retrieverClass, "setDataSource", "(Ljava/lang/String;)V");
    jni::JMethodId extract = jni::getMethodId(
        env, retrieverClass, "extractMetadata", "(I)Ljava/lang/String;");
    jni::JMethodId getPicture = jni::getMethodId(
        env, retrieverClass, "getEmbeddedPicture", "()[B");
    jni::JMethodId release = jni::getMethodId(env, retrieverClass, "release", "()V");
    if (!ctor || !setSource || !extract || !getPicture || !release ||
        jni::checkAndClearException(env, "resolve MediaMetadataRetriever methods")) {
        jni::deleteLocalRef(env, retrieverClass);
        return;
    }

    jni::JObject retriever = jni::newObjectA(env, retrieverClass, ctor, nullptr);
    jni::JString path = jni::newStringUtf(env, track.path);
    if (!retriever || !path || jni::checkAndClearException(env, "create metadata retriever")) {
        if (path) jni::deleteLocalRef(env, path);
        if (retriever) jni::deleteLocalRef(env, retriever);
        jni::deleteLocalRef(env, retrieverClass);
        return;
    }
    jni::JValue sourceArg{};
    sourceArg.l = path;
    jni::callVoidMethodA(env, retriever, setSource, &sourceArg);
    const bool sourceFailed = jni::checkAndClearException(env, "MediaMetadataRetriever.setDataSource");
    jni::deleteLocalRef(env, path);
    if (!sourceFailed) {
        extractString(env, retriever, extract, 7, track.title, sizeof(track.title));
        extractString(env, retriever, extract, 2, track.artist, sizeof(track.artist));
        jni::JByteArray picture = reinterpret_cast<jni::JByteArray>(
            jni::callObjectMethodA(env, retriever, getPicture, nullptr));
        if (!jni::checkAndClearException(env, "MediaMetadataRetriever.getEmbeddedPicture") && picture) {
            const u32 hash = hashPath(track.path);
            snprintf(track.artworkId, sizeof(track.artworkId), "lm_art_%08x", hash);
            if (knownArtwork(hash) || decodeArtwork(env, picture, track.artworkId)) {
                rememberArtwork(hash);
                track.metadataState = 2;
            }
            jni::deleteLocalRef(env, picture);
        }
    }
    jni::callVoidMethodA(env, retriever, release, nullptr);
    jni::checkAndClearException(env, "MediaMetadataRetriever.release");
    jni::deleteLocalRef(env, retriever);
    jni::deleteLocalRef(env, retrieverClass);
}
