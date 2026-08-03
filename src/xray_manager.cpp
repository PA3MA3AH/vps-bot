#include "xray_manager.hpp"
#include "shell_exec.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/stat.h>

namespace xray {

namespace {

using json = nlohmann::json;

// Читает и парсит config.json Xray. Бросает std::exception при ошибке,
// вызывающий код обязан отловить.
json readConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("не удалось открыть " + path);
    }
    json j;
    f >> j;
    return j;
}

bool writeConfig(const std::string& path, const json& j) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    f << j.dump(2) << "\n";
    return true;
}

// Ссылка на массив клиентов первого VLESS-инбаунда. Кидает исключение,
// если структура конфига не такая, как ожидается.
json& clientsArray(json& cfg) {
    if (!cfg.contains("inbounds") || cfg["inbounds"].empty()) {
        throw std::runtime_error("в config.json Xray нет inbounds");
    }
    auto& inbound = cfg["inbounds"][0];
    if (!inbound.contains("settings")) inbound["settings"] = json::object();
    if (!inbound["settings"].contains("clients")) {
        inbound["settings"]["clients"] = json::array();
    }
    return inbound["settings"]["clients"];
}

std::string generateUuid() {
    std::ifstream f("/proc/sys/kernel/random/uuid");
    std::string uuid;
    std::getline(f, uuid);
    return uuid;
}

std::string urlEncodeMinimal(const std::string& s) {
    // Имена у нас и так ограничены [A-Za-z0-9_-] (см. shell::isSafeToken),
    // поэтому полноценный percent-encoding не требуется — просто
    // подстраховываемся на случай пробелов.
    std::string out;
    for (char c : s) {
        if (c == ' ') out += "%20";
        else out += c;
    }
    return out;
}

}  // namespace

std::string listClients(const XrayConfig& cfg) {
    if (!cfg.configured) {
        return "Xray не настроен в config.json бота. Установите его через "
               "scripts/install_xray.sh и добавьте секцию \"xray\" в config.json.";
    }

    json j;
    try {
        j = readConfig(cfg.configPath);
    } catch (const std::exception& e) {
        return std::string("Не удалось прочитать конфиг Xray: ") + e.what();
    }

    try {
        auto& clients = clientsArray(j);
        if (clients.empty()) {
            return "VLESS/Reality: клиентов пока нет.";
        }
        std::ostringstream out;
        out << "VLESS/Reality: " << clients.size() << " клиентов\n\n";
        for (auto& c : clients) {
            std::string email = c.value("email", "(без имени)");
            out << "\xE2\x9A\xAA " << email << "\n";
        }
        out << "\n(онлайн-статус для VLESS не отслеживается — в отличие от "
               "WireGuard тут нет простого способа проверить, кто активен "
               "прямо сейчас, не заглядывая в логи соединений)";
        return out.str();
    } catch (const std::exception& e) {
        return std::string("Ошибка структуры конфига Xray: ") + e.what();
    }
}

AddResult addClient(const XrayConfig& cfg, const std::string& name) {
    AddResult res;
    res.clientName = name;

    if (!cfg.configured) {
        res.error = "Xray не настроен в config.json бота. Установите его через "
                    "scripts/install_xray.sh и добавьте секцию \"xray\" в config.json.";
        return res;
    }
    if (!shell::isSafeToken(name)) {
        res.error = "Имя может содержать только буквы, цифры, - и _ (до 64 символов).";
        return res;
    }
    if (cfg.publicKey.empty() || cfg.serverName.empty() || cfg.endpoint.empty()) {
        res.error = "В config.json не заполнены xray.public_key / xray.server_name / "
                    "xray.endpoint — их выдаёт scripts/install_xray.sh при установке.";
        return res;
    }

    json j;
    try {
        j = readConfig(cfg.configPath);
    } catch (const std::exception& e) {
        res.error = std::string("Не удалось прочитать конфиг Xray: ") + e.what();
        return res;
    }

    try {
        auto& clients = clientsArray(j);
        for (auto& c : clients) {
            if (c.value("email", "") == name) {
                res.error = "Клиент с именем '" + name + "' уже существует.";
                return res;
            }
        }

        std::string uuid = generateUuid();
        if (uuid.empty()) {
            res.error = "Не удалось сгенерировать UUID.";
            return res;
        }

        json newClient;
        newClient["id"] = uuid;
        newClient["email"] = name;
        newClient["flow"] = "xtls-rprx-vision";
        clients.push_back(newClient);

        if (!writeConfig(cfg.configPath, j)) {
            res.error = "Не удалось записать конфиг Xray на диск.";
            return res;
        }

        // Xray не поддерживает горячее применение изменений клиентов без API —
        // проще всего перезапустить сервис (короткий разрыв активных соединений).
        auto [restartCode, restartOut] = shell::run("systemctl restart xray");
        if (restartCode != 0) {
            res.error = "Конфиг обновлён, но перезапуск Xray завершился ошибкой: " + restartOut;
            return res;
        }

        std::ostringstream link;
        link << "vless://" << uuid << "@" << cfg.endpoint << ":" << cfg.port
             << "?encryption=none&flow=xtls-rprx-vision&security=reality"
             << "&sni=" << cfg.serverName << "&fp=chrome&pbk=" << cfg.publicKey
             << "&sid=" << cfg.shortId << "&type=tcp&headerType=none"
             << "#" << urlEncodeMinimal(name);

        shell::run("mkdir -p " + cfg.clientsDir);
        std::string qrPath = cfg.clientsDir + "/vless_" + name + ".png";
        auto [qrCode, qrOut] = shell::run(
            "qrencode -t png -o " + qrPath + " \"" + link.str() + "\"");

        res.ok = true;
        res.link = link.str();
        res.qrPath = (qrCode == 0) ? qrPath : "";
        return res;
    } catch (const std::exception& e) {
        res.error = std::string("Ошибка структуры конфига Xray: ") + e.what();
        return res;
    }
}

RemoveResult removeClient(const XrayConfig& cfg, const std::string& name) {
    RemoveResult res;

    if (!cfg.configured) {
        res.error = "Xray не настроен в config.json бота.";
        return res;
    }
    if (!shell::isSafeToken(name)) {
        res.error = "Недопустимое имя клиента.";
        return res;
    }

    json j;
    try {
        j = readConfig(cfg.configPath);
    } catch (const std::exception& e) {
        res.error = std::string("Не удалось прочитать конфиг Xray: ") + e.what();
        return res;
    }

    try {
        auto& clients = clientsArray(j);
        auto it = clients.begin();
        bool found = false;
        for (; it != clients.end(); ++it) {
            if ((*it).value("email", "") == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            res.error = "Клиент '" + name + "' не найден.";
            return res;
        }
        clients.erase(it);

        if (!writeConfig(cfg.configPath, j)) {
            res.error = "Не удалось записать конфиг Xray на диск.";
            return res;
        }

        auto [restartCode, restartOut] = shell::run("systemctl restart xray");
        if (restartCode != 0) {
            res.error = "Клиент удалён из конфига, но перезапуск Xray завершился ошибкой: " +
                         restartOut;
            return res;
        }

        shell::run("rm -f " + cfg.clientsDir + "/vless_" + name + ".png");

        res.ok = true;
        return res;
    } catch (const std::exception& e) {
        res.error = std::string("Ошибка структуры конфига Xray: ") + e.what();
        return res;
    }
}

}  // namespace xray
