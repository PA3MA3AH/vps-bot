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

struct XrayConfig {
    bool configured = false;    // true, если секция "xray" вообще есть в config.json
    std::string configPath = "/usr/local/etc/xray/config.json";
    std::string endpoint;       // IP или домен сервера (без порта)
    int port = 8443;
    std::string publicKey;      // Reality public key (не приватный!)
    std::string shortId;
    std::string serverName;     // SNI, например "www.microsoft.com"
    std::string clientsDir = "/etc/wireguard/clients";  // переиспользуем ту же папку под QR
};

struct AppConfig {
    std::string botToken;
    int64_t allowedUserId = 0;
    WireGuardConfig wg;
    XrayConfig xray;

    // Читает config.json (путь по умолчанию — рядом с бинарником).
    static AppConfig load(const std::string& path);
};
