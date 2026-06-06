#include "wav_decoder.h"
#include <cstring>
#include <android/log.h>
#define TAG "WavDecoder"

WavDecoder::~WavDecoder() {
    if (fp_) { fclose(fp_); fp_ = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// 内部ヘルパー
// ─────────────────────────────────────────────────────────────────────────────
static uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static uint32_t read_u32le(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}
static uint64_t read_u64le(const uint8_t* p) {
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p+4) << 32);
}

// ─────────────────────────────────────────────────────────────────────────────
bool WavDecoder::Open(const std::string& path) {
    fp_ = fopen(path.c_str(), "rb");
    if (!fp_) { last_error_ = "fopen failed"; return false; }

    info_.path = path;
    const size_t sep = path.find_last_of("/\\");
    info_.filename = (sep != std::string::npos) ? path.substr(sep + 1) : path;
    info_.format = AudioFormat::WAV;
    info_.is_lossless = true;

    if (!ParseHeader()) return false;
    info_.ComputeDerived();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RFC 2361 / EBU Tech 3306 / Microsoft RIFF/RF64 ヘッダー解析
// ─────────────────────────────────────────────────────────────────────────────
bool WavDecoder::ParseHeader() {
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, fp_) != 12) { last_error_ = "header read fail"; return false; }

    bool is_rf64 = false;
    uint64_t rf64_data_size = 0;

    if (memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr+8, "WAVE", 4) == 0) {
        is_rf64 = false;
    } else if (memcmp(hdr, "RF64", 4) == 0 && memcmp(hdr+8, "WAVE", 4) == 0) {
        is_rf64 = true;
    } else {
        last_error_ = "not a WAV file";
        return false;
    }

    // チャンクを順に読む
    bool got_fmt = false, got_data = false;
    uint8_t chunk_hdr[8];

    while (fread(chunk_hdr, 1, 8, fp_) == 8) {
        const uint32_t chunk_size = read_u32le(chunk_hdr + 4);
        const long chunk_start = ftell(fp_);

        if (memcmp(chunk_hdr, "ds64", 4) == 0) {
            // RF64 拡張サイズチャンク
            uint8_t ds64[28];
            if (fread(ds64, 1, 28, fp_) == 28) {
                rf64_data_size = read_u64le(ds64 + 8);
            }
        } else if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint8_t fmt[40] = {};
            const size_t to_read = std::min((size_t)chunk_size, sizeof(fmt));
            if (fread(fmt, 1, to_read, fp_) < 16) {
                last_error_ = "fmt chunk too small"; return false;
            }
            const uint16_t audio_format = read_u16le(fmt);
            info_.channels    = static_cast<uint8_t>(read_u16le(fmt + 2));
            info_.sample_rate = read_u32le(fmt + 4);
            info_.bit_depth   = static_cast<uint8_t>(read_u16le(fmt + 14));
            info_.bitrate_kbps = 0;

            if (audio_format == 3) {
                // IEEE float
                info_.bit_depth = 32;
            } else if (audio_format == 0xFFFE) {
                // EXTENSIBLE: SubFormat は offset 24
                if (to_read >= 26) {
                    const uint16_t sub_fmt = read_u16le(fmt + 24);
                    if (sub_fmt == 3) info_.bit_depth = 32; // float
                }
            } else if (audio_format != 1) {
                last_error_ = "unsupported WAV format";
                return false;
            }
            got_fmt = true;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_offset_ = ftell(fp_);
            data_size_   = is_rf64 ? rf64_data_size : chunk_size;
            got_data = true;
            break;
        }

        // 次のチャンクへ（chunk_size が奇数の場合はパッド1バイト）
        const long next = chunk_start + (long)chunk_size + (chunk_size & 1);
        if (fseek(fp_, next, SEEK_SET) != 0) break;
    }

    if (!got_fmt || !got_data) {
        last_error_ = "missing fmt or data chunk"; return false;
    }

    const uint32_t bytes_per_sample_file = (info_.bit_depth + 7) / 8;
    const uint32_t frame_size_file = bytes_per_sample_file * info_.channels;
    info_.total_samples = (frame_size_file > 0) ? data_size_ / frame_size_file : 0;
    info_.duration_ms   = (info_.sample_rate > 0)
        ? static_cast<uint32_t>(info_.total_samples * 1000ULL / info_.sample_rate) : 0;

    fseek(fp_, data_offset_, SEEK_SET);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decode
//  WAV は PCM 生データなのでコピーするだけ。
//  24bit の場合は 3バイト → 4バイト（上位 24bit 詰め PCM_I32）に変換する。
// ─────────────────────────────────────────────────────────────────────────────
int64_t WavDecoder::Decode(void* output_buffer, size_t max_frames) {
    if (!fp_ || position_ >= info_.total_samples) return 0;

    const size_t frames_left = info_.total_samples - position_;
    const size_t frames_to_read = std::min(max_frames, frames_left);

    const uint32_t file_bytes_per_sample = (info_.bit_depth + 7) / 8;
    const uint32_t file_frame_size       = file_bytes_per_sample * info_.channels;

    if (info_.bit_depth == 24) {
        // 24bit: ファイルから 3バイト×ch 読み込み → int32_t 上位 24bit に詰める
        std::vector<uint8_t> raw(frames_to_read * file_frame_size);
        const size_t n = fread(raw.data(), file_frame_size, frames_to_read, fp_);
        int32_t* out = static_cast<int32_t*>(output_buffer);
        for (size_t i = 0; i < n * info_.channels; ++i) {
            const uint8_t* s = raw.data() + i * 3;
            // リトルエンディアン 3バイト → int32_t（符号拡張して左シフト8）
            int32_t v = s[0] | (s[1] << 8) | (s[2] << 16);
            if (v & 0x800000) v |= 0xFF000000;  // 符号拡張
            out[i] = v << 8;                    // 上位 24bit に配置
        }
        position_ += n;
        return static_cast<int64_t>(n);
    } else {
        // 16bit / 32bit: そのままコピー
        const size_t n = fread(output_buffer, file_frame_size, frames_to_read, fp_);
        position_ += n;
        return static_cast<int64_t>(n);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool WavDecoder::Seek(uint64_t target_sample) {
    if (!fp_) return false;
    target_sample = std::min(target_sample, info_.total_samples);
    const uint32_t file_frame_size = ((info_.bit_depth + 7) / 8) * info_.channels;
    const int64_t byte_offset = data_offset_ + static_cast<int64_t>(target_sample) * file_frame_size;
    if (fseek(fp_, static_cast<long>(byte_offset), SEEK_SET) != 0) {
        last_error_ = "seek failed"; return false;
    }
    position_ = target_sample;
    return true;
}
