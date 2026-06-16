#include "flac_decoder.h"
#include <cstring>
#include <cctype>     // toupper (MetadataCallback 内のラムダで使用)
#include <algorithm>
#include <android/log.h>
#define TAG "FlacDecoder"

FlacDecoder::~FlacDecoder() {
    if (dec_) {
        FLAC__stream_decoder_finish(dec_);
        FLAC__stream_decoder_delete(dec_);
        dec_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool FlacDecoder::Open(const std::string& path) {
    dec_ = FLAC__stream_decoder_new();
    if (!dec_) { last_error_ = "FLAC__stream_decoder_new failed"; return false; }

    info_.path = path;
    const size_t sep = path.find_last_of("/\\");
    info_.filename   = (sep != std::string::npos) ? path.substr(sep+1) : path;
    info_.format     = AudioFormat::FLAC;
    info_.is_lossless = true;

    // MD5 チェック有効
    FLAC__stream_decoder_set_md5_checking(dec_, true);

    const FLAC__StreamDecoderInitStatus s =
        FLAC__stream_decoder_init_file(dec_, path.c_str(),
            WriteCallback, MetadataCallback, ErrorCallback, this);
    if (s != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        last_error_ = FLAC__StreamDecoderInitStatusString[s];
        return false;
    }

    // STREAMINFO メタデータを取得（MetadataCallback が呼ばれる）
    if (!FLAC__stream_decoder_process_until_end_of_metadata(dec_)) {
        last_error_ = "metadata read failed"; return false;
    }

    info_.ComputeDerived();
    return info_.IsValid();
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteCallback
//  libFLAC がブロックをデコードするたびに呼ばれる。
//  staging_ バッファに bytes をアペンドする。
// ─────────────────────────────────────────────────────────────────────────────
FLAC__StreamDecoderWriteStatus FlacDecoder::WriteCallback(
        const FLAC__StreamDecoder*,
        const FLAC__Frame* frame,
        const FLAC__int32* const buffer[],
        void* client_data) {

    auto* self = static_cast<FlacDecoder*>(client_data);
    const uint32_t n        = frame->header.blocksize;
    const uint8_t  channels = static_cast<uint8_t>(frame->header.channels);
    const uint8_t  bps      = static_cast<uint8_t>(frame->header.bits_per_sample);

    if (bps <= 16) {
        // PCM_I16 出力
        const size_t bytes    = n * channels * sizeof(int16_t);
        const size_t old_size = self->staging_.size();
        self->staging_.resize(old_size + bytes);
        int16_t* dst = reinterpret_cast<int16_t*>(self->staging_.data() + old_size);
        for (uint32_t i = 0; i < n; ++i)
            for (uint8_t c = 0; c < channels; ++c)
                *dst++ = static_cast<int16_t>(buffer[c][i]);
    } else {
        // PCM_FLOAT 出力（API 29 で PCM_I32 が使えないため）
        // 24bit: normalize = 1/2^23, 32bit: normalize = 1/2^31
        const float scale = (bps == 24)
            ? (1.0f / 8388608.0f)    // 2^23
            : (1.0f / 2147483648.0f);// 2^31

        const size_t bytes    = n * channels * sizeof(float);
        const size_t old_size = self->staging_.size();
        self->staging_.resize(old_size + bytes);
        float* dst = reinterpret_cast<float*>(self->staging_.data() + old_size);
        for (uint32_t i = 0; i < n; ++i)
            for (uint8_t c = 0; c < channels; ++c)
                *dst++ = static_cast<float>(buffer[c][i]) * scale;
    }

    self->position_ += n;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

// ─────────────────────────────────────────────────────────────────────────────
void FlacDecoder::MetadataCallback(
        const FLAC__StreamDecoder*,
        const FLAC__StreamMetadata* meta,
        void* client_data) {

    auto* self = static_cast<FlacDecoder*>(client_data);

    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        const auto& si = meta->data.stream_info;
        self->info_.sample_rate    = si.sample_rate;
        self->info_.channels       = static_cast<uint8_t>(si.channels);
        self->info_.bit_depth      = static_cast<uint8_t>(si.bits_per_sample);
        self->info_.total_samples  = si.total_samples;
        self->info_.duration_ms    = (si.sample_rate > 0)
            ? static_cast<uint32_t>(si.total_samples * 1000ULL / si.sample_rate) : 0;
    } else if (meta->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        const auto& vc = meta->data.vorbis_comment;
        for (uint32_t i = 0; i < vc.num_comments; ++i) {
            const char* entry = reinterpret_cast<const char*>(vc.comments[i].entry);
            // タグを大文字小文字無視で比較するためのラムダ
            auto starts_with_ci = [&](const char* prefix) {
                const size_t len = strlen(prefix);
                for (size_t j = 0; j < len && entry[j]; ++j)
                    if (toupper((unsigned char)entry[j]) != toupper((unsigned char)prefix[j]))
                        return false;
                return entry[len] == '=';
            };
            auto value_of = [&](const char* prefix) -> std::string {
                const size_t len = strlen(prefix) + 1;  // +1 for '='
                return std::string(entry + len);
            };

            if (starts_with_ci("TITLE"))              self->info_.title   = value_of("TITLE");
            else if (starts_with_ci("ARTIST"))        self->info_.artist  = value_of("ARTIST");
            else if (starts_with_ci("ALBUM"))         self->info_.album   = value_of("ALBUM");
            else if (starts_with_ci("DATE"))          self->info_.date    = value_of("DATE");
            else if (starts_with_ci("MQAENCODER")) {
                self->info_.is_mqa = true;
                self->info_.mqa_encoder = value_of("MQAENCODER");
            } else if (starts_with_ci("ORIGINALSAMPLERATE")) {
                try {
                    self->info_.mqa_original_sr =
                        static_cast<uint32_t>(std::stoul(value_of("ORIGINALSAMPLERATE")));
                    self->info_.is_mqa_studio = true;
                } catch (...) {}
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void FlacDecoder::ErrorCallback(
        const FLAC__StreamDecoder*,
        FLAC__StreamDecoderErrorStatus status,
        void* client_data) {
    auto* self = static_cast<FlacDecoder*>(client_data);
    self->last_error_ = FLAC__StreamDecoderErrorStatusString[status];
    __android_log_print(ANDROID_LOG_ERROR, TAG, "FLAC error: %s", self->last_error_.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Decode: staging_ に十分なデータが溜まるまで process_single() を繰り返す
// ─────────────────────────────────────────────────────────────────────────────
int64_t FlacDecoder::Decode(void* buf, size_t max_frames) {
    if (!dec_ || eof_) return 0;

    const size_t need_bytes = max_frames * static_cast<size_t>(info_.bytes_per_frame);

    // staging_ の先読みバイト数が足りるまでデコードを進める
    while ((staging_.size() - staging_read_) < need_bytes) {
        if (!FLAC__stream_decoder_process_single(dec_)) { eof_ = true; break; }
        const auto state = FLAC__stream_decoder_get_state(dec_);
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
            state == FLAC__STREAM_DECODER_ABORTED) {
            eof_ = true; break;
        }
    }

    const size_t available = staging_.size() - staging_read_;
    if (available == 0) return 0;

    const size_t copy_bytes = std::min(need_bytes, available);
    const size_t frames_out = copy_bytes / static_cast<size_t>(info_.bytes_per_frame);
    std::memcpy(buf, staging_.data() + staging_read_, frames_out * info_.bytes_per_frame);
    staging_read_ += frames_out * info_.bytes_per_frame;

    // staging_ 圧縮戦略:
    //   192kHz/24bit では 1 ブロック = 4096 * 2ch * 4bytes = 32768 bytes。
    //   旧コードの 4MB 閾値では 2.6 秒ごとに O(4MB) erase が発生し、
    //   デコーダースレッドが止まってアンダーラン→飛び飛び再生になっていた。
    //   → 全消費なら即 clear()、残りがあれば 32KB 以上で erase（小さく速い）。
    if (staging_read_ >= staging_.size()) {
        staging_.clear();
        staging_read_ = 0;
    } else if (staging_read_ > 32768) {
        staging_.erase(staging_.begin(), staging_.begin() + staging_read_);
        staging_read_ = 0;
    }

    return static_cast<int64_t>(frames_out);
}

// ─────────────────────────────────────────────────────────────────────────────
bool FlacDecoder::Seek(uint64_t target_sample) {
    if (!dec_) return false;
    staging_.clear();
    staging_read_ = 0;
    eof_ = false;
    if (!FLAC__stream_decoder_seek_absolute(dec_, target_sample)) {
        last_error_ = "FLAC seek failed"; return false;
    }
    position_ = target_sample;
    return true;
}
