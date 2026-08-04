#pragma once
#include <string>
#include <vector>
#include "config.hpp"

namespace wg {

struct ClientStatus {
    std::string name;
    std::string ip;          // без /32
    std::string publicKey;
    bool online = false;     // recent handshake, а не ping
    long long lastHandshake = 0;  // unix-время, 0 если не было
};

// Опрашивает `wg show <iface> dump`, сопоставляет имена из комментариев
// конфига и определяет online по свежести latest-handshake.
std::vector<ClientStatus> getClientsStatus(const WireGuardConfig& cfg);

// Форматирует список клиентов в текст для Telegram
std::string formatClientsList(const std::vector<ClientStatus>& clients);

// Удобный шорткат для форматирования отдельного списка WireGuard.
std::string listClients(const WireGuardConfig& cfg);

// Возвращает уже существующий .conf и QR клиента по имени (без регенерации ключей).
struct GetConfigResult {
    bool ok = false;
    std::string error;
    std::string confPath;
    std::string qrPath;
    std::string clientName;
};
GetConfigResult getClientConfig(const WireGuardConfig& cfg, const std::string& name);

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
// (например "10.66.66.5", как показано в /list), т.к. это единственное,
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
