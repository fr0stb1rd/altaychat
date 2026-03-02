#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <fstream>
#include <cstdlib>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#define DUP_FN _dup
#define DUP2_FN _dup2
#define OPEN_FN _open
#define CLOSE_FN _close
#define NULL_DEVICE "NUL"
#define O_WRONLY_FLAG _O_WRONLY
#define STDERR_FD _fileno(stderr)
#else
#include <unistd.h>
#include <fcntl.h>
#define DUP_FN dup
#define DUP2_FN dup2
#define OPEN_FN open
#define CLOSE_FN close
#define NULL_DEVICE "/dev/null"
#define O_WRONLY_FLAG O_WRONLY
#define STDERR_FD fileno(stderr)
#endif

#include <rtc/rtc.hpp>
#include <portaudio.h>
#include <opus/opus.h>

#include "audio/audio_context.hpp"
#include "signaling/signaling_client.hpp"
#include "webrtc/webrtc_manager.hpp"
#include "config/config.hpp"

#ifndef ALTAYCHAT_VERSION
#define ALTAYCHAT_VERSION "unknown"
#endif

#if defined(_WIN32)
    #define OS_NAME "Windows"
#elif defined(__APPLE__)
    #define OS_NAME "macOS"
#elif defined(__linux__)
    #define OS_NAME "Linux"
#else
    #define OS_NAME "Unknown OS"
#endif

// Compiler Detection
#if defined(__clang__)
    #define COMPILER_NAME "Clang " __clang_version__
#elif defined(__GNUC__)
    #define COMPILER_NAME "GCC " __VERSION__
#elif defined(_MSC_VER)
    #define COMPILER_NAME "MSVC "
#else
    #define COMPILER_NAME "Unknown Compiler"
#endif

// Architecture Detection
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_NAME "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH_NAME "ARM64"
#elif defined(__i386) || defined(_M_IX86)
    #define ARCH_NAME "x86"
#else
    #define ARCH_NAME "Unknown Arch"
#endif

// Build Type Detection
#ifdef NDEBUG
    #define BUILD_TYPE "Release"
#else
    #define BUILD_TYPE "Debug"
#endif

// Global flag and CV for signal handling
std::atomic<bool> g_is_running{true};
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_is_running.store(false, std::memory_order_release);
        g_shutdown_cv.notify_all();
    }
}

// RAII structural wrappers for proper resource cleanup
class PortAudioGuard {
public:
    PortAudioGuard() { err_ = Pa_Initialize(); }
    ~PortAudioGuard() { if (err_ == paNoError) Pa_Terminate(); }
    PaError error() const { return err_; }
private:
    PaError err_;
};

class OpusEncoderGuard {
public:
    OpusEncoderGuard(int sample_rate, int channels, int application) {
        encoder_ = opus_encoder_create(sample_rate, channels, application, &err_);
        if (err_ != OPUS_OK) encoder_ = nullptr;
    }
    ~OpusEncoderGuard() { if (encoder_) opus_encoder_destroy(encoder_); }
    OpusEncoder* get()  const { return encoder_; }
    int          error() const { return err_; }
private:
    OpusEncoder* encoder_{nullptr};
    int          err_{OPUS_OK};
};

class OpusDecoderGuard {
public:
    OpusDecoderGuard(int sample_rate, int channels) {
        decoder_ = opus_decoder_create(sample_rate, channels, &err_);
        if (err_ != OPUS_OK) decoder_ = nullptr;
    }
    ~OpusDecoderGuard() { if (decoder_) opus_decoder_destroy(decoder_); }
    OpusDecoder* get()  const { return decoder_; }
    int          error() const { return err_; }
private:
    OpusDecoder* decoder_{nullptr};
    int          err_{OPUS_OK};
};

// RAII guard that redirects stderr to /dev/null (or NUL on Windows) for its
// lifetime. Guarantees restoration even on early return or exception.
class StderrSuppressor {
public:
    StderrSuppressor() : saved_fd_(DUP_FN(STDERR_FD)) {
        int null_fd = OPEN_FN(NULL_DEVICE, O_WRONLY_FLAG);
        if (null_fd != -1) {
            DUP2_FN(null_fd, STDERR_FD);
            CLOSE_FN(null_fd);
            active_ = true;
        }
    }
    ~StderrSuppressor() {
        if (active_ && saved_fd_ != -1) {
            DUP2_FN(saved_fd_, STDERR_FD);
            CLOSE_FN(saved_fd_);
        }
    }
    // Non-copyable
    StderrSuppressor(const StderrSuppressor&)            = delete;
    StderrSuppressor& operator=(const StderrSuppressor&) = delete;
private:
    int  saved_fd_;
    bool active_{false};
};

