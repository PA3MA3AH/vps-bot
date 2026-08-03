#pragma once
#include <string>

namespace sysinfo {

// Формирует текст с CPU/RAM/диском/аптаймом (без данных WireGuard/Xray —
// их добавляет main.cpp отдельно, т.к. они требуют своих модулей).
std::string getStatusText();

// Формирует текст для /logs <service>
std::string getServiceLogs(const std::string& serviceName, int lines = 50);

}  // namespace sysinfo
