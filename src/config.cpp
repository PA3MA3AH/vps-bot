#include "config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

AppConfig AppConfig::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Не удалось открыть конфиг: " + path);
    }

    nlohmann::json j;
    f >> j;

    AppConfig cfg;
    cfg.botToken = j.at("bot_token").get<std::string>();
    cfg.allowedUserId = j.at("allowed_user_id").get<int64_t>();

    if (cfg.botToken.empty() || cfg.botToken == "PUT_YOUR_TOKEN_HERE") {
        throw std::runtime_error("bot_token не задан в config.json");
    }

    const auto& wg = j.at("wireguard");
    cfg.wg.interface = wg.value("interface", "wg0");
    cfg.wg.configPath = wg.value("config_path", "/etc/wireguard/wg0.conf");
    cfg.wg.serverPublicKey = wg.value("server_public_key", "");
    cfg.wg.endpoint = wg.value("endpoint", "");
    cfg.wg.subnetBase = wg.value("subnet_base", "10.0.0");
    cfg.wg.dns = wg.value("dns", "1.1.1.1");
    cfg.wg.clientsDir = wg.value("clients_dir", "/etc/wireguard/clients");

    return cfg;
}
