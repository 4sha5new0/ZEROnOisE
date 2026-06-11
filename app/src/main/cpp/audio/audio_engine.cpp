#include "audio_engine.h"
#include "../decoder/flac_decoder.h"
#include "../decoder/wav_decoder.h"
#include "../decoder/mp3_decoder.h"
#include "../decoder/mediacodec_decoder.h"
#include "../filesystem/file_scanner.h"
#include "../metadata/metadata_reader.h"
#include <algorithm>
#include <cctype>
#include <unistd.h>        // usleep
#include <sys/resource.h>  // setpriority / PRIO_PROCESS
#include <android/log.h>
#define TAG "AudioEngine"

// ─────────────────────────────────────────────────────────────────────────────
AudioEngine::AudioEngine()
    : ring_buf_(std::make_unique<LockFreeRingBuffer>(kRingCapacity))
{}

AudioEngine::~AudioEngine() {
    StopDecoderThread();
    CloseStream();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateDecoder: 拡張子からデコーダーを生成
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<DecoderBase> AudioEngine::CreateDecoder(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return nullptr;

    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    if (ext == "flac")                     return std::make_unique<FlacDecoder>();
    if (ext == "wav" || ext == "wave")     return std::make_unique<WavDecoder>();
    if (ext == "mp3")                      return std::make_unique<Mp3Decoder>();
    if (ext == "aac" || ext == "m4a"
            || ext == "webm")              return std::make_unique<MediaCodecDecoder>();
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenStream: ソースの SR / フォーマット / チャンネル数で AAudioStream を開く
// ─────────────────────────────────────────────────────────────────────────────
bool AudioEngine::OpenStream(uint32_t sample_rate, aaudio_format_t fmt, uint8_t channels) {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "createStreamBuilder failed");
        return false;
    }

    // ① EXCLUSIVE: システムミキサーを迂回
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    // ② ソースのサンプルレートをそのまま指定（変換なし）
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sample_rate));
    // ③ §4.2 対応表のフォーマット
    AAudioStreamBuilder_setFormat(builder, fmt);
    // ④ ステレオ固定
    AAudioStreamBuilder_setChannelCount(builder, channels);
    // ⑤ NONE: LOW_LATENCY より省電力。POWER_SAVING は使用禁止
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    // ⑥ コールバック
    AAudioStreamBuilder_setDataCallback(builder, DataCallback,  this);
    AAudioStreamBuilder_setErrorCallback(builder, ErrorCallbackStatic, this);
    // ⑦ バッファサイズ
    AAudioStreamBuilder_setFramesPerDataCallback(builder, kCallbackFrames);

    const aaudio_result_t result =
        AAudioStreamBuilder_openStream(builder, &stream_);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || !stream_) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "openStream failed: %s",
            AAudio_convertResultToText(result));
        return false;
    }

    // ⑧ EXCLUSIVE が取得できたか確認
    const aaudio_sharing_mode_t mode =
        AAudioStream_getSharingMode(stream_);
    stream_mode_.store(
        (mode == AAUDIO_SHARING_MODE_EXCLUSIVE) ? StreamMode::EXCLUSIVE : StreamMode::SHARED);

    // バッファキャパシティを 4 倍に設定（アンダーラン余裕）
    AAudioStream_setBufferSizeInFrames(stream_, kCallbackFrames * 4);

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "Stream opened: SR=%u fmt=%d ch=%d mode=%s",
        sample_rate, fmt, channels,
        (mode == AAUDIO_SHARING_MODE_EXCLUSIVE) ? "EXCLUSIVE" : "SHARED");
    return true;
}

