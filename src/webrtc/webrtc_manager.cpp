#include "webrtc_manager.hpp"
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <fstream>

using namespace std;
using namespace rtc;

enum class TurnSource { None, File, Env };

namespace rtp {
    constexpr size_t MIN_HEADER_SIZE = 12;
    constexpr uint8_t VERSION_SHIFT = 6;
    constexpr uint8_t VERSION_EXPECTED = 2;
    constexpr uint8_t CSRC_COUNT_MASK = 0x0F;
    constexpr uint8_t EXTENSION_MASK = 0x10;
    constexpr size_t CSRC_SIZE = 4;
    constexpr size_t EXT_HEADER_SIZE = 4;
}

WebRTCManager::WebRTCManager() {}

WebRTCManager::~WebRTCManager() {
    close();
}

void WebRTCManager::initialize(const std::string& turn_username, const std::string& turn_password) {
    reset(); // Idempotent: safe for reconnects

    Configuration config;
    config.disableAutoNegotiation = true;
    // STUN Servers (Standard + DNS Bypass)
    config.iceServers.emplace_back("stun:stun.cloudflare.com:3478");
    config.iceServers.emplace_back("stun:stun.cloudflare.com:53");

    if (!turn_username.empty() && !turn_password.empty()) {
        std::cout << "[WebRTC] Using TURN credentials from configuration.\n";
        
        // Standard TURN/TURNS Ports
        config.iceServers.emplace_back("turn.cloudflare.com", 3478, turn_username, turn_password, IceServer::RelayType::TurnUdp);
        config.iceServers.emplace_back("turn.cloudflare.com", 3478, turn_username, turn_password, IceServer::RelayType::TurnTcp);
        config.iceServers.emplace_back("turn.cloudflare.com", 5349, turn_username, turn_password, IceServer::RelayType::TurnTls);
        
        // Firewall Bypass Ports (Cloudflare extra routes)
        config.iceServers.emplace_back("turn.cloudflare.com", 53, turn_username, turn_password, IceServer::RelayType::TurnUdp);
        config.iceServers.emplace_back("turn.cloudflare.com", 80, turn_username, turn_password, IceServer::RelayType::TurnTcp);
        config.iceServers.emplace_back("turn.cloudflare.com", 443, turn_username, turn_password, IceServer::RelayType::TurnTls);
        
        config.iceTransportPolicy = TransportPolicy::Relay;
    } else {
        std::cout << "[WebRTC] No TURN credentials found - STUN-only mode (direct connections only).\n"
                  << "[WebRTC] Hint: add them to 'altaychat.conf' or set ALTAYCHAT_TURN_USERNAME/ALTAYCHAT_TURN_PASSWORD.\n";
        config.iceTransportPolicy = TransportPolicy::All;
    }
    
    std::shared_ptr<PeerConnection> local_pc;
    {
        std::lock_guard<std::mutex> lk(pc_mutex_);
        pc_ = std::make_shared<PeerConnection>(config);
        local_pc = pc_;
    }
    
    local_pc->onStateChange([](PeerConnection::State state) {
        cout << "[WebRTC] PeerConnection state changed to: " << state << endl;
    });

    local_pc->onGatheringStateChange([](PeerConnection::GatheringState state) {
        cout << "[WebRTC] ICE Gathering state changed to: " << state << endl;
    });

    local_pc->onSignalingStateChange([](PeerConnection::SignalingState state) {
        cout << "[WebRTC] Signaling state changed to: " << state << endl;
    });

    local_pc->onLocalDescription([this](Description description) {
        if (on_local_description) on_local_description(description);
    });

    local_pc->onLocalCandidate([this](Candidate candidate) {
        cout << "[WebRTC] Gathered local ICE candidate: " << candidate.candidate() << endl;
        if (on_local_candidate) on_local_candidate(candidate);
    });
    
    constexpr uint32_t AUDIO_SSRC = 42;
    auto audio = Description::Audio("audio", Description::Direction::SendRecv);
    audio.addOpusCodec(111);
    audio.addSSRC(AUDIO_SSRC, "audio", "audio-stream", "audio");
    
    auto track = local_pc->addTrack(audio);
    
    {
        std::lock_guard<std::mutex> lk(track_mutex_);
        track_ = track;
    }

    if (on_track_created) {
        on_track_created(track);
    }
    
    rtpConfig_ = make_shared<RtpPacketizationConfig>(AUDIO_SSRC, "audio", 111, OpusRtpPacketizer::DefaultClockRate);
    auto packetizer = make_shared<OpusRtpPacketizer>(rtpConfig_);
    auto srReporter = make_shared<RtcpSrReporter>(rtpConfig_);
    packetizer->addToChain(srReporter);
    track->setMediaHandler(packetizer);

    // Setup network RTP parsing block locally
    track->onMessage([this](rtc::variant<rtc::binary, std::string> data) {
        if (!on_audio_received) return;
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            if (bin.size() < rtp::MIN_HEADER_SIZE) return;

            uint8_t firstByte = static_cast<uint8_t>(bin[0]);
            if ((firstByte >> rtp::VERSION_SHIFT) != rtp::VERSION_EXPECTED) return;

            size_t headerSize = rtp::MIN_HEADER_SIZE;

            uint8_t csrcCount = firstByte & rtp::CSRC_COUNT_MASK;
            headerSize += csrcCount * rtp::CSRC_SIZE;
            if (headerSize >= bin.size()) return;

            bool hasExtension = (firstByte & rtp::EXTENSION_MASK) != 0;
            if (hasExtension) {
                if (headerSize + rtp::EXT_HEADER_SIZE > bin.size()) return;
                uint16_t extLen = (static_cast<uint8_t>(bin[headerSize + 2]) << 8)
                                | static_cast<uint8_t>(bin[headerSize + 3]);
                headerSize += rtp::EXT_HEADER_SIZE + extLen * 4;
                if (headerSize >= bin.size()) return;
            }

            if (bin.size() <= headerSize) return;

            size_t payloadLen = bin.size() - headerSize;
            bool hasPadding = (firstByte & 0x20) != 0;
            if (hasPadding) {
                if (payloadLen == 0) return;
                uint8_t padCount = static_cast<uint8_t>(bin.back());
                if (padCount > payloadLen) return;
                payloadLen -= padCount;
            }

            if (payloadLen == 0) return;

            const uint8_t* opusData = reinterpret_cast<const uint8_t*>(bin.data()) + headerSize;
            on_audio_received(opusData, payloadLen);
        }
    });
}

