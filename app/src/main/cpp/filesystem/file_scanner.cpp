#include "file_scanner.h"
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <android/log.h>
#define TAG "FileScanner"

static const char* const kSupportedExts[] = {
    "flac", "wav", "wave", "mp3", "aac", "m4a", "webm", nullptr
};

bool FileScanner::IsSupportedExtension(const std::string& filename) {
    const size_t dot = filename.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    for (int i = 0; kSupportedExts[i]; ++i)
        if (ext == kSupportedExts[i]) return true;
    return false;
}

std::vector<FileEntry> FileScanner::ScanDirectory(const std::string& dir_path) {
    std::vector<FileEntry> result;
    DIR* dp = opendir(dir_path.c_str());
    if (!dp) return result;

    dirent* ep;
    while ((ep = readdir(dp)) != nullptr) {
        const std::string name = ep->d_name;
        if (name == "." || name == "..") continue;
        if (name[0] == '.') continue;  // 隠しファイルは除外

        FileEntry fe;
        fe.name      = name;
        fe.full_path = dir_path + "/" + name;

        struct stat st;
        if (stat(fe.full_path.c_str(), &st) == 0) {
            fe.size_bytes  = st.st_size;
            fe.modified_ms = static_cast<int64_t>(st.st_mtime) * 1000;
        }

        if (ep->d_type == DT_DIR) {
            fe.is_directory = true;
            result.push_back(std::move(fe));
        } else if (ep->d_type == DT_REG || ep->d_type == DT_UNKNOWN) {
            if (IsSupportedExtension(name)) {
                fe.is_directory = false;
                result.push_back(std::move(fe));
            }
            // 非対応ファイルは追加しない（10.3 の仕様）
        }
    }
    closedir(dp);

    // ディレクトリ先頭、その後ファイルをアルファベット順
    std::sort(result.begin(), result.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_directory != b.is_directory) return a.is_directory > b.is_directory;
        return a.name < b.name;
    });
    return result;
}

std::vector<std::string> FileScanner::ListAudioFiles(const std::string& dir_path) {
    std::vector<std::string> files;
    DIR* dp = opendir(dir_path.c_str());
    if (!dp) return files;
    dirent* ep;
    while ((ep = readdir(dp)) != nullptr) {
        const std::string name = ep->d_name;
        if (name[0] == '.') continue;
        if ((ep->d_type == DT_REG || ep->d_type == DT_UNKNOWN)
                && IsSupportedExtension(name)) {
            files.push_back(name);
        }
    }
    closedir(dp);
    std::sort(files.begin(), files.end());
    return files;
}

std::string FileScanner::GetNextFile(const std::string& current_path) {
    const size_t sep = current_path.rfind('/');
    if (sep == std::string::npos) return {};
    const std::string dir  = current_path.substr(0, sep);
    const std::string name = current_path.substr(sep + 1);

    const auto files = ListAudioFiles(dir);
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] == name && i + 1 < files.size())
            return dir + "/" + files[i + 1];
    }
    return {};
}

std::string FileScanner::GetPrevFile(const std::string& current_path) {
    const size_t sep = current_path.rfind('/');
    if (sep == std::string::npos) return {};
    const std::string dir  = current_path.substr(0, sep);
    const std::string name = current_path.substr(sep + 1);

    const auto files = ListAudioFiles(dir);
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] == name && i > 0)
            return dir + "/" + files[i - 1];
    }
    return {};
}

std::vector<std::string> FileScanner::GetStorageRoots() {
    // Shanling M3 Ultra の標準パス
    std::vector<std::string> roots;
    const char* internal = "/sdcard";
    struct stat st;
    if (stat(internal, &st) == 0) roots.push_back(internal);

    // microSD: /storage/<UUID>/ を探す
    DIR* dp = opendir("/storage");
    if (dp) {
        dirent* ep;
        while ((ep = readdir(dp)) != nullptr) {
            const std::string n = ep->d_name;
            if (n == "." || n == ".." || n == "emulated" || n == "self") continue;
            const std::string p = std::string("/storage/") + n;
            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                roots.push_back(p);
        }
        closedir(dp);
    }
    return roots;
}
