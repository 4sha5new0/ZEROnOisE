#pragma once
#include "../audio_info.h"
#include <string>
#include <vector>

struct FileEntry {
    std::string name;
    std::string full_path;
    bool        is_directory = false;
    int64_t     size_bytes   = 0;
    int64_t     modified_ms  = 0;

    // ファイルのみ（メタデータ先読み後に設定）
    AudioInfo   info;
    bool        meta_loaded  = false;
};

class FileScanner {
public:
    // ディレクトリを同期スキャンしてエントリ一覧を返す（メタデータは後から非同期で埋める）
    static std::vector<FileEntry> ScanDirectory(const std::string& dir_path);

    // dir_path 内で current_path の次のファイルを返す。末尾の場合は空文字列。
    static std::string GetNextFile(const std::string& current_path);

    // dir_path 内で current_path の前のファイルを返す。先頭の場合は空文字列。
    static std::string GetPrevFile(const std::string& current_path);

    // 対応フォーマットの拡張子か
    static bool IsSupportedExtension(const std::string& filename);

    // ストレージルートのリスト（内部 / microSD）
    static std::vector<std::string> GetStorageRoots();

private:
    static std::vector<std::string> ListAudioFiles(const std::string& dir_path);
};
