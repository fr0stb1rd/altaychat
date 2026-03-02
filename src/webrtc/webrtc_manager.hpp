#pragma once

#include <memory>
#include <string>
#include <functional>
#include <rtc/rtc.hpp>
#include <chrono>

class WebRTCManager {
public:
    WebRTCManager();
    ~WebRTCManager();

    void initialize(const std::string& turn_username, const std::string& turn_password);
    
    // Remote actions triggered by signaling
    void set_role(const std::string& role);
    void create_offer();
    void handle_offer(const std::string& sdp, const std::string& type);
    void handle_answer(const std::string& sdp, const std::string& type);
    void add_remote_candidate(const std::string& candidate, const std::string& mid);
    void reset();
    void close();

    // Data Pipeline
    void send_audio(const uint8_t* opus_data, size_t size, std::chrono::duration<double> frameDuration);

    // Callbacks to send signals
    std::function<void(rtc::Description)> on_local_description;
    std::function<void(rtc::Candidate)> on_local_candidate;
    std::function<void(std::shared_ptr<rtc::Track>)> on_track_created;
    std::function<void(const uint8_t* opus_payload, size_t size)> on_audio_received;

private:
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::mutex pc_mutex_;
    std::string role_; 

    // Internal Media State
    std::mutex track_mutex_;
    std::shared_ptr<rtc::Track> track_;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig_;
};
