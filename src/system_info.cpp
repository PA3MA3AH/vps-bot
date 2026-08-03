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
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    CpuTimes b = readCpuTimes();

    long long totalDiff = b.total - a.total;
    long long idleDiff = b.idle - a.idle;
    if (totalDiff <= 0) return 0.0;
    double pct = (1.0 - static_cast<double>(idleDiff) / totalDiff) * 100.0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

std::string getLoadAverage() {
    std::ifstream f("/proc/loadavg");
    double one = 0, five = 0, fifteen = 0;
    f >> one >> five >> fifteen;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", one, five, fifteen);
    return std::string(buf);
}

int getCpuCoreCount() {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    int count = 0;
    while (std::getline(f, line)) {
        if (line.rfind("processor", 0) == 0) count++;
    }
    return count > 0 ? count : 1;
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

}  // namespace

std::string getStatusText() {
    double cpu = getCpuUsagePercent();
    std::string loadAvg = getLoadAverage();
    int cores = getCpuCoreCount();
    MemInfo mem = getMemInfo();
    DiskInfo disk = getDiskInfo();
    std::string uptime = getUptime();

    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "\xF0\x9F\x9F\xA2 VPS онлайн\n"
        "Аптайм: %s\n\n"
        "CPU: %.0f%% (ядер: %d)\n"
        "Нагрузка (1/5/15 мин): %s\n"
        "RAM: %.0f МБ / %.0f МБ\n"
        "Диск: %.1f / %.1f ГБ",
        uptime.c_str(), cpu, cores, loadAvg.c_str(), mem.usedMb, mem.totalMb,
        disk.usedGb, disk.totalGb);

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
