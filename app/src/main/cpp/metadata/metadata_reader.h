#pragma once
#include "../audio_info.h"
#include <string>

// ファイルパスから AudioInfo を構築する。
// フォーマット判定 → 対応デコーダーでヘッダーのみ読み込む。
// 再生には使わず、ファイルブラウザのメタデータ表示用途専用。
class MetadataReader {
public:
    // path のメタデータを読んで AudioInfo を返す。失敗時は is_valid=false
    static AudioInfo Read(const std::string& path);

private:
    static AudioFormat DetectFormat(const std::string& path);
};
