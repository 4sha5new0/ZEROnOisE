#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <aaudio/AAudio.h>
#include "../audio_info.h"

// ─────────────────────────────────────────────────────────────────────────────
// LevelMeter
//
//  AAudio Callback（RTスレッド）でサンプルを計算し、
//  std::atomic 経由で UIスレッドへ安全に渡す。
//  mutex は使わない。
// ─────────────────────────────────────────────────────────────────────────────
class LevelMeter {
public:
    struct Snapshot {
        float rms_l_dbfs     = -96.0f;
        float rms_r_dbfs     = -96.0f;
        float peak_l_dbfs    = -96.0f;
        float peak_r_dbfs    = -96.0f;
        float hold_l_dbfs    = -96.0f;
        float hold_r_dbfs    = -96.0f;
        bool  clip_l         = false;
        bool  clip_r         = false;
    };

    // ─── RT スレッドから呼ぶ ─────────────────────────────────────────────────
    void Compute(const void* audio_data, int32_t num_frames,
                 aaudio_format_t fmt, uint32_t sample_rate) {

        double sum_sq_l = 0.0, sum_sq_r = 0.0;
        float  peak_l = 0.0f, peak_r = 0.0f;

        if (fmt == AAUDIO_FORMAT_PCM_I16) {
            const int16_t* p = static_cast<const int16_t*>(audio_data);
            for (int i = 0; i < num_frames; ++i) {
                const float l = p[i * 2    ] / 32768.0f;
                const float r = p[i * 2 + 1] / 32768.0f;
                sum_sq_l += l * l;
                sum_sq_r += r * r;
                if (std::abs(l) > peak_l) peak_l = std::abs(l);
                if (std::abs(r) > peak_r) peak_r = std::abs(r);
            }
        } else if (fmt == AAUDIO_FORMAT_PCM_I32) {
            const int32_t* p = static_cast<const int32_t*>(audio_data);
            for (int i = 0; i < num_frames; ++i) {
                const float l = p[i * 2    ] / 2147483648.0f;
                const float r = p[i * 2 + 1] / 2147483648.0f;
                sum_sq_l += l * l;
                sum_sq_r += r * r;
                if (std::abs(l) > peak_l) peak_l = std::abs(l);
                if (std::abs(r) > peak_r) peak_r = std::abs(r);
            }
        } else if (fmt == AAUDIO_FORMAT_PCM_FLOAT) {
            const float* p = static_cast<const float*>(audio_data);
            for (int i = 0; i < num_frames; ++i) {
                const float l = p[i * 2    ];
                const float r = p[i * 2 + 1];
                sum_sq_l += l * l;
                sum_sq_r += r * r;
                if (std::abs(l) > peak_l) peak_l = std::abs(l);
                if (std::abs(r) > peak_r) peak_r = std::abs(r);
            }
        }

        const float rms_l = (num_frames > 0)
            ? static_cast<float>(std::sqrt(sum_sq_l / num_frames)) : 0.0f;
        const float rms_r = (num_frames > 0)
            ? static_cast<float>(std::sqrt(sum_sq_r / num_frames)) : 0.0f;

        const float rms_l_db = (rms_l > 1e-9f)
            ? 20.0f * std::log10(rms_l) : -96.0f;
        const float rms_r_db = (rms_r > 1e-9f)
            ? 20.0f * std::log10(rms_r) : -96.0f;
        const float peak_l_db = (peak_l > 1e-9f)
            ? 20.0f * std::log10(peak_l) : -96.0f;
        const float peak_r_db = (peak_r > 1e-9f)
            ? 20.0f * std::log10(peak_r) : -96.0f;

        rms_l_.store(rms_l_db,  std::memory_order_relaxed);
        rms_r_.store(rms_r_db,  std::memory_order_relaxed);
        peak_l_.store(peak_l_db, std::memory_order_relaxed);
        peak_r_.store(peak_r_db, std::memory_order_relaxed);
        clip_l_.store(peak_l >= 1.0f, std::memory_order_relaxed);
        clip_r_.store(peak_r >= 1.0f, std::memory_order_relaxed);

        // ピークホールド（RTスレッド内ローカル変数のみ使用）
        const uint32_t hold_frames = sample_rate * 3u;  // 3秒
        if (peak_l_db > hold_l_raw_) {
            hold_l_raw_ = peak_l_db;
            hold_counter_l_ = 0;
        } else if (++hold_counter_l_ > hold_frames) {
            hold_l_raw_ = peak_l_db;
            hold_counter_l_ = 0;
        }
        if (peak_r_db > hold_r_raw_) {
            hold_r_raw_ = peak_r_db;
            hold_counter_r_ = 0;
        } else if (++hold_counter_r_ > hold_frames) {
            hold_r_raw_ = peak_r_db;
            hold_counter_r_ = 0;
        }
        hold_l_.store(hold_l_raw_, std::memory_order_relaxed);
        hold_r_.store(hold_r_raw_, std::memory_order_relaxed);
    }

    void Reset() {
        rms_l_.store(-96.0f,  std::memory_order_relaxed);
        rms_r_.store(-96.0f,  std::memory_order_relaxed);
        peak_l_.store(-96.0f, std::memory_order_relaxed);
        peak_r_.store(-96.0f, std::memory_order_relaxed);
        hold_l_.store(-96.0f, std::memory_order_relaxed);
        hold_r_.store(-96.0f, std::memory_order_relaxed);
        clip_l_.store(false,  std::memory_order_relaxed);
        clip_r_.store(false,  std::memory_order_relaxed);
        hold_l_raw_ = hold_r_raw_ = -96.0f;
        hold_counter_l_ = hold_counter_r_ = 0;
    }

    // ─── UI スレッドから呼ぶ ─────────────────────────────────────────────────
    Snapshot Read() const {
        Snapshot s;
        s.rms_l_dbfs  = rms_l_.load(std::memory_order_relaxed);
        s.rms_r_dbfs  = rms_r_.load(std::memory_order_relaxed);
        s.peak_l_dbfs = peak_l_.load(std::memory_order_relaxed);
        s.peak_r_dbfs = peak_r_.load(std::memory_order_relaxed);
        s.hold_l_dbfs = hold_l_.load(std::memory_order_relaxed);
        s.hold_r_dbfs = hold_r_.load(std::memory_order_relaxed);
        s.clip_l      = clip_l_.load(std::memory_order_relaxed);
        s.clip_r      = clip_r_.load(std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<float> rms_l_{-96.0f};
    std::atomic<float> rms_r_{-96.0f};
    std::atomic<float> peak_l_{-96.0f};
    std::atomic<float> peak_r_{-96.0f};
    std::atomic<float> hold_l_{-96.0f};
    std::atomic<float> hold_r_{-96.0f};
    std::atomic<bool>  clip_l_{false};
    std::atomic<bool>  clip_r_{false};

    // RTスレッド内のみでアクセスするホールド用変数（atomicは不要）
    float    hold_l_raw_    = -96.0f;
    float    hold_r_raw_    = -96.0f;
    uint32_t hold_counter_l_ = 0;
    uint32_t hold_counter_r_ = 0;
};
