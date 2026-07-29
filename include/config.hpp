#pragma once
#include <cstdint>
#include <string>

struct WireGuardConfig {
    std::string interface = "wg0";
    std::string configPath = "/etc/wireguard/wg0.conf";
    std::string serverPublicKey;
    std::string endpoint;       // "ip_или_домен:порт"
    std::string subnetBase = "10.0.0";  // используем /24, клиенты получают .2, .3, ...
    std::string dns = "1.1.1.1";
    std::string clientsDir = "/etc/wireguard/clients";
};

struct AppConfig {
    std::string botToken;
    int64_t allowedUserId = 0;
    WireGuardConfig wg;

    // Читает config.json (путь по умолчанию — рядом с бинарником).
    static AppConfig load(const std::string& path);
};