class PaStreamGuard {
public:
    PaStreamGuard() : stream_(nullptr) {}
    ~PaStreamGuard() {
        if (stream_) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
        }
    }
    PaStream** address() { return &stream_; }
    PaStream* get() const { return stream_; }
private:
    PaStream* stream_;
};

// Prints the startup banner with system info.
static void print_banner_info(bool debug_mode, const std::string& audio_backend) {
#if defined(_WIN32)
    const char* uname = getenv("USERNAME");
#else
    const char* uname = getenv("USER");
#endif
    std::string user_str = uname ? uname : "guest";

    std::cout << "============================\n"
         << "User     : " << user_str << "\n"
         << "Version  : " << ALTAYCHAT_VERSION << " (" << BUILD_TYPE << ")\n"
         << "Target   : " << OS_NAME << " (" << ARCH_NAME << ")\n"
         << "Compiler : " << COMPILER_NAME << "\n"
         << "Audio    : " << audio_backend << " / Opus (48kHz, Mono)\n"
         << "License  : SPDX-License-Identifier: MIT\n"
         << "Architect: https://fr0stb1rd.gitlab.io/\n"
         << (debug_mode ? "[DEBUG MODE ENABLED]\n" : "");
}

static void print_ascii_art() {
    std::cout << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⣶⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⣿⣿⣆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣾⣿⡿⠙⢿⣿⣧⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣿⣿⠏⠀⠀⠀⠻⣿⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⡿⠃⠀⠀⠀⠀⠀⠙⣿⣿⣆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣾⣿⡟⠁⠀⠀⠀⠀⠀⠀⠀⠈⢻⣿⣧⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣿⣿⠏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠹⣿⣿⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⢿⣿⣦⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⢀⣾⣿⠟⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠻⣿⣷⡀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⣠⣿⣿⠋⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⣿⣿⣄⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⢀⣼⣿⡿⠁⠀⠀⠀⠀⠀⠀⠀⠀⢰⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢿⣿⣦⡀⠀⠀⠀\n"
              << "⠀⠀⢠⣾⣿⠟⠀⠀⠀⠀⠀⠀⠀⣸⡀⠀⣸⣿⡆⠀⠀⢸⡀⠀⠀⣿⠀⠀⠀⠀⠀⠹⣿⣷⡄⠀⠀\n"
              << "⠀⣰⣿⣿⣋⣀⣀⡀⠀⣸⡆⠀⢀⣿⣇⠀⣿⣿⣇⠀⠀⣿⣇⠀⢸⣿⣇⠀⠀⣤⠀⢀⣘⣿⣿⣆⠀\n"
              << "⠘⠛⠛⠛⠛⠛⠛⠁⣰⣿⣷⠀⢸⣿⣿⢰⣿⠀⣿⠀⢰⡿⣿⣀⣿⠉⣿⡄⣰⣿⡆⠘⠛⠛⠛⠛⠃\n"
              << "⠲⠶⠶⠶⠶⠶⠶⠶⠿⠁⢿⣇⣿⠇⢿⣿⡏⠀⢿⡆⣼⠃⢹⣿⡏⠀⠸⠿⠿⠉⢿⡀⣰⡶⠶⠶⠂\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⡿⠀⠸⣿⡇⠀⢸⣷⡿⠀⠈⣿⠀⠀⠀⠀⠀⠀⠘⣿⡟⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢻⡇⠀⠀⣿⠀⠀⠈⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠹⠁⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⠀⠀⠀⠘⠀⠀⠀⢿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
              << "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n";
}

class TaskQueue {
public:
    TaskQueue() : running_(true) {
        worker_ = std::thread([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(mutex_);
                    cv_.wait(lk, [this] { return !tasks_.empty() || !running_; });
                    if (!running_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                if (task) task();
            }
        });
    }

