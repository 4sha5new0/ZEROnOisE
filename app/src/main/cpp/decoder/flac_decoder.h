#pragma once
#include "decoder_base.h"
#include <FLAC/stream_decoder.h>
#include <vector>
#include <mutex>

class FlacDecoder : public DecoderBase {
public:
    ~FlacDecoder() override;
    bool        Open(const std::string& path) override;
    const AudioInfo& GetInfo()   const override { return info_; }
    int64_t     Decode(void* buf, size_t max_frames) override;
    bool        Seek(uint64_t target_sample) override;
    uint64_t    GetPosition()    const override { return position_; }
    std::string GetLastError()   const override { return last_error_; }

private:
    // ── libFLAC コールバック（static）──────────────────────────────────────
    static FLAC__StreamDecoderWriteStatus  WriteCallback(
        const FLAC__StreamDecoder*, const FLAC__Frame*,
        const FLAC__int32* const[], void*);
    static void MetadataCallback(
        const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*);
    static void ErrorCallback(
        const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*);

    FLAC__StreamDecoder* dec_     = nullptr;
    AudioInfo            info_;
    uint64_t             position_ = 0;
    bool                 eof_      = false;
    std::string          last_error_;

    // デコードコールバック → Decode() 呼び出し元に渡すステージングバッファ
    // Decode() はこのバッファに必要量が溜まるまで process_single() をループする
    std::vector<uint8_t> staging_;      // int32_t / int16_t バイト列
    size_t               staging_read_  = 0;  // staging_ 内の読み取り位置
};
