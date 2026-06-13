#pragma once
#include <string>
#include <cstdint>
#include <aaudio/AAudio.h>

// ─────────────────────────────────────────────────────────────────────────────
// AudioFormat
// ─────────────────────────────────────────────────────────────────────────────
enum class AudioFormat : uint8_t {
    UNKNOWN = 0,
    FLAC    = 1,
    WAV     = 2,
    MP3     = 3,
    AAC     = 4,   // .aac (ADTS) / .m4a (MP4)
    WEBM    = 5    // .webm (Opus or Vorbis)
};

inline const char* FormatName(AudioFormat f) {
    switch (f) {
        case AudioFormat::FLAC: return "FLAC";
        case AudioFormat::WAV:  return "WAV";
        case AudioFormat::MP3:  return "MP3";
        case AudioFormat::AAC:  return "AAC";
        case AudioFormat::WEBM: return "WebM";
        default:                return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AudioInfo
//   完全に解析済みのファイル情報。ファイルを開いた直後に MetadataReader が設定する。
// ─────────────────────────────────────────────────────────────────────────────
struct AudioInfo {
    // ── ファイル基本 ──────────────────────────────────────────────────────────
    std::string path;
    std::string filename;           // path から取り出したファイル名
    int64_t     file_size   = 0;    // bytes

    // ── オーディオ基本 ────────────────────────────────────────────────────────
    AudioFormat format       = AudioFormat::UNKNOWN;
    uint32_t    sample_rate  = 44100;
    uint8_t     bit_depth    = 16;   // lossless のみ有効。lossy は 0
    uint8_t     channels     = 2;
    uint32_t    duration_ms  = 0;
    uint64_t    total_samples = 0;

    // ── 品質フラグ ────────────────────────────────────────────────────────────
    bool        is_lossless  = false;
    bool        is_hires     = false;  // lossless && SR>=88200 && bit>=24

    // ── Lossy 専用 ────────────────────────────────────────────────────────────
    uint32_t    bitrate_kbps = 0;
    std::string codec_detail;       // e.g. "Opus", "Vorbis", "FLAC (Lv.5)"

    // ── MQA ──────────────────────────────────────────────────────────────────
    bool        is_mqa           = false;
    bool        is_mqa_studio    = false;
    uint32_t    mqa_original_sr  = 0;
    std::string mqa_encoder;

    // ── タグ ─────────────────────────────────────────────────────────────────
    std::string title;
    std::string artist;
    std::string album;
    std::string date;
    uint32_t    track_number = 0;

    // ── AAudio ストリーム設定（ComputeDerived() で算出）────────────────────────
    aaudio_format_t aaudio_format = AAUDIO_FORMAT_PCM_I16;
    int32_t         bytes_per_frame = 4;  // channels × bytes_per_sample

    // ────────────────────────────────────────────────────────────────────────
    // ComputeDerived(): bit_depth / is_lossless / channels から導出値を計算する。
    // 各デコーダーが AudioInfo を設定した後に必ず呼ぶこと。
    // ────────────────────────────────────────────────────────────────────────
    void ComputeDerived() {
        is_hires = is_lossless && sample_rate >= 88200 && bit_depth >= 24;

        if (!is_lossless) {
            // MP3 / AAC / WebM: MediaCodec / minimp3 は PCM_I16 で出力
            aaudio_format  = AAUDIO_FORMAT_PCM_I16;
            bytes_per_frame = 2 * channels;
        } else {
            switch (bit_depth) {
                case 16:
                    // 16bit: PCM_I16（ビットパーフェクト）
                    aaudio_format  = AAUDIO_FORMAT_PCM_I16;
                    bytes_per_frame = 2 * channels;
                    break;
                case 24:
                    // AAUDIO_FORMAT_PCM_I32 は API 31 (Android 12) 以降のみ対応。
                    // M3 Ultra = Android 10 (API 29) では使用不可。
                    // PCM_FLOAT を使う。float32 の仮数部は 23bit + 暗黙の 1bit = 24bit 相当。
                    // 24bit 整数 [-8388608, 8388607] は float32 で誤差ゼロで表現可能。
                    aaudio_format  = AAUDIO_FORMAT_PCM_FLOAT;
                    bytes_per_frame = 4 * channels;
                    break;
                case 32:
                    // 32bit int も PCM_FLOAT で送る（float32 仮数部 < 32bit のため
                    // 下位 9bit が丸められるが、32bit lossless 音源は極めて希少）
                    aaudio_format  = AAUDIO_FORMAT_PCM_FLOAT;
                    bytes_per_frame = 4 * channels;
                    break;
                default:
                    aaudio_format  = AAUDIO_FORMAT_PCM_I16;
                    bytes_per_frame = 2 * channels;
                    break;
            }
        }
    }

    bool IsValid() const { return format != AudioFormat::UNKNOWN && sample_rate > 0; }
};
