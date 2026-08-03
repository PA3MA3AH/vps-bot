#pragma once
#include <string>
#include <vector>
#include "config.hpp"

namespace wg {

struct ClientStatus {
    std::string name;
    std::string ip;          // без /32
    std::string publicKey;
    bool online = false;     // определяется реальным ICMP-пингом на ip
    long long lastHandshake = 0;  // unix-время, 0 если не было
};

// Опрашивает `wg show <iface> dump`, сопоставляет имена из комментариев
// конфига и параллельно пингует каждого клиента по его внутреннему IP,
// чтобы определить, онлайн ли он прямо сейчас (а не просто "был онлайн
// когда-то за последние N минут" по handshake).
std::vector<ClientStatus> getClientsStatus(const WireGuardConfig& cfg);

// Форматирует список клиентов в текст для Telegram
std::string formatClientsList(const std::vector<ClientStatus>& clients);

// Удобный шорткат для /wg
std::string listClients(const WireGuardConfig& cfg);

// Добавляет нового клиента: генерирует ключи, добавляет peer в конфиг,
// применяет через `wg set`, создаёт .conf и QR-код.
struct AddResult {
    bool ok = false;
    std::string error;
    std::string confPath;   // путь к .conf клиента
    std::string qrPath;     // путь к .png с QR-кодом
    std::string clientName;
};
AddResult addClient(const WireGuardConfig& cfg, const std::string& name);

// Удаляет клиента по имени (ищет по комментарию "# name" в конфиге).
struct RemoveResult {
    bool ok = false;
    std::string error;
};
RemoveResult removeClient(const WireGuardConfig& cfg, const std::string& name);

// Переименовывает существующего клиента. identifier — это его IP
// (например "10.66.66.5", как показано в /wg), т.к. это единственное,
// что гарантированно есть у любого пира, даже у добавленных вручную без
// имени. Если у пира уже был комментарий с именем — заменяет его, если
// не было (частый случай для клиентов, настроенных руками) — добавляет.
struct RenameResult {
    bool ok = false;
    std::string error;
    std::string oldName;
};
RenameResult renameClient(const WireGuardConfig& cfg, const std::string& identifier,
                           const std::string& newName);

}  // namespace wg
