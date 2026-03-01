#include "signaling_client.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

SignalingClient::SignalingClient() {
    ws_.onOpen([]() {
        cout << "[WebSocket] Connected to signaling server." << endl;
    });

    ws_.onError([](const string& error) {
        cerr << "[WebSocket] Error: " << error << endl;
    });

    ws_.onClosed([]() {
        cout << "[WebSocket] Connection closed." << endl;
    });

    ws_.onMessage([this](std::variant<rtc::binary, string> data) {
        if (!holds_alternative<string>(data)) return;
        
        try {
            auto j = json::parse(get<string>(data));
            string type = j.value("type", "");
            
            if (type == "role") {
                string role = j.value("role", "");
                cout << "[Signaling] Assigned role: " << role << endl;
                if (on_role) on_role(role);
            } else if (type == "peer_joined") {
                cout << "[Signaling] Peer joined." << endl;
                if (on_peer_joined) on_peer_joined();
            } else if (type == "offer") {
                cout << "[Signaling] Received Offer." << endl;
                if (on_offer) on_offer(j.value("sdp", ""), "offer");
            } else if (type == "answer") {
                cout << "[Signaling] Received Answer SDP." << endl;
                if (on_answer) on_answer(j.value("sdp", ""), "answer");
            } else if (type == "candidate") {
                if (on_candidate) on_candidate(j.value("candidate", ""), j.value("mid", ""));
            } else if (type == "peer_left") {
                cout << "[Signaling] Peer disconnected." << endl;
                if (on_peer_left) on_peer_left();
            } else if (type == "error") {
                string msg = j.value("message", "");
                cout << "[Signaling] Error: " << msg << endl;
                if (on_error) on_error(msg);
            }
        } catch (const json::parse_error& e) {
            cerr << "[Signaling] JSON Parse Error: " << e.what() << endl;
        }
    });
}

SignalingClient::~SignalingClient() {
    close();
}

void SignalingClient::connect(const std::string& url) {
    ws_.open(url);
}

void SignalingClient::send_sdp(const rtc::Description& description) {
    json j;
    j["type"] = description.typeString();
    j["sdp"] = string(description);
    ws_.send(j.dump());
    cout << "[Signaling] Sent " << j["type"].get<string>() << " SDP." << endl;
}

void SignalingClient::send_candidate(const rtc::Candidate& candidate) {
    json j;
    j["type"] = "candidate";
    j["candidate"] = candidate.candidate();
    j["mid"] = candidate.mid();
    ws_.send(j.dump());
}

void SignalingClient::close() {
    if(ws_.isOpen()) {
        ws_.close();
    }
}
