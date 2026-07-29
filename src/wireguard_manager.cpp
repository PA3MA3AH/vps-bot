#include "wireguard_manager.hpp"
#include "shell_exec.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wg {

namespace {

// Читает конфиг сервера и строит map PublicKey -> имя клиента
// (имя берётся из строки-комментария "# name", идущей перед PublicKey).
std::map<std::string, std::string> readNamesByPubkey(const std::string& configPath) {
    std::map<std::string, std::string> result;
    std::ifstream f(configPath);
    if (!f.is_open()) return result;

    std::string line, pendingName;
    while (std::getline(f, line)) {
        // обрезаем пробелы
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string();
            return s.substr(a, b - a + 1);
        };
        std::string t = trim(line);
        if (t.rfind("#", 0) == 0) {
            pendingName = trim(t.substr(1));
        } else if (t.rfind("PublicKey", 0) == 0) {
            auto pos = t.find('=');
            if (pos != std::string::npos && !pendingName.empty()) {
                std::string key = trim(t.substr(pos + 1));
                result[key] = pendingName;
                pendingName.clear();
            }
        }
    }
    return result;
}

// Находит следующий свободный последний октет в подсети (начиная с .2, т.к. .1 — сервер)
int nextFreeOctet(const std::string& configPath, const std::string& subnetBase) {
    std::ifstream f(configPath);
    std::string line;
    int maxOctet = 1;
    while (std::getline(f, line)) {
        auto pos = line.find("AllowedIPs");
        if (pos == std::string::npos) continue;
        auto prefixPos = line.find(subnetBase + ".");
        if (prefixPos == std::string::npos) continue;
        size_t start = prefixPos + subnetBase.size() + 1;
        size_t end = line.find_first_not_of("0123456789", start);
        if (end == std::string::npos) end = line.size();
        try {
            int octet = std::stoi(line.substr(start, end - start));
            maxOctet = std::max(maxOctet, octet);
        } catch (...) {}
    }
    return maxOctet + 1;
}