    ~TaskQueue() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            running_ = false;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void push(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
    std::thread worker_;
};

int main(int argc, char **argv) {
    // Register signal handlers for robust shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    bool debug_mode = false;
    bool smoke_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") {
            debug_mode = true;
        } else if (std::string(argv[i]) == "--version") {
            std::cout << "AltayChat " << ALTAYCHAT_VERSION << " (" << OS_NAME << "/" << ARCH_NAME << ", " << BUILD_TYPE << ")\n\n";
            print_ascii_art();
            return 0;
        } else if (std::string(argv[i]) == "--smoke-test") {
            smoke_test = true;
        }
    }

    if (smoke_test) {
        std::cout << "AltayChat " << ALTAYCHAT_VERSION << " (" << OS_NAME << "/" << ARCH_NAME << ", " << BUILD_TYPE << ")\n";
        std::cout << "Smoke test    : PASSED - all libraries loaded successfully.\n";
        return 0;
    }

    // 1. Initialize PortAudio first to get API info
    std::unique_ptr<PortAudioGuard> pa_guard;
    {
        std::unique_ptr<StderrSuppressor> suppress;
        if (!debug_mode) suppress = std::make_unique<StderrSuppressor>();
        pa_guard = std::make_unique<PortAudioGuard>();
        // ~suppress fires here, restoring stderr before we check for errors
    }

    if (pa_guard->error() != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(pa_guard->error()) << "\n";
        return 1;
    }

    // 2. Identify the backend
    const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(Pa_GetDefaultHostApi());
    std::string audio_backend = apiInfo ? apiInfo->name : "Unknown Backend";

    // 3. Now print the banner with the identified backend
    std::cout << "AltayChat - WebRTC P2P Audio\n";
    print_banner_info(debug_mode, audio_backend);

    AppConfig app_config = AppConfig::load();

    // 4. Proceed with Room ID and Stream opening
    std::string room_id;
    std::cout << "[Info] Enter room name (e.g. test1, room123): ";
    std::cin >> room_id;

    std::string base_ws_url = app_config.signaling_url;
    if (!base_ws_url.empty() && base_ws_url.back() != '/') {
        base_ws_url += "/";
    }
    std::string ws_url = base_ws_url + room_id;

    rtc::InitLogger(debug_mode ? rtc::LogLevel::Debug : rtc::LogLevel::Error);
    
    OpusEncoderGuard opus_enc(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP);
    if (!opus_enc.get()) {
        std::cerr << "Failed to create Opus encoder: " << opus_strerror(opus_enc.error()) << "\n";
        return 1;
    }

    OpusDecoderGuard opus_dec(SAMPLE_RATE, CHANNELS);
    if (!opus_dec.get()) {
        std::cerr << "Failed to create Opus decoder: " << opus_strerror(opus_dec.error()) << "\n";
        return 1;
    }

    AudioContext ctx;
    ctx.encoder = opus_enc.get();
    ctx.decoder = opus_dec.get();

    PaStreamGuard stream_guard;
    PaError err = Pa_OpenDefaultStream(stream_guard.address(),
                               CHANNELS,          // input channels
                               CHANNELS,          // output channels
                               paInt16,           // sample format
                               SAMPLE_RATE,
                               FRAMES_PER_BUFFER, // frames per buffer
                               paCallback,
                               &ctx);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    TaskQueue signaling_queue;
    WebRTCManager webrtc;

    SignalingClient signaling;

    webrtc.on_local_description = [&](rtc::Description desc) {
        signaling.send_sdp(desc);
    };

    webrtc.on_local_candidate = [&](rtc::Candidate cand) {
        signaling.send_candidate(cand);
    };

    signaling.on_role = [&](const std::string& role) {
        webrtc.set_role(role);
    };

    signaling.on_peer_joined = [&]() {
        signaling_queue.push([&]() {
            webrtc.initialize(app_config.turn_username, app_config.turn_password);
            webrtc.create_offer();
        });
    };

    signaling.on_offer = [&](const std::string& sdp, const std::string& type) {
        signaling_queue.push([&, sdp, type]() {
            webrtc.initialize(app_config.turn_username, app_config.turn_password);
            webrtc.handle_offer(sdp, type);
        });
    };

    signaling.on_answer = [&](const std::string& sdp, const std::string& type) {
        webrtc.handle_answer(sdp, type);
    };

    signaling.on_candidate = [&](const std::string& cand, const std::string& mid) {
        webrtc.add_remote_candidate(cand, mid);
    };

    webrtc.on_track_created = [stream = stream_guard.get()](std::shared_ptr<rtc::Track> track) {
        track->onOpen([stream]() {
            std::cout << "[Info] Audio track open. Starting audio stream...\n";
            Pa_StartStream(stream);
        });

        track->onClosed([stream]() {
            std::cout << "[Info] Audio track closed.\n";
            Pa_StopStream(stream);
        });
    };

    webrtc.on_audio_received = [&ctx](const uint8_t* opus_payload, size_t size) {
        int16_t out[FRAMES_PER_BUFFER * CHANNELS];
        int decoded = opus_decode(ctx.decoder, opus_payload, static_cast<opus_int32>(size),
                                  out, FRAMES_PER_BUFFER, 0);
        if (decoded > 0) {
            ctx.rx_queue.push(out, static_cast<size_t>(decoded) * CHANNELS);
        }
    };

    // Wire on_peer_left: stop the stream so we are not encoding noise into the void.
    // Pa_StopStream is safe to call from a non-callback thread (PortAudio docs).
    // When a new peer connects, the track->onOpen handler above will restart it.
    signaling.on_peer_left = [&webrtc, &signaling_queue, stream = stream_guard.get()]() {
        std::cout << "[Signaling] Peer disconnected. Stream paused - waiting for reconnect.\n";
        Pa_StopStream(stream);
        // pc_->close() and reset() cannot be called from the network thread directly.
        // We defer the cleanup to the TaskQueue.
        signaling_queue.push([&webrtc]() {
            webrtc.reset();
        });
    };

    std::cout << "[Info] Connecting to Cloudflare signaling server...\n";
    signaling.connect(ws_url);
    
    std::cout << "[Info] Audio Stream Active. Press CTRL+C to close the chat and exit.\n";
    
    // Audio Worker Thread: Event driven
    std::thread audio_worker([&ctx, &webrtc]() {
        int16_t pcm_buf[FRAMES_PER_BUFFER * CHANNELS];
        unsigned char opus_buf[MAX_PACKET_SIZE];

        while (g_is_running.load(std::memory_order_relaxed)) {
            size_t samples_needed = FRAMES_PER_BUFFER * CHANNELS;

            // Wait via condition variable: timeout allows routine check of shutdown flag.
            // Note: worker_cv.notify_one() in the audio callback does NOT hold worker_mutex -
            // this is intentional and correct. notify_one() is lock-free on the notifier side;
            // only the waiting thread needs the lock (which it already holds here).
            std::unique_lock<std::mutex> lk(ctx.worker_mutex);
            ctx.worker_cv.wait_for(lk, std::chrono::milliseconds(10), [&] {
                return ctx.tx_queue.size() >= samples_needed || !g_is_running.load(std::memory_order_relaxed);
            });
            lk.unlock();

            // Perform encode loop until queue descends below frame capacity
            while (ctx.tx_queue.size() >= samples_needed && g_is_running.load(std::memory_order_relaxed)) {
                size_t read = ctx.tx_queue.pop(pcm_buf, samples_needed);

                if (read == samples_needed) {
                    int nbytes = opus_encode(ctx.encoder, pcm_buf, FRAMES_PER_BUFFER, opus_buf, MAX_PACKET_SIZE);
                    if (nbytes > 0) {
                        std::chrono::duration<double> frameDuration(
                            static_cast<double>(FRAMES_PER_BUFFER) / SAMPLE_RATE);
                        webrtc.send_audio(opus_buf, static_cast<size_t>(nbytes), frameDuration);
                    }
                }
            }
        }
    });

    // Wait efficiently on CV until shutdown is signaled via SIGTERM or SIGINT
    std::unique_lock<std::mutex> lock(g_shutdown_mutex);
    g_shutdown_cv.wait(lock, [] { return !g_is_running.load(std::memory_order_relaxed); });

    std::cout << "[Info] Shutting down gracefully...\n";
    
    // Release Worker
    ctx.worker_cv.notify_one();
    if (audio_worker.joinable()) {
        audio_worker.join();
    }

    // RAII scopes destruct downwards here, cleanly closing stream -> decoder -> encoder -> PA API.
    return 0;
}
