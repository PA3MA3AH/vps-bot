#pragma once
#include <string>
#include <vector>
#include "config.hpp"

namespace wg {

struct AddResult {
    bool ok = false;
    std::string error;
    std::string confPath;   // путь к .conf клиента
    std::string qrPath;     // путь к .png с QR-кодом
    std::string clientName;
};

// Список клиентов в человекочитаемом виде (для /wg)
std::string listClients(const WireGuardConfig& cfg);

// Добавляет нового клиента: генерирует ключи, добавляет peer в конфиг,
// применяет через `wg syncconf`, создаёт .conf и QR-код.
AddResult addClient(const WireGuardConfig& cfg, const std::string& name);

// Удаляет клиента по имени (ищет по комментарию "# name" в конфиге).
struct RemoveResult {
    bool ok = false;
    std::string error;
};
RemoveResult removeClient(const WireGuardConfig& cfg, const std::string& name);

}  // namespace wg
