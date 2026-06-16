#pragma once
#include <string>
#include <cstdint>
#include "../audio_info.h"

class DecoderBase {
public:
    virtual ~DecoderBase() = default;

    virtual bool Open(const std::string& path) = 0;
    virtual const AudioInfo& GetInfo() const = 0;

    // 返値: デコードしたフレーム数 / 0 = EOF または未生成 / -1 = エラー
    // 0 が EOF かどうかは IsAtEOF() で判定する。
    virtual int64_t Decode(void* output_buffer, size_t max_frames) = 0;

    virtual bool     Seek(uint64_t target_sample) = 0;
    virtual uint64_t GetPosition() const = 0;
    virtual std::string GetLastError() const = 0;

    // デコーダーが本当に EOF に達しているか。
    // Decode() が 0 を返したとき:
    //   IsAtEOF() == true  → 真の EOF → 次曲へ
    //   IsAtEOF() == false → コーデック未生成 → 少し待ってリトライ
    virtual bool IsAtEOF() const { return false; }
};
