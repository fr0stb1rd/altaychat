#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <opus/opus.h>
#include <portaudio.h>

#include <atomic>
#include <cassert>

constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 1;
constexpr int FRAMES_PER_BUFFER = 960; // 20ms at 48kHz
constexpr int MAX_PACKET_SIZE = 4000;

// Single-Producer / Single-Consumer Lock-Free Ring Buffer
// Capacity must be a power of 2.
template<typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
    LockFreeRingBuffer() : head_(0), tail_(0), buffer_{} {}

    // Called by Producer
    size_t push(const T* data, size_t count) {
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t current_head = head_.load(std::memory_order_acquire);
        
        size_t available = Capacity - (current_tail - current_head);
        size_t to_write = std::min(count, available);
        if (to_write == 0) return 0;

        for (size_t i = 0; i < to_write; ++i) {
            buffer_[(current_tail + i) & (Capacity - 1)] = data[i];
        }
        
        tail_.store(current_tail + to_write, std::memory_order_release);
        return to_write;
    }

    // Called by Consumer
    size_t pop(T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_acquire);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        
        size_t available = current_tail - current_head;
        size_t to_read = std::min(count, available);
        if (to_read == 0) return 0;

        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[(current_head + i) & (Capacity - 1)];
        }
        
        head_.store(current_head + to_read, std::memory_order_release);
        return to_read;
    }

    size_t size() const {
        size_t current_head = head_.load(std::memory_order_acquire);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        return current_tail - current_head;
    }

private:
    // Each atomic on its own cache line to prevent false sharing.
    // buffer_ follows on its own aligned slot - keeping it after the hot
    // atomics ensures it does not share a line with head_ or tail_.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) T buffer_[Capacity];
};

struct AudioContext {
    OpusEncoder *encoder = nullptr;
    OpusDecoder *decoder = nullptr;

    // ~1 second of audio buffer (power of 2)
    static constexpr size_t RING_BUFFER_SIZE = 65536;

    // rx_queue: Network Thread (Producer) -> Audio Thread (Consumer)
    LockFreeRingBuffer<int16_t, RING_BUFFER_SIZE> rx_queue;

    // tx_queue: Audio Thread (Producer) -> Worker Thread (Consumer)
    LockFreeRingBuffer<int16_t, RING_BUFFER_SIZE> tx_queue;

    // Worker Thread Event Notification
    std::mutex worker_mutex;
    std::condition_variable worker_cv;
};

// PortAudio Callback
int paCallback(const void *inputBuffer, void *outputBuffer,
               unsigned long framesPerBuffer,
               const PaStreamCallbackTimeInfo* timeInfo,
               PaStreamCallbackFlags statusFlags,
               void *userData);
