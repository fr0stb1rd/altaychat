#include "audio_context.hpp"
#include <algorithm>

int paCallback(const void *inputBuffer, void *outputBuffer,
               unsigned long framesPerBuffer,
               const PaStreamCallbackTimeInfo* timeInfo,
               PaStreamCallbackFlags statusFlags,
               void *userData) {
    auto *ctx = static_cast<AudioContext*>(userData);
    auto *in = static_cast<const int16_t*>(inputBuffer);
    auto *out = static_cast<int16_t*>(outputBuffer);

    size_t samples_needed = framesPerBuffer * CHANNELS;

    // Capture and Queue (Producer for tx_queue)
    if (in != nullptr) {
        // If the queue is full, we drop samples (audio glitch is inevitable at this point,
        // but it prevents crashing or blocking).
        ctx->tx_queue.push(in, samples_needed);
        // Wake up the worker thread. notify_one() does NOT require worker_mutex
        // to be held by the notifier - only the waiter needs the lock.
        // Calling it lock-free here is correct and avoids any mutex acquisition
        // in this real-time audio callback.
        ctx->worker_cv.notify_one();
    }

    // Read and Play (Consumer for rx_queue)
    if (out != nullptr) {
        size_t samples_read = ctx->rx_queue.pop(out, samples_needed);
        
        // If not enough data available (underflow), pad the rest with silence
        if (samples_read < samples_needed) {
            std::fill(out + samples_read, out + samples_needed, 0);
        }
    }

    return paContinue;
}
