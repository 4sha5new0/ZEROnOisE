#pragma once
#include <string>
#include <unordered_map>
#include "../audio_info.h"

// VorbisComment タグマップ（key は大文字に正規化済み）から MQA 情報を検出する。
// LSB パターン検出は誤検出リスクがあるため採用しない。
class MQADetector {
public:
    using TagMap = std::unordered_map<std::string, std::string>;

    // info にMQA関連フィールドを書き込む。is_mqa = false の場合は何もしない。
    static void Detect(AudioInfo& info, const TagMap& tags);

private:
    static std::string ToUpper(const std::string& s);
};
