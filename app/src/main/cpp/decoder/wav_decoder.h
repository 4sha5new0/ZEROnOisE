#pragma once
#include "decoder_base.h"
#include <cstdio>
#include <vector>

class WavDecoder : public DecoderBase {
public:
    ~WavDecoder() override;
    bool        Open(const std::string& path) override;
    const AudioInfo& GetInfo()   const override { return info_; }
    int64_t     Decode(void* buf, size_t max_frames) override;
    bool        Seek(uint64_t target_sample) override;
    uint64_t    GetPosition()    const override { return position_; }
    std::string GetLastError()   const override { return last_error_; }

private:
    bool ParseHeader();

    FILE*                fp_          = nullptr;
    AudioInfo            info_;
    int64_t              data_offset_ = 0;
    uint64_t             data_size_   = 0;
    uint64_t             position_    = 0;
    bool                 is_float_    = false; // 32bit IEEE float WAV
    std::vector<uint8_t> raw_buf_;             // 24/32bit 変換用バッファ（毎回確保を避ける）
    std::string          last_error_;
};
