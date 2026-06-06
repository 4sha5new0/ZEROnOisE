#include "mp3_decoder.h"
#include <algorithm>

Mp3Decoder::~Mp3Decoder() {
    if (opened_) mp3dec_ex_close(&dec_);
}

bool Mp3Decoder::Open(const std::string& path) {
    if (mp3dec_ex_open(&dec_, path.c_str(), MP3D_SEEK_TO_SAMPLE) != 0) {
        last_error_ = "mp3dec_ex_open failed";
        return false;
    }
    opened_ = true;

    info_.path       = path;
    const size_t sep = path.find_last_of("/\\");
    info_.filename   = (sep != std::string::npos) ? path.substr(sep + 1) : path;
    info_.format     = AudioFormat::MP3;
    info_.is_lossless = false;
    info_.sample_rate = dec_.info.hz;
    info_.channels    = static_cast<uint8_t>(dec_.info.channels);
    info_.bit_depth   = 0;  // lossy
    info_.bitrate_kbps = dec_.info.bitrate_kbps;
    info_.total_samples = dec_.samples / dec_.info.channels;
    info_.duration_ms  = (dec_.info.hz > 0)
        ? static_cast<uint32_t>(info_.total_samples * 1000ULL / dec_.info.hz) : 0;
    info_.codec_detail = "MP3";
    info_.ComputeDerived();

    position_ = 0;
    return true;
}

int64_t Mp3Decoder::Decode(void* buf, size_t max_frames) {
    if (!opened_) return -1;
    if (position_ >= info_.total_samples) return 0;

    // mp3dec_ex_read は サンプル数（channels 込み）を要求する
    const size_t samples_to_read = max_frames * info_.channels;
    mp3d_sample_t* out = static_cast<mp3d_sample_t*>(buf);
    const size_t n = mp3dec_ex_read(&dec_, out, samples_to_read);
    if (n == 0) return 0;
    const size_t frames_read = n / info_.channels;
    position_ += frames_read;
    return static_cast<int64_t>(frames_read);
}

bool Mp3Decoder::Seek(uint64_t target_sample) {
    if (!opened_) return false;
    target_sample = std::min(target_sample, info_.total_samples);
    // MP3D_SEEK_TO_SAMPLE で開いているので sample 単位シークが有効
    const uint64_t offset = target_sample * info_.channels;
    if (mp3dec_ex_seek(&dec_, offset) != 0) {
        last_error_ = "seek failed";
        return false;
    }
    position_ = target_sample;
    return true;
}
