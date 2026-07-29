#include "system_info.hpp"
#include "shell_exec.hpp"

#include <sys/statvfs.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace sysinfo {

namespace {

struct CpuTimes {
    long long idle = 0;
    long long total = 0;
};

CpuTimes readCpuTimes() {
    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line);  // первая строка: "cpu  user nice system idle iowait irq softirq ..."
    std::istringstream iss(line);
    std::string cpuLabel;
    iss >> cpuLabel;

    std::vector<long long> vals;
    long long v;
    while (iss >> v) vals.push_back(v);

    CpuTimes t;
    if (vals.size() >= 4) {
        long long idle = vals[3] + (vals.size() > 4 ? vals[4] : 0);  // idle + iowait
        long long total = 0;
        for (auto x : vals) total += x;
        t.idle = idle;
        t.total = total;
    }
    return t;
}

double getCpuUsagePercent() {
    CpuTimes a = readCpuTimes();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CpuTimes b = readCpuTimes();

    long long totalDiff = b.total - a.total;
    long long idleDiff = b.idle - a.idle;
    if (totalDiff <= 0) return 0.0;
    return (1.0 - static_cast<double>(idleDiff) / totalDiff) * 100.0;
}

struct MemInfo {
    double totalMb = 0;
    double usedMb = 0;
};

MemInfo getMemInfo() {
    std::ifstream f("/proc/meminfo");
    std::string key;
    long long value;
    std::string unit;

    long long memTotal = 0, memAvailable = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        iss >> key >> value >> unit;
        if (key == "MemTotal:") memTotal = value;
        if (key == "MemAvailable:") memAvailable = value;
    }

    MemInfo m;
    m.totalMb = memTotal / 1024.0;
    m.usedMb = (memTotal - memAvailable) / 1024.0;
    return m;
}

struct DiskInfo {
    double totalGb = 0;
    double usedGb = 0;
};

DiskInfo getDiskInfo(const std::string& path = "/") {
    struct statvfs st{};
    DiskInfo d;
    if (statvfs(path.c_str(), &st) == 0) {
        double total = static_cast<double>(st.f_blocks) * st.f_frsize;
        double free = static_cast<double>(st.f_bfree) * st.f_frsize;
        d.totalGb = total / (1024.0 * 1024 * 1024);
        d.usedGb = (total - free) / (1024.0 * 1024 * 1024);
    }
    return d;
}

std::string getUptime() {
    std::ifstream f("/proc/uptime");
    double seconds = 0;
    f >> seconds;

    long total = static_cast<long>(seconds);
    long days = total / 86400;
    long hours = (total % 86400) / 3600;
    long mins = (total % 3600) / 60;

    std::ostringstream oss;
    if (days > 0) oss << days << "д ";
    oss << hours << "ч " << mins << "м";
    return oss.str();
}

int countWireGuardPeers(int& active) {
    // Использует `wg show <iface> dump`, если wg установлен и есть права.
    auto [code, out] = shell::run("wg show all dump");
    active = 0;
    int total = 0;
    std::istringstream iss(out);
    std::string line;
    long long now = static_cast<long long>(std::time(nullptr));
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::vector<std::string> fields;
        std::string tok;
        while (ls >> tok) fields.push_back(tok);
        // строка интерфейса имеет 4 поля, строка пира — 8
        if (fields.size() >= 8) {
            total++;
            long long handshake = 0;
            try { handshake = std::stoll(fields[4]); } catch (...) {}
            if (handshake > 0 && (now - handshake) < 180) active++;
        }
    }
    return total;
}

}  // namespace

std::string getStatusText() {
    double cpu = getCpuUsagePercent();
    MemInfo mem = getMemInfo();
    DiskInfo disk = getDiskInfo();
    std::string uptime = getUptime();

    int active = 0;
    int totalPeers = countWireGuardPeers(active);

    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "\xF0\x9F\x9F\xA2 VPS онлайн\n"
        "Аптайм: %s\n\n"
        "CPU: %.0f%%\n"
        "RAM: %.0f МБ / %.0f МБ\n"
        "Диск: %.1f / %.1f ГБ\n\n"
        "WireGuard:\n"
        "Клиентов: %d\n"
        "Активны: %d",
        uptime.c_str(), cpu, mem.usedMb, mem.totalMb,
        disk.usedGb, disk.totalGb, totalPeers, active);

    return std::string(buf);
}

std::string getServiceLogs(const std::string& serviceName, int lines) {
    if (!shell::isSafeToken(serviceName)) {
        return "Недопустимое имя сервиса.";
    }
    std::string cmd = "journalctl -u " + serviceName + " -n " +
                       std::to_string(lines) + " --no-pager";
    auto [code, out] = shell::run(cmd);
    if (out.size() > 3500) {
        out = out.substr(out.size() - 3500);  // Telegram лимит ~4096 символов
    }
    return out.empty() ? "Логи пусты или сервис не найден." : out;
}

}  // namespace sysinfo
