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
// JNI ローカル参照管理の原則
//   ・FindClass / NewObject / NewStringUTF などで生成した jobject / jclass /
//     jstring はすべて DeleteLocalRef で解放する。
//   ・解放しないと参照テーブル（上限 512）が溢れて SIGSEGV でクラッシュする。
//   ・関数から return する jobject だけは呼び出し側に所有権を渡すため解放しない。
// ─────────────────────────────────────────────────────────────────────────────

static jobject AudioInfoToJava(JNIEnv* env, const AudioInfo& info) {
    jclass cls = env->FindClass("com/example/audioplayer/AudioInfoData");
    if (!cls) return nullptr;

    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    if (!ctor) { env->DeleteLocalRef(cls); return nullptr; }

    jobject obj = env->NewObject(cls, ctor);
    if (!obj) { env->DeleteLocalRef(cls); return nullptr; }

    // jstring を作成 → SetObjectField → 即 DeleteLocalRef
    auto setStr = [&](const char* field, const std::string& val) {
        jfieldID f = env->GetFieldID(cls, field, "Ljava/lang/String;");
        if (!f) return;
        jstring js = env->NewStringUTF(val.c_str());
        if (js) {
            env->SetObjectField(obj, f, js);
            env->DeleteLocalRef(js);   // ← 必須
        }
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

    env->DeleteLocalRef(cls);   // ← cls 解放（obj は呼び出し側へ所有権移譲）
    return obj;
}

static jobject FileEntryToJava(JNIEnv* env, const FileEntry& fe) {
    jclass cls = env->FindClass("com/example/audioplayer/FileEntryData");
    if (!cls) return nullptr;

    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    if (!ctor) { env->DeleteLocalRef(cls); return nullptr; }

    jobject obj = env->NewObject(cls, ctor);
    if (!obj) { env->DeleteLocalRef(cls); return nullptr; }

    auto setStr = [&](const char* field, const std::string& val) {
        jfieldID f = env->GetFieldID(cls, field, "Ljava/lang/String;");
        if (!f) return;
        jstring js = env->NewStringUTF(val.c_str());
        if (js) {
            env->SetObjectField(obj, f, js);
            env->DeleteLocalRef(js);   // ← 必須
        }
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

    if (!fe.is_directory && fe.meta_loaded) {
        jobject ai = AudioInfoToJava(env, fe.info);
        if (ai) {
            jfieldID f = env->GetFieldID(cls, "audioInfo",
                "Lcom/example/audioplayer/AudioInfoData;");
            if (f) env->SetObjectField(obj, f, ai);
            env->DeleteLocalRef(ai);   // ← 必須
        }
    }

    env->DeleteLocalRef(cls);
    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
extern "C" {

JNIEXPORT jlong JNICALL
Java_com_example_audioplayer_NativeBridge_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new AudioEngine());
}

JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<AudioEngine*>(handle);
}

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
Java_com_example_audioplayer_NativeBridge_nativePlay(JNIEnv*, jobject, jlong h) {
    reinterpret_cast<AudioEngine*>(h)->Play();
}
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativePause(JNIEnv*, jobject, jlong h) {
    reinterpret_cast<AudioEngine*>(h)->Pause();
}
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeStop(JNIEnv*, jobject, jlong h) {
    reinterpret_cast<AudioEngine*>(h)->Stop();
}
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeSeek(
        JNIEnv*, jobject, jlong h, jlong sample) {
    reinterpret_cast<AudioEngine*>(h)->Seek(static_cast<uint64_t>(sample));
}
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativeNextTrack(JNIEnv*, jobject, jlong h) {
    reinterpret_cast<AudioEngine*>(h)->NextTrack();
}
JNIEXPORT void JNICALL
Java_com_example_audioplayer_NativeBridge_nativePrevTrack(JNIEnv*, jobject, jlong h) {
    reinterpret_cast<AudioEngine*>(h)->PrevTrack();
}

JNIEXPORT jlong JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetPosition(JNIEnv*, jobject, jlong h) {
    return static_cast<jlong>(reinterpret_cast<AudioEngine*>(h)->GetPosition());
}
JNIEXPORT jboolean JNICALL
Java_com_example_audioplayer_NativeBridge_nativeIsPlaying(JNIEnv*, jobject, jlong h) {
    return reinterpret_cast<AudioEngine*>(h)->IsPlaying();
}
JNIEXPORT jint JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetStreamMode(JNIEnv*, jobject, jlong h) {
    return static_cast<jint>(reinterpret_cast<AudioEngine*>(h)->GetStreamMode());
}
JNIEXPORT jint JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetUnderrunCount(JNIEnv*, jobject, jlong h) {
    return static_cast<jint>(reinterpret_cast<AudioEngine*>(h)->GetUnderrunCount());
}

JNIEXPORT jobject JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetCurrentInfo(
        JNIEnv* env, jobject, jlong h) {
    return AudioInfoToJava(env, reinterpret_cast<AudioEngine*>(h)->GetCurrentInfoCopy());
}

JNIEXPORT jfloatArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetLevelMeter(
        JNIEnv* env, jobject, jlong h) {
    const auto snap = reinterpret_cast<AudioEngine*>(h)->GetLevel();
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

JNIEXPORT jobjectArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeScanDirectory(
        JNIEnv* env, jobject, jstring jdir) {
    const char* dir = env->GetStringUTFChars(jdir, nullptr);
    auto entries = FileScanner::ScanDirectory(std::string(dir));
    env->ReleaseStringUTFChars(jdir, dir);

    // メタデータ先読み（ファイルのみ）
    for (auto& fe : entries) {
        if (!fe.is_directory) {
            fe.info = MetadataReader::Read(fe.full_path);
            fe.meta_loaded = true;
        }
    }

    jclass cls = env->FindClass("com/example/audioplayer/FileEntryData");
    if (!cls) {
        // フォールバック: 空配列を返す
        jclass obj_cls = env->FindClass("java/lang/Object");
        jobjectArray empty = env->NewObjectArray(0, obj_cls, nullptr);
        env->DeleteLocalRef(obj_cls);
        return empty;
    }

    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(entries.size()), cls, nullptr);
    env->DeleteLocalRef(cls);   // arr が cls を保持するので解放可能

    for (size_t i = 0; i < entries.size(); ++i) {
        jobject obj = FileEntryToJava(env, entries[i]);
        if (obj) {
            env->SetObjectArrayElement(arr, static_cast<jsize>(i), obj);
            env->DeleteLocalRef(obj);   // ← 必須
        }
    }
    return arr;
}

JNIEXPORT jobjectArray JNICALL
Java_com_example_audioplayer_NativeBridge_nativeGetStorageRoots(JNIEnv* env, jobject) {
    const auto roots = FileScanner::GetStorageRoots();

    jclass str_cls = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(roots.size()), str_cls, nullptr);
    env->DeleteLocalRef(str_cls);

    for (size_t i = 0; i < roots.size(); ++i) {
        jstring js = env->NewStringUTF(roots[i].c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);   // ← 必須
    }
    return arr;
}

JNIEXPORT jobject JNICALL
Java_com_example_audioplayer_NativeBridge_nativeReadMetadata(
        JNIEnv* env, jobject, jstring jpath) {
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    AudioInfo info = MetadataReader::Read(std::string(path));
    env->ReleaseStringUTFChars(jpath, path);
    return AudioInfoToJava(env, info);
}

}  // extern "C"
