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

// У VLESS/Reality нет выдаваемого сервером внутреннего IP, поэтому
// онлайн-статус нельзя корректно определить ICMP-пингом как у WireGuard.
struct ClientStatus {
    std::string name;
    std::string id;
};

// Возвращает структурированный список клиентов первого VLESS-инбаунда.
std::vector<ClientStatus> getClientsStatus(const XrayConfig& cfg);

// Отдельный текстовый список клиентов Xray для внутреннего использования.
std::string listClients(const XrayConfig& cfg);

// Добавляет клиента: генерирует UUID, дописывает в inbounds[0].settings.clients
// конфига Xray, перезапускает сервис, формирует vless:// ссылку и QR-код.
AddResult addClient(const XrayConfig& cfg, const std::string& name);

// Удаляет клиента по имени (полю "email" в конфиге Xray).
RemoveResult removeClient(const XrayConfig& cfg, const std::string& name);

}  // namespace xray
