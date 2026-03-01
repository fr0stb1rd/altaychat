#pragma once

#include <string>
#include <functional>
#include <rtc/rtc.hpp>

class SignalingClient {
public:
    SignalingClient();
    ~SignalingClient();

    void connect(const std::string& url);
    void send_sdp(const rtc::Description& description);
    void send_candidate(const rtc::Candidate& candidate);
    void close();

    // Callbacks
    std::function<void(const std::string&)> on_role;
    std::function<void()> on_peer_joined;
    std::function<void(const std::string&, const std::string&)> on_offer;
    std::function<void(const std::string&, const std::string&)> on_answer;
    std::function<void(const std::string&, const std::string&)> on_candidate;
    std::function<void()> on_peer_left;
    std::function<void(const std::string&)> on_error;

private:
    rtc::WebSocket ws_;
};
