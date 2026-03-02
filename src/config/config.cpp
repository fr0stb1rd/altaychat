#include "config.hpp"
#include <fstream>
#include <cstdlib>
#include <iostream>

AppConfig AppConfig::load() {
    AppConfig config;
    config.signaling_url = "wss://your-worker.your-subdomain.workers.dev"; // Default

    // 1. Configuration File
    std::ifstream f("altaychat.conf");
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("SIGNALING_URL=", 0) == 0) {
                config.signaling_url = line.substr(14);
            } else if (line.rfind("ALTAYCHAT_TURN_USERNAME=", 0) == 0) {
                config.turn_username = line.substr(24);
            } else if (line.rfind("ALTAYCHAT_TURN_PASSWORD=", 0) == 0) {
                config.turn_password = line.substr(24);
            }
        }
    }

    // 2. Environment Variables (Override)
    const char* env_sig_url = std::getenv("ALTAYCHAT_SIGNALING_URL");
    if (env_sig_url && env_sig_url[0] != '\0') {
        config.signaling_url = env_sig_url;
    }

    const char* env_turn_user = std::getenv("ALTAYCHAT_TURN_USERNAME");
    if (env_turn_user && env_turn_user[0] != '\0') {
        config.turn_username = env_turn_user;
    }

    const char* env_turn_pass = std::getenv("ALTAYCHAT_TURN_PASSWORD");
    if (env_turn_pass && env_turn_pass[0] != '\0') {
        config.turn_password = env_turn_pass;
    }

    // 3. Fallback Warning
    if (config.signaling_url == "wss://your-worker.your-subdomain.workers.dev") {
        std::cout << "[Warning] Using placeholder signaling URL. Please configure your own signaling server in altaychat.conf or via ALTAYCHAT_SIGNALING_URL.\n";
    }

    return config;
}
