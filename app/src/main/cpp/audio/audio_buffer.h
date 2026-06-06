#pragma once
#include <atomic>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// LockFreeRingBuffer
//
//  Single-Producer / Single-Consumer のロックフリーリングバッファ。
//
//  Producer: Decoder Thread（Write を呼ぶ）
//  Consumer: AAudio Callback Thread（Read を呼ぶ）
//
//  AAudio コールバックは RT スレッドのため mutex は使えない。
//  write_pos_ / read_pos_ を atomic<size_t> で管理し、memory_order を明示する。
// ─────────────────────────────────────────────────────────────────────────────
class LockFreeRingBuffer {
public:
    // capacity_bytes: 2 の累乗を推奨。
    // 96kHz / 32bit / stereo での約 2 秒分 = ~1.5 MB
    explicit LockFreeRingBuffer(size_t capacity_bytes)
        : buf_(capacity_bytes, 0)
        , capacity_(capacity_bytes)
        , write_pos_(0)
        , read_pos_(0)
    {}

    // ─── Producer API（Decoder Thread 専用）──────────────────────────────────

    // 書き込み可能バイト数
    size_t WritableBytes() const {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        const size_t used = (w - r + capacity_) % capacity_;
        return capacity_ - used - 1;  // 1 バイトは満杯判定用に確保
    }

    // data を最大 byte_count バイト書き込む。返値: 実際に書いたバイト数
    size_t Write(const void* data, size_t byte_count) {
        const size_t writable = WritableBytes();
        const size_t to_write = std::min(byte_count, writable);
        if (to_write == 0) return 0;

        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const uint8_t* src = static_cast<const uint8_t*>(data);

        const size_t space_to_end = capacity_ - w;
        if (to_write <= space_to_end) {
            std::memcpy(&buf_[w], src, to_write);
        } else {
            std::memcpy(&buf_[w], src, space_to_end);
            std::memcpy(&buf_[0], src + space_to_end, to_write - space_to_end);
        }

        write_pos_.store((w + to_write) % capacity_, std::memory_order_release);
        return to_write;
    }

    // ─── Consumer API（AAudio Callback / RT スレッド専用）────────────────────

    // 読み出し可能バイト数
    size_t ReadableBytes() const {
        const size_t w = write_pos_.load(std::memory_order_acquire);
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        return (w - r + capacity_) % capacity_;
    }

    // data に最大 byte_count バイト読み出す。
    // アンダーランの場合は不足分をゼロ（無音）で埋め、読み出したバイト数を返す。
    size_t Read(void* data, size_t byte_count) {
        const size_t readable = ReadableBytes();
        const size_t to_read  = std::min(byte_count, readable);

        const size_t r = read_pos_.load(std::memory_order_relaxed);
        uint8_t* dst = static_cast<uint8_t*>(data);

        if (to_read > 0) {
            const size_t space_to_end = capacity_ - r;
            if (to_read <= space_to_end) {
                std::memcpy(dst, &buf_[r], to_read);
            } else {
                std::memcpy(dst, &buf_[r], space_to_end);
                std::memcpy(dst + space_to_end, &buf_[0], to_read - space_to_end);
            }
            read_pos_.store((r + to_read) % capacity_, std::memory_order_release);
        }

        // アンダーラン分を無音で埋める
        if (to_read < byte_count) {
            std::memset(dst + to_read, 0, byte_count - to_read);
        }

        return to_read;
    }

    // ─── 共通 API ────────────────────────────────────────────────────────────

    // シーク / 曲切り替え時に呼ぶ（Decoder Thread から）
    void Flush() {
        write_pos_.store(0, std::memory_order_release);
        read_pos_.store(0, std::memory_order_release);
        std::fill(buf_.begin(), buf_.end(), 0);
    }

    size_t Capacity() const { return capacity_; }

private:
    std::vector<uint8_t>  buf_;
    const size_t          capacity_;
    std::atomic<size_t>   write_pos_;
    std::atomic<size_t>   read_pos_;
};
