// MINIMP3_IMPLEMENTATION はこのファイルだけで定義する (pimpl の肝)
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT 0   // int16_t 出力
#include "minimp3_ex.h"          // third_party/minimp3/ 経由で include される

#include "mp3_decoder.h"
#include <algorithm>
#include <android/log.h>
#define TAG "Mp3Decoder"

// minimp3 の実装を完全に隠蔽する内部構造体
struct Mp3DecoderImpl {
    mp3dec_ex_t dec{};
    bool        opened = false;
};

Mp3Decoder::Mp3Decoder()
    : impl_(std::make_unique<Mp3DecoderImpl>())
{}

Mp3Decoder::~Mp3Decoder() {
    if (impl_ && impl_->opened) {
        mp3dec_ex_close(&impl_->dec);
    }
}

bool Mp3Decoder::Open(const std::string& path) {
    if (mp3dec_ex_open(&impl_->dec, path.c_str(), MP3D_SEEK_TO_SAMPLE) != 0) {
        last_error_ = "mp3dec_ex_open failed";
        return false;
    }
    impl_->opened = true;

    info_.path        = path;
    const size_t sep  = path.find_last_of("/\\");
    info_.filename    = (sep != std::string::npos) ? path.substr(sep + 1) : path;
    info_.format      = AudioFormat::MP3;
    info_.is_lossless = false;
    info_.sample_rate = static_cast<uint32_t>(impl_->dec.info.hz);
    info_.channels    = static_cast<uint8_t>(impl_->dec.info.channels);
    info_.bit_depth   = 0;  // lossy
    info_.bitrate_kbps = static_cast<uint32_t>(impl_->dec.info.bitrate_kbps);
    info_.total_samples = impl_->dec.samples
                          / static_cast<uint64_t>(impl_->dec.info.channels);
    info_.duration_ms = (impl_->dec.info.hz > 0)
        ? static_cast<uint32_t>(info_.total_samples * 1000ULL / impl_->dec.info.hz)
        : 0;
    info_.codec_detail = "MP3 (lossy)";
    info_.ComputeDerived();

    position_ = 0;
    return true;
}

int64_t Mp3Decoder::Decode(void* buf, size_t max_frames) {
    if (!impl_->opened) return -1;
    if (position_ >= info_.total_samples) return 0;

    const size_t samples_to_read =
        max_frames * static_cast<size_t>(info_.channels);

    mp3d_sample_t* out = static_cast<mp3d_sample_t*>(buf);
    const size_t n = mp3dec_ex_read(&impl_->dec, out, samples_to_read);
    if (n == 0) return 0;

    const size_t frames_read = n / static_cast<size_t>(info_.channels);
    position_ += frames_read;
    return static_cast<int64_t>(frames_read);
}

bool Mp3Decoder::Seek(uint64_t target_sample) {
    if (!impl_->opened) return false;
    target_sample = std::min(target_sample, info_.total_samples);
    const uint64_t offset =
        target_sample * static_cast<uint64_t>(info_.channels);
    if (mp3dec_ex_seek(&impl_->dec, offset) != 0) {
        last_error_ = "seek failed";
        return false;
    }
    position_ = target_sample;
    return true;
}
