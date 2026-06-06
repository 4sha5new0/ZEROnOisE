#pragma once
#include "decoder_base.h"
#include <vector>
#include <cstdio>

// minimp3 は FetchContent でダウンロードされた minimp3.h を使用
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT 0   // int16_t 出力を使う
#include <minimp3.h>
#include <minimp3_ex.h>

class Mp3Decoder : public DecoderBase {
public:
    ~Mp3Decoder() override;
    bool        Open(const std::string& path) override;
    const AudioInfo& GetInfo()   const override { return info_; }
    int64_t     Decode(void* buf, size_t max_frames) override;
    bool        Seek(uint64_t target_sample) override;
    uint64_t    GetPosition()    const override { return position_; }
    std::string GetLastError()   const override { return last_error_; }

private:
    mp3dec_ex_t  dec_{};
    AudioInfo    info_;
    uint64_t     position_  = 0;
    bool         opened_    = false;
    std::string  last_error_;
};
