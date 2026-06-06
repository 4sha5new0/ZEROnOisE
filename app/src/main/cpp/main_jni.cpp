#include <jni.h>
#include <string>
#include <memory>
#include <vector>
#include <android/log.h>
#include "audio/audio_engine.h"
#include "filesystem/file_scanner.h"
#include "metadata/metadata_reader.h"

#define TAG "ZEROnOisE_JNI"

// ─────────────────────────────────────────────────────────────────────────────
// ヘルパー: AudioInfo → Java の AudioInfoData オブジェクトに変換
// ─────────────────────────────────────────────────────────────────────────────
static jobject AudioInfoToJava(JNIEnv* env, const AudioInfo& info) {
    jclass cls = env->FindClass("com/example/audioplayer/AudioInfoData");
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    jobject obj = env->NewObject(cls, ctor);

    auto setStr = [&](const char* field, const std::string& val) {
        jfieldID f = env->GetFieldID(cls, field, "Ljava/lang/String;");
        if (f) env->SetObjectField(obj, f, env->NewStringUTF(val.c_str()));
    };
    auto setInt = [&](const char* field, int val) {
        jfieldID f = env->GetFieldID(cls, field, "I");
        if (f) env->SetIntField(obj, f, val);
    };
    auto setLong = [&](const char* field, int64_t val) {
        jfieldID f = env->GetFieldID(cls, field, "J");
        if (f) env->SetLongField(obj, f, val);
    };
    auto setBool = [&](const char* field, bool val) {
        jfieldID f = env->GetFieldID(cls, field, "Z");
        if (f) env->SetBooleanField(obj, f, val);
    };

    setStr ("path",          info.path);
    setStr ("filename",      info.filename);
    setStr ("title",         info.title);
    setStr ("artist",        info.artist);
    setStr ("album",         info.album);
    setStr ("codecDetail",   info.codec_detail);
    setStr ("mqaEncoder",    info.mqa_encoder);
    setInt ("formatOrdinal", static_cast<int>(info.format));
    setInt ("sampleRate",    static_cast<int>(info.sample_rate));
    setInt ("bitDepth",      static_cast<int>(info.bit_depth));
    setInt ("channels",      static_cast<int>(info.channels));
    setInt ("durationMs",    static_cast<int>(info.duration_ms));
    setInt ("bitrateKbps",   static_cast<int>(info.bitrate_kbps));
    setInt ("mqaOriginalSr", static_cast<int>(info.mqa_original_sr));
    setLong("totalSamples",  static_cast<int64_t>(info.total_samples));
    setLong("fileSizeBytes", info.file_size);
    setBool("isLossless",    info.is_lossless);
    setBool("isHires",       info.is_hires);
    setBool("isMqa",         info.is_mqa);
    setBool("isMqaStudio",   info.is_mqa_studio);

    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// ヘルパー: FileEntry → Java の FileEntryData オブジェクトに変換
// ─────────────────────────────────────────────────────────────────────────────
static jobject FileEntryToJava(JNIEnv* env, const FileEntry& fe) {
    jclass cls = env->FindClass("com/example/audioplayer/FileEntryData");
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    jobject obj = env->NewObject(cls, ctor);

    auto setStr = [&](const char* field, const std::string& val) {
        jfieldID f = env->GetFieldID(cls, field, "Ljava/lang/String;");
        if (f) env->SetObjectField(obj, f, env->NewStringUTF(val.c_str()));
    };
    auto setBool = [&](const char* field, bool val) {
        jfieldID f = env->GetFieldID(cls, field, "Z");
        if (f) env->SetBooleanField(obj, f, val);
    };
    auto setLong = [&](const char* field, int64_t val) {
        jfieldID f = env->GetFieldID(cls, field, "J");
        if (f) env->SetLongField(obj, f, val);
    };

    setStr ("name",        fe.name);
    setStr ("fullPath",    fe.full_path);
    setBool("isDirectory", fe.is_directory);
    setLong("sizeBytes",   fe.size_bytes);
    setLong("modifiedMs",  fe.modified_ms);

    // ファイルの場合のみメタデータを付与
    if (!fe.is_directory && fe.meta_loaded) {
        jclass acls = env->FindClass("com/example/audioplayer/AudioInfoData");
        if (acls) {
            jobject ai = AudioInfoToJava(env, fe.info);
            jfieldID f = env->GetFieldID(cls, "audioInfo",
                "Lcom/example/audioplayer/AudioInfoData;");
            if (f && ai) env->SetObjectField(obj, f, ai);
        }
    }
    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// JNI 実装
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {

// nativeCreate: AudioEngine を生成して long ハンドルを返す
JNIEXPORT jlong JNICALL
Java_com_example_audioplayer_NativeBridge_nativeCreate(JNIEnv*, jobject) {
    auto* engine = new AudioEngine();
    return reinterpret_cast<jlong>(engine);
}

// nativeDestroy: AudioEngine を破棄
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<AudioEngine*>(handle);
}

// nativeLoadFile: ファイルを開く（再生は nativePlay で開始）
JNIEXPORT jboolean JNICALL
Java_com_example_audioplayer_NativeBridge_nativeLoadFile(
        JNIEnv* env, jobject, jlong handle, jstring jpath) {
    auto* engine = reinterpret_cast<AudioEngine*>(handle);
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    const bool ok = engine->LoadFile(std::string(path));
    env->ReleaseStringUTFChars(jpath, path);
    return ok;
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativePlay(JNIEnv*, jobject, jlong handle) {
    reinterpret_cast<AudioEngine*>(handle)->Play();
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativePause(JNIEnv*, jobject, jlong handle) {
    reinterpret_cast<AudioEngine*>(handle)->Pause();
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeStop(JNIEnv*, jobject, jlong handle) {
    reinterpret_cast<AudioEngine*>(handle)->Stop();
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeSeek(
        JNIEnv*, jobject, jlong handle, jlong target_sample) {
    reinterpret_cast<AudioEngine*>(handle)
        ->Seek(static_cast<uint64_t>(target_sample));
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeNextTrack(JNIEnv*, jobject, jlong handle) {
    reinterpret_cast<AudioEngine*>(handle)->NextTrack();
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativePrevTrack(JNIEnv*, jobject, jlong handle) {
    reinterpret_cast<AudioEngine*>(handle)->PrevTrack();
}

// nativeGetPosition: 現在位置（サンプル単位）
JNIEXPORT jlong JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetPosition(JNIEnv*, jobject, jlong handle) {
    return static_cast<jlong>(
        reinterpret_cast<AudioEngine*>(handle)->GetPosition());
}

// nativeIsPlaying
JNIEXPORT jboolean JNICALL
Java_com_example_audioplayer_NativeBridge_nativeIsPlaying(JNIEnv*, jobject, jlong handle) {
    return reinterpret_cast<AudioEngine*>(handle)->IsPlaying();
}

// nativeGetStreamMode: 0=EXCLUSIVE, 1=SHARED, 2=NONE
JNIEXPORT jint JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetStreamMode(JNIEnv*, jobject, jlong handle) {
    return static_cast<jint>(
        reinterpret_cast<AudioEngine*>(handle)->GetStreamMode());
}

// nativeGetUnderrunCount
JNIEXPORT jint JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetUnderrunCount(JNIEnv*, jobject, jlong handle) {
    return static_cast<jint>(
        reinterpret_cast<AudioEngine*>(handle)->GetUnderrunCount());
}

// nativeGetCurrentInfo: 現在のファイル情報を AudioInfoData オブジェクトで返す
JNIEXPORT jobject JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetCurrentInfo(
        JNIEnv* env, jobject, jlong handle) {
    auto* engine = reinterpret_cast<AudioEngine*>(handle);
    return AudioInfoToJava(env, engine->GetCurrentInfo());
}

// nativeGetLevelMeter: float[8] = {rmsL, rmsR, peakL, peakR, holdL, holdR, clipL, clipR}
JNIEXPORT jfloatArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetLevelMeter(
        JNIEnv* env, jobject, jlong handle) {
    const auto snap =
        reinterpret_cast<AudioEngine*>(handle)->GetLevel();
    jfloatArray arr = env->NewFloatArray(8);
    float data[8] = {
        snap.rms_l_dbfs,  snap.rms_r_dbfs,
        snap.peak_l_dbfs, snap.peak_r_dbfs,
        snap.hold_l_dbfs, snap.hold_r_dbfs,
        snap.clip_l ? 1.0f : 0.0f,
        snap.clip_r ? 1.0f : 0.0f
    };
    env->SetFloatArrayRegion(arr, 0, 8, data);
    return arr;
}

// nativeScanDirectory: ディレクトリをスキャンして FileEntryData[] を返す
JNIEXPORT jobjectArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeScanDirectory(
        JNIEnv* env, jobject, jstring jdir) {
    const char* dir = env->GetStringUTFChars(jdir, nullptr);
    auto entries = FileScanner::ScanDirectory(std::string(dir));
    env->ReleaseStringUTFChars(jdir, dir);

    // メタデータ先読み（ファイルのみ、同期）
    for (auto& fe : entries) {
        if (!fe.is_directory) {
            fe.info = MetadataReader::Read(fe.full_path);
            fe.meta_loaded = true;
        }
    }

    jclass cls = env->FindClass("com/example/audioplayer/FileEntryData");
    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(entries.size()), cls, nullptr);
    for (size_t i = 0; i < entries.size(); ++i) {
        jobject obj = FileEntryToJava(env, entries[i]);
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), obj);
        env->DeleteLocalRef(obj);
    }
    return arr;
}

// nativeGetStorageRoots: ストレージルートの一覧を String[] で返す
JNIEXPORT jobjectArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetStorageRoots(JNIEnv* env, jobject) {
    const auto roots = FileScanner::GetStorageRoots();
    jclass str_cls = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(roots.size()), str_cls, nullptr);
    for (size_t i = 0; i < roots.size(); ++i) {
        env->SetObjectArrayElement(arr, static_cast<jsize>(i),
            env->NewStringUTF(roots[i].c_str()));
    }
    return arr;
}

// nativeReadMetadata: 単一ファイルのメタデータ読み取り
JNIEXPORT jobject JNICALL
Java_com_example_audioplayer_NativeBridge_nativeReadMetadata(
        JNIEnv* env, jobject, jstring jpath) {
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    AudioInfo info = MetadataReader::Read(std::string(path));
    env->ReleaseStringUTFChars(jpath, path);
    return AudioInfoToJava(env, info);
}

}  // extern "C"
