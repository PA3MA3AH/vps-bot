#pragma once
#include <string>
#include <vector>
#include "config.hpp"

namespace xray {

struct AddResult {
    bool ok = false;
    std::string error;
    std::string link;       // vless://... ссылка для импорта в Happ
    std::string qrPath;     // путь к .png с QR-кодом ссылки
    std::string clientName;
};

struct RemoveResult {
    bool ok = false;
    std::string error;
};

// Список текущих клиентов Xray (для /vless)
std::string listClients(const XrayConfig& cfg);

// Добавляет клиента: генерирует UUID, дописывает в inbounds[0].settings.clients
// конфига Xray, перезапускает сервис, формирует vless:// ссылку и QR-код.
AddResult addClient(const XrayConfig& cfg, const std::string& name);

// Удаляет клиента по имени (полю "email" в конфиге Xray).
RemoveResult removeClient(const XrayConfig& cfg, const std::string& name);

}  // namespace xray
