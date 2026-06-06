#pragma once
#include "decoder_base.h"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <vector>

class MediaCodecDecoder : public DecoderBase {
public:
    ~MediaCodecDecoder() override;
    bool        Open(const std::string& path) override;
    const AudioInfo& GetInfo()   const override { return info_; }
    int64_t     Decode(void* buf, size_t max_frames) override;
    bool        Seek(uint64_t target_sample) override;
    uint64_t    GetPosition()    const override { return position_; }
    std::string GetLastError()   const override { return last_error_; }

private:
    bool DrainOutput();  // コーデックの出力バッファをステージングバッファに移す

    AMediaExtractor* extractor_ = nullptr;
    AMediaCodec*     codec_     = nullptr;
    AudioInfo        info_;
    uint64_t         position_  = 0;
    bool             input_eof_ = false;
    bool             output_eof_ = false;
    std::string      last_error_;

    // デコード済み PCM_I16 サンプルのステージングバッファ
    std::vector<int16_t> staging_;
    size_t               staging_read_ = 0;
};