void AudioEngine::CloseStream() {
    if (stream_) {
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
        stream_mode_.store(StreamMode::NONE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadFile: ファイルを開いてストリームを準備する（再生は Play() で開始）
// ─────────────────────────────────────────────────────────────────────────────
bool AudioEngine::LoadFile(const std::string& path) {
    StopDecoderThread();
    if (stream_) {
        AAudioStream_requestStop(stream_);
    }

    auto dec = CreateDecoder(path);
    if (!dec) {
        if (on_error_) on_error_("Unsupported format: " + path);
        return false;
    }
    if (!dec->Open(path)) {
        if (on_error_) on_error_(dec->GetLastError());
        return false;
    }

    const AudioInfo& new_info = dec->GetInfo();

    // ストリームの SR / フォーマットが変わる場合は再作成
    // atomic 値で比較（JNI スレッドから呼ばれるため mutex より atomic が適切）
    const bool need_recreate = !stream_ ||
        (new_info.sample_rate   != cb_sample_rate_.load()) ||
        (new_info.aaudio_format != cb_aaudio_format_.load()) ||
        (new_info.channels      != 2u);  // 常にステレオ固定

    if (need_recreate) {
        CloseStream();
        if (!OpenStream(new_info.sample_rate, new_info.aaudio_format, new_info.channels)) {
            return false;
        }
    }

    ring_buf_->Flush();
    level_meter_.Reset();
    decoder_       = std::move(dec);
    UpdateCurrentInfo(new_info);   // mutex + atomic を一括更新
    position_.store(0);
    underrun_count_.store(0);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::Play() {
    if (!stream_ || !decoder_) return;
    if (!playing_.load()) {
        playing_.store(true);
        AAudioStream_requestStart(stream_);
        StartDecoderThread();
    }
}

void AudioEngine::Pause() {
    if (playing_.load()) {
        playing_.store(false);
        StopDecoderThread();
        if (stream_) AAudioStream_requestPause(stream_);
        level_meter_.Reset();
    }
}

void AudioEngine::Stop() {
    StopDecoderThread();
    if (stream_) AAudioStream_requestStop(stream_);
    ring_buf_->Flush();
    level_meter_.Reset();
    playing_.store(false);
    position_.store(0);
    if (decoder_) decoder_->Seek(0);
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::Seek(uint64_t target_sample) {
    const bool was_playing = playing_.load();
    StopDecoderThread();

    ring_buf_->Flush();
    if (decoder_) decoder_->Seek(target_sample);
    position_.store(target_sample);

    if (was_playing) {
        playing_.store(true);
        StartDecoderThread();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::NextTrack() {
    std::string path, next;
    { std::lock_guard<std::mutex> lock(info_mutex_); path = current_info_.path; }
    if (path.empty()) return;
    next = FileScanner::GetNextFile(path);
    if (next.empty()) { Stop(); if (on_playback_end_) on_playback_end_(); return; }
    const bool was_playing = playing_.load();
    if (LoadFile(next) && was_playing) Play();
    if (on_track_changed_) on_track_changed_(next);
}

void AudioEngine::PrevTrack() {
    std::string path;
    uint32_t sr;
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        path = current_info_.path;
        sr   = current_info_.sample_rate;
    }
    if (path.empty()) return;
    if (position_.load() > static_cast<uint64_t>(sr) * 3) { Seek(0); return; }
    const std::string prev = FileScanner::GetPrevFile(path);
    const bool was_playing = playing_.load();
    if (!prev.empty() && LoadFile(prev) && was_playing) Play();
    if (on_track_changed_ && !prev.empty()) on_track_changed_(prev);
}

// ─────────────────────────────────────────────────────────────────────────────
// SwitchTrack: Decoder Thread 内から EOF 時に呼ぶ
// ─────────────────────────────────────────────────────────────────────────────
bool AudioEngine::SwitchTrack(const std::string& path) {
    auto dec = CreateDecoder(path);
    if (!dec || !dec->Open(path)) return false;

    const AudioInfo& new_info = dec->GetInfo();
    // atomic 値で現在の SR/format と比較（Decoder Thread から呼ばれる）
    const bool need_recreate =
        (new_info.sample_rate   != cb_sample_rate_.load())    ||
        (new_info.aaudio_format != cb_aaudio_format_.load())  ||
        (new_info.channels      != 2u);

    if (need_recreate) {
        // ストリームを止めて再作成
        if (stream_) AAudioStream_requestStop(stream_);
        CloseStream();
        if (!OpenStream(new_info.sample_rate, new_info.aaudio_format, new_info.channels)) {
            return false;
        }
        AAudioStream_requestStart(stream_);
    }

    ring_buf_->Flush();
    decoder_      = std::move(dec);
    UpdateCurrentInfo(new_info);   // mutex + atomic を一括更新
    position_.store(0);

    if (on_track_changed_) on_track_changed_(path);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DecoderThreadFunc: Producer ループ
// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::StartDecoderThread() {
    if (decoder_running_.load()) return;
    decoder_running_.store(true);
    decoder_thread_ = std::thread(&AudioEngine::DecoderThreadFunc, this);
}

void AudioEngine::StopDecoderThread() {
    decoder_running_.store(false);
    if (decoder_thread_.joinable()) decoder_thread_.join();
}

void AudioEngine::DecoderThreadFunc() {
    // nice 値を上げて（優先度を下げて）OS の電力管理を促す
    // SCHED_BATCH は Android NDK では未保証のため setpriority を使用
    setpriority(PRIO_PROCESS, 0, 10);

    constexpr size_t kChunkFrames = 4096;
    std::vector<uint8_t> chunk_buf(kChunkFrames * 8);  // 最大 8 bytes/frame

    while (decoder_running_.load()) {
        if (!decoder_) { usleep(10000); continue; }

        // bytes_per_frame は atomic で読む（UpdateCurrentInfo と同期済み）
        const size_t chunk_bytes = kChunkFrames
            * static_cast<size_t>(cb_bytes_per_frame_.load(std::memory_order_acquire));

        // リングバッファに書き込める余裕があるまで待つ
        if (ring_buf_->WritableBytes() < chunk_bytes) {
            usleep(2000);
            continue;
        }

        const int64_t n = decoder_->Decode(chunk_buf.data(), kChunkFrames);
        if (n == 0) {
            // EOF: 自動次曲（current_info_.path を mutex で読む）
            std::string cur_path;
            { std::lock_guard<std::mutex> lock(info_mutex_); cur_path = current_info_.path; }
            const std::string next = FileScanner::GetNextFile(cur_path);
            if (next.empty()) {
                playing_.store(false);
                if (on_playback_end_) on_playback_end_();
                break;
            }
            if (!SwitchTrack(next)) {
                if (on_error_) on_error_("Failed to open next track: " + next);
                break;
            }
            continue;
        }
        if (n < 0) {
            if (on_error_) on_error_(decoder_->GetLastError());
            break;
        }

        position_.fetch_add(static_cast<uint64_t>(n));

        const size_t written = ring_buf_->Write(chunk_buf.data(),
            static_cast<size_t>(n)
            * static_cast<size_t>(cb_bytes_per_frame_.load(std::memory_order_acquire)));
        if (written == 0) usleep(1000);
    }
    decoder_running_.store(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// DataCallback: AAudio RT スレッド
//  ルール: I/O・メモリ確保・mutex 禁止
// ─────────────────────────────────────────────────────────────────────────────
aaudio_data_callback_result_t AudioEngine::DataCallback(
        AAudioStream*, void* user_data,
        void* audio_data, int32_t num_frames) {

    auto* self = static_cast<AudioEngine*>(user_data);

    // RT スレッドでは mutex 禁止 → atomic で読む
    const int32_t  bytes_per_frame = self->cb_bytes_per_frame_.load(std::memory_order_acquire);
    const int32_t  fmt             = self->cb_aaudio_format_.load(std::memory_order_acquire);
    const uint32_t sample_rate     = self->cb_sample_rate_.load(std::memory_order_acquire);

    if (!self->playing_.load()) {
        std::memset(audio_data, 0, static_cast<size_t>(num_frames) * bytes_per_frame);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    const size_t byte_count = static_cast<size_t>(num_frames) * bytes_per_frame;
    const size_t read = self->ring_buf_->Read(audio_data, byte_count);

    if (read < byte_count) {
        self->underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // レベルメーター計算（atomic 読み値を使用: mutex 不要）
    self->level_meter_.Compute(audio_data, num_frames,
        static_cast<aaudio_format_t>(fmt), sample_rate);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::ErrorCallbackStatic(
        AAudioStream*, void* user_data, aaudio_result_t error) {
    auto* self = static_cast<AudioEngine*>(user_data);
    __android_log_print(ANDROID_LOG_ERROR, TAG, "AAudio error: %s",
        AAudio_convertResultToText(error));
    if (error == AAUDIO_ERROR_DISCONNECTED && self->playing_.load()) {
        self->StopDecoderThread();
        self->CloseStream();
        // atomic 値でストリームを再作成（current_info_ へのアクセス不要）
        self->OpenStream(
            self->cb_sample_rate_.load(),
            static_cast<aaudio_format_t>(self->cb_aaudio_format_.load()),
            2);
        if (self->stream_) AAudioStream_requestStart(self->stream_);
        self->StartDecoderThread();
    }
}
