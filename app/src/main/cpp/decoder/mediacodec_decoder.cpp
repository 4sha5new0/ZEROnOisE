#include "mediacodec_decoder.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#define TAG "MediaCodecDecoder"

static constexpr int64_t kTimeoutUs = 10000;  // 10ms

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

bool MediaCodecDecoder::Open(const std::string& path) {
    extractor_ = AMediaExtractor_new();

    // setDataSource(path) は端末によってパーミッションエラーになるため
    // ファイルディスクリプタを渡す方式に変更
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        last_error_ = "open() failed: " + path;
        return false;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        last_error_ = "fstat() failed";
        ::close(fd);
        return false;
    }
    const media_status_t src_st = AMediaExtractor_setDataSourceFd(
        extractor_, fd, 0, static_cast<off64_t>(st.st_size));
    ::close(fd);
    if (src_st != AMEDIA_OK) {
        last_error_ = "AMediaExtractor_setDataSourceFd failed";
        return false;
    }

    const size_t track_count = AMediaExtractor_getTrackCount(extractor_);
    bool found = false;

    for (size_t i = 0; i < track_count && !found; ++i) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
        const char* mime = nullptr;
        if (!AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime)
                || !mime || strncmp(mime, "audio/", 6) != 0) {
            AMediaFormat_delete(fmt);
            continue;
        }

        AMediaExtractor_selectTrack(extractor_, i);

        int32_t sr = 0, ch = 0;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
        info_.sample_rate  = sr > 0 ? static_cast<uint32_t>(sr) : 44100;
        info_.channels     = ch > 0 ? static_cast<uint8_t>(ch)  : 2;
        info_.bit_depth    = 0;
        info_.is_lossless  = false;
        info_.bitrate_kbps = 0;

        int64_t duration_us = 0;
        AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &duration_us);
        info_.duration_ms   = static_cast<uint32_t>(duration_us / 1000);
        info_.total_samples = static_cast<uint64_t>(duration_us)
                              * info_.sample_rate / 1000000ULL;

        const size_t sep = path.find_last_of("/\\");
        info_.filename = (sep != std::string::npos) ? path.substr(sep + 1) : path;
        info_.path     = path;

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

        // mime を std::string にコピーしてから fmt 関連操作
        const std::string mime_str(mime);

        codec_ = AMediaCodec_createDecoderByType(mime_str.c_str());
        if (!codec_) {
            last_error_ = "createDecoderByType failed: " + mime_str;
            AMediaFormat_delete(fmt);
            return false;
        }
        if (AMediaCodec_configure(codec_, fmt, nullptr, nullptr, 0) != AMEDIA_OK) {
            last_error_ = "AMediaCodec_configure failed";
            AMediaFormat_delete(fmt);
            return false;
        }
        // start() の戻り値を必ずチェック
        if (AMediaCodec_start(codec_) != AMEDIA_OK) {
            last_error_ = "AMediaCodec_start failed";
            AMediaFormat_delete(fmt);
            return false;
        }
        AMediaFormat_delete(fmt);
        found = true;
    }

    if (!found) { last_error_ = "no audio track found"; return false; }
    info_.ComputeDerived();
    return true;
}

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
    const uint8_t* out_buf =
        AMediaCodec_getOutputBuffer(codec_, out_idx, &out_size);
    if (out_buf && buf_info.size > 0) {
        const size_t samples = buf_info.size / sizeof(int16_t);
        const size_t old     = staging_.size();
        staging_.resize(old + samples);
        std::memcpy(staging_.data() + old,
                    out_buf + buf_info.offset,
                    buf_info.size);
    }
    AMediaCodec_releaseOutputBuffer(codec_, out_idx, false);
    return true;
}

int64_t MediaCodecDecoder::Decode(void* buf, size_t max_frames) {
    if (!codec_ || output_eof_) return 0;

    const size_t need_samples = max_frames * info_.channels;

    while ((staging_.size() - staging_read_) < need_samples && !output_eof_) {
        // 入力
        if (!input_eof_) {
            const ssize_t in_idx =
                AMediaCodec_dequeueInputBuffer(codec_, kTimeoutUs);
            if (in_idx >= 0) {
                size_t in_size = 0;
                uint8_t* in_buf =
                    AMediaCodec_getInputBuffer(codec_, in_idx, &in_size);
                const ssize_t n =
                    AMediaExtractor_readSampleData(extractor_, in_buf, in_size);
                if (n <= 0) {
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
        // 出力
        DrainOutput();
    }

    const size_t available = staging_.size() - staging_read_;
    if (available == 0) {
        // output_eof_ == false のとき → コーデック未生成（ウォームアップ中）
        // DecoderThreadFunc は IsAtEOF() で判断するので 0 を返してよい
        return 0;
    }

    const size_t copy_samples = std::min(need_samples, available);
    const size_t frames_out   = copy_samples / info_.channels;
    std::memcpy(buf,
                staging_.data() + staging_read_,
                frames_out * info_.channels * sizeof(int16_t));
    staging_read_ += frames_out * info_.channels;
    position_     += frames_out;

    // 圧縮
    if (staging_read_ >= staging_.size()) {
        staging_.clear();
        staging_read_ = 0;
    } else if (staging_read_ > 32768) {
        staging_.erase(staging_.begin(),
                       staging_.begin() + staging_read_);
        staging_read_ = 0;
    }

    return static_cast<int64_t>(frames_out);
}

bool MediaCodecDecoder::Seek(uint64_t target_sample) {
    if (!extractor_ || !codec_) return false;
    const int64_t target_us =
        static_cast<int64_t>(target_sample) * 1000000LL / info_.sample_rate;
    AMediaCodec_flush(codec_);
    staging_.clear();
    staging_read_ = 0;
    input_eof_    = false;
    output_eof_   = false;
    if (AMediaExtractor_seekTo(extractor_, target_us,
            AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC) != AMEDIA_OK) {
        last_error_ = "AMediaExtractor_seekTo failed";
        return false;
    }
    position_ = target_sample;
    return true;
}
