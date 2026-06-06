#include "mediacodec_decoder.h"
#include <algorithm>
#include <cstring>
#include <android/log.h>
#define TAG "MediaCodecDecoder"

static constexpr int64_t kTimeoutUs = 5000;  // 5ms

MediaCodecDecoder::~MediaCodecDecoder() {
    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    if (extractor_) {
        AMediaExtractor_delete(extractor_);
        extractor_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool MediaCodecDecoder::Open(const std::string& path) {
    extractor_ = AMediaExtractor_new();
    if (AMediaExtractor_setDataSource(extractor_, path.c_str()) != AMEDIA_OK) {
        last_error_ = "AMediaExtractor_setDataSource failed";
        return false;
    }

    const size_t track_count = AMediaExtractor_getTrackCount(extractor_);
    bool found = false;
    const char* mime_type = nullptr;

    for (size_t i = 0; i < track_count && !found; ++i) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
        const char* mime = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime)
                && mime && strncmp(mime, "audio/", 6) == 0) {

            AMediaExtractor_selectTrack(extractor_, i);

            // フォーマット情報取得
            int32_t sr = 0, ch = 0;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);

            info_.sample_rate   = sr > 0 ? static_cast<uint32_t>(sr) : 44100;
            info_.channels      = ch > 0 ? static_cast<uint8_t>(ch) : 2;
            info_.bit_depth     = 0;  // lossy
            info_.is_lossless   = false;
            info_.bitrate_kbps  = 0;

            // トラック長の取得
            int64_t duration_us = 0;
            AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &duration_us);
            info_.duration_ms   = static_cast<uint32_t>(duration_us / 1000);
            info_.total_samples = static_cast<uint64_t>(duration_us)
                                  * info_.sample_rate / 1000000ULL;

            // フォーマット種別
            const size_t sep  = path.find_last_of("/\\");
            info_.filename    = (sep != std::string::npos) ? path.substr(sep+1) : path;
            info_.path        = path;

            if (strstr(mime, "opus")) {
                info_.format       = AudioFormat::WEBM;
                info_.codec_detail = "Opus (lossy)";
            } else if (strstr(mime, "vorbis")) {
                info_.format       = AudioFormat::WEBM;
                info_.codec_detail = "Vorbis (lossy)";
            } else {
                info_.format       = AudioFormat::AAC;
                info_.codec_detail = "AAC (lossy)";
            }

            mime_type = mime;
            found = true;

            // コーデック作成・設定・起動
            codec_ = AMediaCodec_createDecoderByType(mime_type);
            if (!codec_) {
                last_error_ = "AMediaCodec_createDecoderByType failed";
                AMediaFormat_delete(fmt);
                return false;
            }
            if (AMediaCodec_configure(codec_, fmt, nullptr, nullptr, 0) != AMEDIA_OK) {
                last_error_ = "AMediaCodec_configure failed";
                AMediaFormat_delete(fmt);
                return false;
            }
            AMediaCodec_start(codec_);
        }
        AMediaFormat_delete(fmt);
    }

    if (!found) { last_error_ = "no audio track"; return false; }

    info_.ComputeDerived();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrainOutput: コーデック出力キューを staging_ に移す
// ─────────────────────────────────────────────────────────────────────────────
bool MediaCodecDecoder::DrainOutput() {
    AMediaCodecBufferInfo buf_info;
    const ssize_t out_idx =
        AMediaCodec_dequeueOutputBuffer(codec_, &buf_info, kTimeoutUs);

    if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return true;
    if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)  return true;
    if (out_idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) return true;
    if (out_idx < 0) return true;

    if (buf_info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        AMediaCodec_releaseOutputBuffer(codec_, out_idx, false);
        output_eof_ = true;
        return false;
    }

    size_t out_size = 0;
    const uint8_t* out_buf = AMediaCodec_getOutputBuffer(codec_, out_idx, &out_size);
    if (out_buf && buf_info.size > 0) {
        const size_t samples = buf_info.size / sizeof(int16_t);
        const size_t old = staging_.size();
        staging_.resize(old + samples);
        std::memcpy(staging_.data() + old,
                    out_buf + buf_info.offset,
                    buf_info.size);
    }
    AMediaCodec_releaseOutputBuffer(codec_, out_idx, false);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
int64_t MediaCodecDecoder::Decode(void* buf, size_t max_frames) {
    if (!codec_ || output_eof_) return 0;

    const size_t need_samples = max_frames * info_.channels;

    // staging_ に十分溜まるまでポンプする
    while ((staging_.size() - staging_read_) < need_samples && !output_eof_) {

        // 入力側: エクストラクターからエンコードデータを送り込む
        if (!input_eof_) {
            const ssize_t in_idx =
                AMediaCodec_dequeueInputBuffer(codec_, kTimeoutUs);
            if (in_idx >= 0) {
                size_t in_size = 0;
                uint8_t* in_buf = AMediaCodec_getInputBuffer(codec_, in_idx, &in_size);
                const ssize_t n = AMediaExtractor_readSampleData(
                    extractor_, in_buf, in_size);

                if (n < 0) {
                    // EOF
                    AMediaCodec_queueInputBuffer(codec_, in_idx, 0, 0,
                        AMediaExtractor_getSampleTime(extractor_),
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    input_eof_ = true;
                } else {
                    AMediaCodec_queueInputBuffer(codec_, in_idx, 0, n,
                        AMediaExtractor_getSampleTime(extractor_), 0);
                    AMediaExtractor_advance(extractor_);
                }
            }
        }

        // 出力側: デコード済みデータを staging_ へ
        DrainOutput();
    }

    const size_t available = staging_.size() - staging_read_;
    if (available == 0) return 0;

    const size_t copy_samples = std::min(need_samples, available);
    const size_t frames_out   = copy_samples / info_.channels;
    std::memcpy(buf,
                staging_.data() + staging_read_,
                frames_out * info_.channels * sizeof(int16_t));
    staging_read_ += frames_out * info_.channels;

    position_ += frames_out;

    // 消費済みバッファを整理
    if (staging_read_ > 64 * 1024) {
        staging_.erase(staging_.begin(),
                       staging_.begin() + staging_read_);
        staging_read_ = 0;
    }

    return static_cast<int64_t>(frames_out);
}

// ─────────────────────────────────────────────────────────────────────────────
bool MediaCodecDecoder::Seek(uint64_t target_sample) {
    if (!extractor_ || !codec_) return false;

    const int64_t target_us =
        static_cast<int64_t>(target_sample) * 1000000LL / info_.sample_rate;

    // コーデックをフラッシュしてからシーク
    AMediaCodec_flush(codec_);
    staging_.clear();
    staging_read_ = 0;
    input_eof_  = false;
    output_eof_ = false;

    if (AMediaExtractor_seekTo(extractor_, target_us,
            AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC) != AMEDIA_OK) {
        last_error_ = "AMediaExtractor_seekTo failed";
        return false;
    }

    position_ = target_sample;
    return true;
}
