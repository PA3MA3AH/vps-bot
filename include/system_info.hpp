#pragma once
#include <string>

namespace sysinfo {

// Формирует готовый текст для команды /status
std::string getStatusText();

// Формирует текст для /logs <service>
std::string getServiceLogs(const std::string& serviceName, int lines = 50);

}  // namespace sysinfo
