#pragma once
#include <aaudio/AAudio.h>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <functional>
#include "audio_buffer.h"
#include "level_meter.h"
#include "../audio_info.h"
#include "../decoder/decoder_base.h"

// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine
//
//  再生の中核。ファイルを開き、AAudioStream を設定し、Decoder Thread を起動する。
//  Java 側からは JNI ブリッジ（main_jni.cpp）経由でのみ操作する。
// ─────────────────────────────────────────────────────────────────────────────
class AudioEngine {
public:
    enum class StreamMode { EXCLUSIVE = 0, SHARED = 1, NONE = 2 };

    // コールバック: Java 側への通知用（JNI スレッドではなく Decoder Thread から呼ばれる）
    using TrackChangedCallback = std::function<void(const std::string& new_path)>;
    using PlaybackEndCallback  = std::function<void()>;
    using ErrorCallback_t      = std::function<void(const std::string& msg)>;

    AudioEngine();
    ~AudioEngine();

    // ファイルを開いて再生準備（停止状態）
    bool LoadFile(const std::string& path);

    void Play();
    void Pause();
    void Stop();
    void Seek(uint64_t target_sample);

    void NextTrack();
    void PrevTrack();

    bool            IsPlaying()     const { return playing_.load(); }
    uint64_t        GetPosition()   const { return position_.load(); }
    StreamMode      GetStreamMode() const { return stream_mode_.load(); }
    const AudioInfo& GetCurrentInfo()     { return current_info_; }
    LevelMeter::Snapshot GetLevel() const { return level_meter_.Read(); }
    uint32_t        GetUnderrunCount() const { return underrun_count_.load(); }

    void SetTrackChangedCallback(TrackChangedCallback cb) { on_track_changed_ = std::move(cb); }
    void SetPlaybackEndCallback (PlaybackEndCallback  cb) { on_playback_end_   = std::move(cb); }
    void SetErrorCallback       (ErrorCallback_t      cb) { on_error_          = std::move(cb); }

private:
    // ── AAudio ────────────────────────────────────────────────────────────────
    bool OpenStream(uint32_t sample_rate, aaudio_format_t fmt, uint8_t channels);
    void CloseStream();

    static aaudio_data_callback_result_t DataCallback(
        AAudioStream*, void* user_data, void* audio_data, int32_t num_frames);
    static void ErrorCallbackStatic(
        AAudioStream*, void* user_data, aaudio_result_t error);

    // ── Decoder Thread ────────────────────────────────────────────────────────
    void StartDecoderThread();
    void StopDecoderThread();
    void DecoderThreadFunc();

    // ── 曲切り替え（Decoder Thread 内から呼ぶ）───────────────────────────────
    bool SwitchTrack(const std::string& path);

    // ── デコーダー生成 ────────────────────────────────────────────────────────
    static std::unique_ptr<DecoderBase> CreateDecoder(const std::string& path);

    // ── メンバ変数 ────────────────────────────────────────────────────────────
    AAudioStream*                    stream_          = nullptr;
    std::unique_ptr<LockFreeRingBuffer> ring_buf_;

    std::unique_ptr<DecoderBase>     decoder_;
    AudioInfo                        current_info_;

    std::thread                      decoder_thread_;
    std::atomic<bool>                decoder_running_{false};
    std::atomic<bool>                playing_{false};

    std::atomic<uint64_t>            position_{0};
    std::atomic<StreamMode>          stream_mode_{StreamMode::NONE};
    std::atomic<uint32_t>            underrun_count_{0};

    LevelMeter                       level_meter_;

    TrackChangedCallback             on_track_changed_;
    PlaybackEndCallback              on_playback_end_;
    ErrorCallback_t                  on_error_;

    // リングバッファの目標サイズ: 約 2 秒分（最大 SR × 2s × 4bytes × 2ch）
    static constexpr size_t kRingCapacity = 384000 * 2 * 4 * 2;  // ~6MB
    static constexpr int32_t kCallbackFrames = 2048;
};
