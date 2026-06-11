#pragma once
#include <aaudio/AAudio.h>
#include <atomic>
#include <mutex>
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
//
//  スレッド間データ管理:
//   ・current_info_  → info_mutex_ で保護（Decoder Thread 書き込み / JNI 読み込み）
//   ・cb_bytes_per_frame_ / cb_aaudio_format_ / cb_sample_rate_
//                   → std::atomic（AAudio RT コールバックが lock-free で読む用）
// ─────────────────────────────────────────────────────────────────────────────
class AudioEngine {
public:
    enum class StreamMode { EXCLUSIVE = 0, SHARED = 1, NONE = 2 };

    using TrackChangedCallback = std::function<void(const std::string& new_path)>;
    using PlaybackEndCallback  = std::function<void()>;
    using ErrorCallback_t      = std::function<void(const std::string& msg)>;

    AudioEngine();
    ~AudioEngine();

    bool LoadFile(const std::string& path);
    void Play();
    void Pause();
    void Stop();
    void Seek(uint64_t target_sample);
    void NextTrack();
    void PrevTrack();

    bool       IsPlaying()       const { return playing_.load(); }
    uint64_t   GetPosition()     const { return position_.load(); }
    StreamMode GetStreamMode()   const { return stream_mode_.load(); }
    uint32_t   GetUnderrunCount()const { return underrun_count_.load(); }
    LevelMeter::Snapshot GetLevel() const { return level_meter_.Read(); }

    // current_info_ は mutex で保護してコピーを返す
    AudioInfo GetCurrentInfoCopy() {
        std::lock_guard<std::mutex> lock(info_mutex_);
        return current_info_;
    }

    void SetTrackChangedCallback(TrackChangedCallback cb) { on_track_changed_ = std::move(cb); }
    void SetPlaybackEndCallback (PlaybackEndCallback  cb) { on_playback_end_   = std::move(cb); }
    void SetErrorCallback       (ErrorCallback_t      cb) { on_error_          = std::move(cb); }

private:
    bool OpenStream(uint32_t sample_rate, aaudio_format_t fmt, uint8_t channels);
    void CloseStream();

    static aaudio_data_callback_result_t DataCallback(
        AAudioStream*, void* user_data, void* audio_data, int32_t num_frames);
    static void ErrorCallbackStatic(
        AAudioStream*, void* user_data, aaudio_result_t error);

    void StartDecoderThread();
    void StopDecoderThread();
    void DecoderThreadFunc();
    bool SwitchTrack(const std::string& path);

    static std::unique_ptr<DecoderBase> CreateDecoder(const std::string& path);

    // ── current_info_ の更新ヘルパー（必ずこれ経由で書き込む）──────────────────
    void UpdateCurrentInfo(const AudioInfo& info) {
        {
            std::lock_guard<std::mutex> lock(info_mutex_);
            current_info_ = info;
        }
        // RT コールバック用 atomic 値を即時更新
        cb_bytes_per_frame_.store(info.bytes_per_frame,  std::memory_order_release);
        cb_aaudio_format_.store(info.aaudio_format,      std::memory_order_release);
        cb_sample_rate_.store(info.sample_rate,          std::memory_order_release);
    }

    // ── メンバ変数 ────────────────────────────────────────────────────────────
    AAudioStream*                    stream_     = nullptr;
    std::unique_ptr<LockFreeRingBuffer> ring_buf_;

    std::unique_ptr<DecoderBase>     decoder_;

    mutable std::mutex               info_mutex_;   // current_info_ 保護
    AudioInfo                        current_info_;

    // AAudio RT コールバックが lock-free で読む用（mutex 禁止のため atomic）
    std::atomic<int32_t>             cb_bytes_per_frame_{4};
    std::atomic<int32_t>             cb_aaudio_format_{AAUDIO_FORMAT_PCM_I16};
    std::atomic<uint32_t>            cb_sample_rate_{44100};

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

    static constexpr size_t   kRingCapacity  = 384000 * 2 * 4 * 2;
    static constexpr int32_t  kCallbackFrames = 2048;
};