void WebRTCManager::set_role(const std::string& role) {
    role_ = role;
}

void WebRTCManager::create_offer() {
    std::lock_guard<std::mutex> lk(pc_mutex_);
    if (pc_) pc_->setLocalDescription(Description::Type::Offer);
}

void WebRTCManager::handle_offer(const std::string& sdp, const std::string& type) {
    std::lock_guard<std::mutex> lk(pc_mutex_);
    if (pc_) {
        pc_->setRemoteDescription(Description(sdp, type));
        pc_->setLocalDescription(Description::Type::Answer);
    }
}

void WebRTCManager::handle_answer(const std::string& sdp, const std::string& type) {
    std::lock_guard<std::mutex> lk(pc_mutex_);
    if (pc_) pc_->setRemoteDescription(Description(sdp, type));
}

void WebRTCManager::add_remote_candidate(const std::string& candidate, const std::string& mid) {
    std::lock_guard<std::mutex> lk(pc_mutex_);
    if (pc_) pc_->addRemoteCandidate(Candidate(candidate, mid));
}

void WebRTCManager::reset() {
    std::shared_ptr<PeerConnection> old_pc;
    {
        std::lock_guard<std::mutex> lk(pc_mutex_);
        if (pc_) {
            old_pc = pc_;
            pc_.reset();
        }
    }
    
    if (old_pc) {
        old_pc->close();
    }
    
    {
        std::lock_guard<std::mutex> lk(track_mutex_);
        track_.reset();
    }
    rtpConfig_.reset();
}

void WebRTCManager::close() {
    reset();
}

void WebRTCManager::send_audio(const uint8_t* opus_data, size_t size, std::chrono::duration<double> frameDuration) {
    std::shared_ptr<rtc::Track> safe_track;
    {
        std::lock_guard<std::mutex> lk(track_mutex_);
        safe_track = track_;
    }

    if (safe_track && safe_track->isOpen()) {
        safe_track->sendFrame(reinterpret_cast<const std::byte*>(opus_data), size, rtc::FrameInfo(frameDuration));
    }
}
