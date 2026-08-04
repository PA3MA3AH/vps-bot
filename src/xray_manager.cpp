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

std::string urlEncode(const std::string& s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string formatEndpoint(const std::string& endpoint) {
    // IPv6-адрес в URI обязан быть заключён в квадратные скобки.
    if (endpoint.find(':') != std::string::npos &&
        (endpoint.front() != '[' || endpoint.back() != ']')) {
        return "[" + endpoint + "]";
    }
    return endpoint;
}

std::string buildVlessLink(const XrayConfig& cfg, const std::string& uuid,
                           const std::string& name) {
    std::ostringstream link;
    link << "vless://" << uuid << "@" << formatEndpoint(cfg.endpoint) << ":" << cfg.port
         << "?encryption=none&flow=xtls-rprx-vision&security=reality"
         << "&sni=" << urlEncode(cfg.serverName) << "&fp=chrome&pbk="
         << urlEncode(cfg.publicKey) << "&sid=" << urlEncode(cfg.shortId)
         << "&spx=%2F&type=tcp"
         << "#" << urlEncode(name);
    return link.str();
}

bool hasLinkSettings(const XrayConfig& cfg) {
    return !cfg.publicKey.empty() && !cfg.shortId.empty() &&
           !cfg.serverName.empty() && !cfg.endpoint.empty() && cfg.port > 0;
}

}  // namespace

std::vector<ClientStatus> getClientsStatus(const XrayConfig& cfg) {
    std::vector<ClientStatus> result;
    if (!cfg.configured) return result;

    try {
        json j = readConfig(cfg.configPath);
        auto& clients = clientsArray(j);
        result.reserve(clients.size());
        for (const auto& client : clients) {
            result.push_back({
                client.value("email", "(без имени)"),
                client.value("id", "")
            });
        }
    } catch (const std::exception&) {
        // Единый список остаётся доступным даже если конфиг Xray временно
        // недоступен. Детальное сообщение возвращает listClients().
    }
    return result;
}

std::string listClients(const XrayConfig& cfg) {
    if (!cfg.configured) {
        return "Xray не настроен в config.json бота. Установите его через "
               "scripts/install_xray.sh и добавьте секцию \"xray\" в config.json.";
    }

    json j;
    try {
        j = readConfig(cfg.configPath);
        auto& clients = clientsArray(j);
        if (clients.empty()) return "VLESS/Reality: клиентов пока нет.";

        std::ostringstream out;
        out << "VLESS/Reality: " << clients.size() << " клиентов\n\n";
        for (const auto& client : clients) {
            out << "\xE2\x9A\xAA " << client.value("email", "(без имени)") << "\n";
        }
        out << "\n(у VLESS/Reality нет внутреннего IP: ICMP-пинг и онлайн-статус "
                "для клиентов не определяются без отдельной телеметрии Xray)";
        return out.str();
    } catch (const std::exception& e) {
        return std::string("Не удалось прочитать конфиг Xray: ") + e.what();
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
    if (!hasLinkSettings(cfg)) {
        res.error = "В config.json не заполнены параметры xray для формирования ссылки.";
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

        std::string linkText = buildVlessLink(cfg, uuid, name);

        shell::run("mkdir -p " + cfg.clientsDir);
        std::string qrPath = cfg.clientsDir + "/vless_" + name + ".png";
        auto [qrCode, qrOut] = shell::run(
            "qrencode -t png -o " + qrPath + " \"" + linkText + "\"");
        if (qrCode == 0) chmod(qrPath.c_str(), S_IRUSR | S_IWUSR);

        res.ok = true;
        res.link = linkText;
        res.qrPath = (qrCode == 0) ? qrPath : "";
        return res;
    } catch (const std::exception& e) {
        res.error = std::string("Ошибка структуры конфига Xray: ") + e.what();
        return res;
    }
}

GetLinkResult getClientLink(const XrayConfig& cfg, const std::string& name) {
    GetLinkResult res;
    res.clientName = name;
    if (!cfg.configured) {
        res.error = "Xray не настроен в config.json бота.";
        return res;
    }
    if (!shell::isSafeToken(name)) {
        res.error = "Недопустимое имя клиента.";
        return res;
    }
    if (!hasLinkSettings(cfg)) {
        res.error = "В config.json не заполнены параметры xray для формирования ссылки.";
        return res;
    }

    try {
        json j = readConfig(cfg.configPath);
        auto& clients = clientsArray(j);
        for (const auto& client : clients) {
            if (client.value("email", "") != name) continue;
            const std::string uuid = client.value("id", "");
            if (uuid.empty()) {
                res.error = "У VLESS-клиента '" + name + "' отсутствует UUID в конфиге Xray.";
                return res;
            }

            res.link = buildVlessLink(cfg, uuid, name);
            shell::run("mkdir -p " + cfg.clientsDir);
            res.qrPath = cfg.clientsDir + "/vless_" + name + ".png";
            auto [qrCode, qrOut] = shell::run(
                "qrencode -t png -o " + res.qrPath + " \"" + res.link + "\"");
            if (qrCode != 0) {
                res.error = "Не удалось создать QR-код: " + qrOut;
                return res;
            }
            chmod(res.qrPath.c_str(), S_IRUSR | S_IWUSR);
            res.ok = true;
            return res;
        }
        res.error = "VLESS-клиент '" + name + "' не найден.";
    } catch (const std::exception& e) {
        res.error = std::string("Не удалось прочитать конфиг Xray: ") + e.what();
    }
    return res;
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
