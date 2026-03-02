#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

struct AppConfig {
    std::string signaling_url;
    std::string turn_username;
    std::string turn_password;

    static AppConfig load();
};

#endif // CONFIG_HPP
