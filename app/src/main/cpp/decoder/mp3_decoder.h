#pragma once
#include "decoder_base.h"
#include <memory>
#include <string>

// minimp3 の型を外部に漏らさないために Pimpl パターンを使用。
// MINIMP3_IMPLEMENTATION は mp3_decoder.cpp 内のみで定義する。
// (ヘッダーに MINIMP3_IMPLEMENTATION を書くと、include した全 .cpp で
//  実装が重複定義されてリンクエラーになる)

struct Mp3DecoderImpl;  // forward declaration

class Mp3Decoder : public DecoderBase {
public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    bool             Open(const std::string& path) override;
    const AudioInfo& GetInfo()       const override { return info_; }
    int64_t          Decode(void* buf, size_t max_frames) override;
    bool             Seek(uint64_t target_sample) override;
    uint64_t         GetPosition()   const override { return position_; }
    std::string      GetLastError()  const override { return last_error_; }

private:
    std::unique_ptr<Mp3DecoderImpl> impl_;   // minimp3 の実装を隠蔽
    AudioInfo   info_;
    uint64_t    position_   = 0;
    std::string last_error_;
};
