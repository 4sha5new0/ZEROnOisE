#pragma once
#include <string>
#include <cstdint>
#include "../audio_info.h"

// ─────────────────────────────────────────────────────────────────────────────
// DecoderBase
//
//  全デコーダーが実装する純粋仮想インターフェース。
//  Decode() の出力フォーマットは info.aaudio_format に従う。
//
//  スレッドモデル:
//   ・Open / Seek / Decode / GetPosition は Decoder Thread から呼ぶ。
//   ・GetInfo は任意スレッドから読み取り専用で呼べる（書き込みは Open 時のみ）。
// ─────────────────────────────────────────────────────────────────────────────
class DecoderBase {
public:
    virtual ~DecoderBase() = default;

    // ファイルを開いてメタデータを読み込む。成功時 true。
    virtual bool Open(const std::string& path) = 0;

    // フォーマット情報（Open 後に有効）
    virtual const AudioInfo& GetInfo() const = 0;

    // フレーム単位でデコード。
    //   output_buffer: AudioInfo.bytes_per_frame × max_frames バイトのバッファ
    //   返値: デコードしたフレーム数 / 0 = EOF / -1 = エラー
    virtual int64_t Decode(void* output_buffer, size_t max_frames) = 0;

    // サンプル単位でシーク。Decoder Thread から呼ぶ。
    // シーク後は次の Decode() から新しい位置のデータが返る。
    virtual bool Seek(uint64_t target_sample) = 0;

    // 現在の再生位置（サンプル単位）
    virtual uint64_t GetPosition() const = 0;

    // 最後のエラーの説明文
    virtual std::string GetLastError() const = 0;
};