bool writeFileMode600(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    f << content;
    f.close();
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trimCopy(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

}  // namespace

std::string listClients(const WireGuardConfig& cfg) {
    auto names = readNamesByPubkey(cfg.configPath);

    auto [code, out] = shell::run("wg show " + cfg.interface + " dump");
    if (code != 0 || out.empty()) {
        return "Не удалось получить данные WireGuard (проверьте права доступа / интерфейс).";
    }

    std::istringstream iss(out);
    std::string line;
    bool first = true;
    std::ostringstream result;
    int total = 0, active = 0;
    long long now = static_cast<long long>(std::time(nullptr));

    std::vector<std::string> rows;

    while (std::getline(iss, line)) {
        if (first) { first = false; continue; }  // первая строка = сам интерфейс
        std::istringstream ls(line);
        std::vector<std::string> f;
        std::string tok;
        while (ls >> tok) f.push_back(tok);
        if (f.size() < 8) continue;

        std::string pubkey = f[0];
        std::string allowedIps = f[3];
        long long handshake = 0;
        try { handshake = std::stoll(f[4]); } catch (...) {}
        bool isActive = handshake > 0 && (now - handshake) < 180;

        total++;
        if (isActive) active++;

        std::string name = names.count(pubkey) ? names[pubkey] : "(без имени)";
        std::string status = isActive ? "\xF0\x9F\x9F\xA2" : "\xE2\x9A\xAA";

        rows.push_back(status + " " + name + "  " + allowedIps);
    }

    result << "WireGuard: " << total << " клиентов, " << active << " активны\n\n";
    for (auto& r : rows) result << r << "\n";
    return result.str();
}

AddResult addClient(const WireGuardConfig& cfg, const std::string& name) {
    AddResult res;
    res.clientName = name;

    if (!shell::isSafeToken(name)) {
        res.error = "Имя может содержать только буквы, цифры, - и _ (до 64 символов).";
        return res;
    }

    // Проверяем, что клиент с таким именем ещё не существует
    auto names = readNamesByPubkey(cfg.configPath);
    for (auto& [pk, n] : names) {
        if (n == name) {
            res.error = "Клиент с именем '" + name + "' уже существует.";
            return res;
        }
    }

    std::string tmpDir = "/tmp/vpsbot_" + name + "_" + std::to_string(::getpid());
    shell::run("mkdir -p " + tmpDir);

    shell::run("wg genkey > " + tmpDir + "/privatekey");
    shell::run("wg pubkey < " + tmpDir + "/privatekey > " + tmpDir + "/publickey");
    shell::run("wg genpsk > " + tmpDir + "/presharedkey");

    std::string privkey = trimCopy(readFile(tmpDir + "/privatekey"));
    std::string pubkey = trimCopy(readFile(tmpDir + "/publickey"));
    std::string psk = trimCopy(readFile(tmpDir + "/presharedkey"));

    if (privkey.empty() || pubkey.empty() || psk.empty()) {
        res.error = "Не удалось сгенерировать ключи WireGuard (проверьте, что установлен `wg`).";
        shell::run("rm -rf " + tmpDir);
        return res;
    }

    int octet = nextFreeOctet(cfg.configPath, cfg.subnetBase);
    std::string clientIp = cfg.subnetBase + "." + std::to_string(octet);

    // Добавляем peer в живой интерфейс
    auto [setCode, setOut] = shell::run(
        "wg set " + cfg.interface + " peer " + pubkey +
        " preshared-key " + tmpDir + "/presharedkey" +
        " allowed-ips " + clientIp + "/32");
    if (setCode != 0) {
        res.error = "wg set завершился с ошибкой: " + setOut;
        shell::run("rm -rf " + tmpDir);
        return res;
    }

    // Сохраняем peer в конфиг-файл, чтобы он пережил перезагрузку
    std::ofstream cf(cfg.configPath, std::ios::app);
    cf << "\n[Peer]\n"
       << "# " << name << "\n"
       << "PublicKey = " << pubkey << "\n"
       << "PresharedKey = " << psk << "\n"
       << "AllowedIPs = " << clientIp << "/32\n";
    cf.close();

    // Формируем клиентский .conf
    std::ostringstream clientConf;
    clientConf << "[Interface]\n"
                << "PrivateKey = " << privkey << "\n"
                << "Address = " << clientIp << "/32\n"
                << "DNS = " << cfg.dns << "\n\n"
                << "[Peer]\n"
                << "PublicKey = " << cfg.serverPublicKey << "\n"
                << "PresharedKey = " << psk << "\n"
                << "Endpoint = " << cfg.endpoint << "\n"
                << "AllowedIPs = 0.0.0.0/0, ::/0\n"
                << "PersistentKeepalive = 25\n";

    shell::run("mkdir -p " + cfg.clientsDir);
    std::string confPath = cfg.clientsDir + "/" + name + ".conf";
    std::string qrPath = cfg.clientsDir + "/" + name + ".png";

    if (!writeFileMode600(confPath, clientConf.str())) {
        res.error = "Не удалось записать client .conf на диск.";
        shell::run("rm -rf " + tmpDir);
        return res;
    }

    auto [qrCode, qrOut] = shell::run("qrencode -t png -r " + confPath + " -o " + qrPath);
    shell::run("rm -rf " + tmpDir);

    res.ok = true;
    res.confPath = confPath;
    res.qrPath = (qrCode == 0) ? qrPath : "";
    return res;
}

RemoveResult removeClient(const WireGuardConfig& cfg, const std::string& name) {
    RemoveResult res;
    if (!shell::isSafeToken(name)) {
        res.error = "Недопустимое имя клиента.";
        return res;
    }

    std::ifstream in(cfg.configPath);
    if (!in.is_open()) {
        res.error = "Не удалось открыть конфиг WireGuard.";
        return res;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    in.close();

    // Ищем блок [Peer], начинающийся с "# name"
    int blockStart = -1, blockEnd = -1;
    std::string foundPubkey;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = trimCopy(lines[i]);
        if (t == "[Peer]") {
            // смотрим, совпадает ли следующая строка-комментарий с именем
            if (i + 1 < lines.size() && trimCopy(lines[i + 1]) == "# " + name) {
                blockStart = static_cast<int>(i);
                size_t j = i + 1;
                for (; j < lines.size(); ++j) {
                    std::string tj = trimCopy(lines[j]);
                    if (tj.rfind("PublicKey", 0) == 0) {
                        auto pos = tj.find('=');
                        if (pos != std::string::npos) foundPubkey = trimCopy(tj.substr(pos + 1));
                    }
                    if (j > i + 1 && (tj == "[Peer]" || tj == "[Interface]")) break;
                }
                blockEnd = (j < lines.size() && trimCopy(lines[j]) != "[Peer]" &&
                            trimCopy(lines[j]) != "[Interface]")
                               ? static_cast<int>(j)
                               : static_cast<int>(j) - 1;
                // blockEnd указывает на последнюю строку блока включительно
                blockEnd = (j == lines.size()) ? static_cast<int>(lines.size()) - 1
                                                : static_cast<int>(j) - 1;
                break;
            }
        }
    }

    if (blockStart == -1 || foundPubkey.empty()) {
        res.error = "Клиент '" + name + "' не найден в конфиге.";
        return res;
    }

    // Удаляем peer из живого интерфейса
    shell::run("wg set " + cfg.interface + " peer " + foundPubkey + " remove");

    // Переписываем файл конфига без найденного блока
    std::ofstream out(cfg.configPath, std::ios::trunc);
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (i >= blockStart && i <= blockEnd) continue;
        out << lines[i] << "\n";
    }
    out.close();

    // Удаляем файлы клиента
    shell::run("rm -f " + cfg.clientsDir + "/" + name + ".conf");
    shell::run("rm -f " + cfg.clientsDir + "/" + name + ".png");

    res.ok = true;
    return res;
}

}  // namespace wg
