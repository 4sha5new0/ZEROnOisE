#include "audio_engine.h"
#include "../decoder/flac_decoder.h"
#include "../decoder/wav_decoder.h"
#include "../decoder/mp3_decoder.h"
#include "../decoder/mediacodec_decoder.h"
#include "../filesystem/file_scanner.h"
#include "../metadata/metadata_reader.h"
#include <algorithm>
#include <cctype>
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
    const bool need_recreate = !stream_ ||
        (new_info.sample_rate   != current_info_.sample_rate) ||
        (new_info.aaudio_format != current_info_.aaudio_format) ||
        (new_info.channels      != current_info_.channels);

    if (need_recreate) {
        CloseStream();
        if (!OpenStream(new_info.sample_rate, new_info.aaudio_format, new_info.channels)) {
            return false;
        }
    }

    ring_buf_->Flush();
    level_meter_.Reset();
    decoder_       = std::move(dec);
    current_info_  = new_info;
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
    if (current_info_.path.empty()) return;
    const std::string next = FileScanner::GetNextFile(current_info_.path);
    if (next.empty()) {
        Stop();
        if (on_playback_end_) on_playback_end_();
        return;
    }
    const bool was_playing = playing_.load();
    if (LoadFile(next) && was_playing) Play();
    if (on_track_changed_) on_track_changed_(next);
}

void AudioEngine::PrevTrack() {
    if (current_info_.path.empty()) return;
    // 3秒以上再生済みなら先頭に戻る
    if (position_.load() > static_cast<uint64_t>(current_info_.sample_rate) * 3) {
        Seek(0);
        return;
    }
    const std::string prev = FileScanner::GetPrevFile(current_info_.path);
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
    const bool need_recreate =
        (new_info.sample_rate   != current_info_.sample_rate) ||
        (new_info.aaudio_format != current_info_.aaudio_format) ||
        (new_info.channels      != current_info_.channels);

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
    current_info_ = new_info;
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
    // スレッド優先度を下げて OS 電力管理を促す
    struct sched_param sp{};
    sp.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_BATCH, &sp);

    constexpr size_t kChunkFrames = 4096;
    std::vector<uint8_t> chunk_buf(kChunkFrames * 8);  // 最大 8 bytes/frame

    while (decoder_running_.load()) {
        if (!decoder_) { usleep(10000); continue; }

        const size_t chunk_bytes = kChunkFrames
            * static_cast<size_t>(current_info_.bytes_per_frame);

        // リングバッファに書き込める余裕があるまで待つ
        if (ring_buf_->WritableBytes() < chunk_bytes) {
            usleep(2000);  // 2ms 待機してバッファが消化されるのを待つ
            continue;
        }

        const int64_t n = decoder_->Decode(chunk_buf.data(), kChunkFrames);
        if (n == 0) {
            // EOF: 自動次曲
            const std::string next = FileScanner::GetNextFile(current_info_.path);
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

        // position_ を更新（サンプル単位）
        position_.fetch_add(static_cast<uint64_t>(n));

        const size_t written = ring_buf_->Write(chunk_buf.data(),
            static_cast<size_t>(n) * current_info_.bytes_per_frame);
        if (written == 0) {
            // 書き込み失敗（バッファフル）: 少し待ってリトライ
            usleep(1000);
        }
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
    if (!self->playing_.load()) {
        // 停止中: 無音で埋めて継続
        std::memset(audio_data, 0,
            num_frames * self->current_info_.bytes_per_frame);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    const size_t byte_count =
        static_cast<size_t>(num_frames) * self->current_info_.bytes_per_frame;
    const size_t read = self->ring_buf_->Read(audio_data, byte_count);

    if (read < byte_count) {
        // アンダーラン: 不足分を無音で補う
        self->underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // レベルメーター計算（atomic 書き込みのみ: RT-safe）
    self->level_meter_.Compute(
        audio_data, num_frames,
        self->current_info_.aaudio_format,
        self->current_info_.sample_rate);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioEngine::ErrorCallbackStatic(
        AAudioStream*, void* user_data, aaudio_result_t error) {
    auto* self = static_cast<AudioEngine*>(user_data);
    __android_log_print(ANDROID_LOG_ERROR, TAG, "AAudio error: %s",
        AAudio_convertResultToText(error));
    // ストリームが切断された場合は再開を試みる
    if (error == AAUDIO_ERROR_DISCONNECTED && self->playing_.load()) {
        self->StopDecoderThread();
        self->CloseStream();
        self->OpenStream(
            self->current_info_.sample_rate,
            self->current_info_.aaudio_format,
            self->current_info_.channels);
        if (self->stream_) AAudioStream_requestStart(self->stream_);
        self->StartDecoderThread();
    }
}
