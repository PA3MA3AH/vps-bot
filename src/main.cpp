#include <tgbot/tgbot.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "config.hpp"
#include "shell_exec.hpp"
#include "system_info.hpp"
#include "wireguard_manager.hpp"
#include "xray_manager.hpp"

using namespace TgBot;

namespace {

// true, если сообщение пришло от разрешённого пользователя.
// Для НЕ разрешённых — бот молчит (ничего не отвечает и не подтверждает
// сам факт своего существования/функциональности).
bool isAllowed(const AppConfig& cfg, const Message::Ptr& msg) {
    return msg->from && msg->from->id == cfg.allowedUserId;
}

std::string readFileToString(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string formatAllClients(const AppConfig& cfg) {
    const auto wgClients = wg::getClientsStatus(cfg.wg);
    const auto xrayClients = xray::getClientsStatus(cfg.xray);

    std::ostringstream out;
    out << "Клиенты: " << (wgClients.size() + xrayClients.size()) << "\n\n";
    for (const auto& client : wgClients) {
        out << (client.online ? "🟢" : "⚪") << " " << client.name
            << " | WG | " << client.ip << "\n";
    }
    for (const auto& client : xrayClients) {
        out << "⚪ " << client.name << " | VL\n";
    }
    if (wgClients.empty() && xrayClients.empty()) {
        out << "— клиентов нет\n";
    }
    return out.str();
}

}  // namespace

int main() {
    AppConfig cfg;
    try {
        cfg = AppConfig::load("config.json");
    } catch (const std::exception& e) {
        std::cerr << "Ошибка конфигурации: " << e.what() << std::endl;
        return 1;
    }

    Bot bot(cfg.botToken);

    // ---- /start, /help ----
    bot.getEvents().onCommand("start", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        bot.getApi().sendMessage(message->chat->id,
            "Бот управления VPS запущен.\n\n"
             "Статус:\n"
             "/status — состояние сервера\n"
             "/list — общий список WireGuard и VLESS/Reality\n\n"
             "WireGuard:\n"
             "/newclient <имя> — добавить клиента\n"
             "/getwg <имя> — получить сохранённые конфиг и QR\n"
             "/deleteclient <имя> — удалить клиента\n"
             "/rename <ip> <новое_имя> — переименовать клиента (ip — как в /list)\n\n"
             "VLESS/Reality (для Happ и т.п.):\n"
             "/newvless <имя> — добавить клиента\n"
             "/getvl <имя> — получить ссылку и QR повторно\n"
             "/deletevless <имя> — удалить клиента\n\n"
            "Прочее:\n"
            "/logs <сервис> — последние логи systemd-сервиса\n"
            "/update — обновление пакетов (с подтверждением)\n"
            "/reboot — перезагрузка сервера (с подтверждением)");
    });

    // ---- /status ----
    bot.getEvents().onCommand("status", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::string text = sysinfo::getStatusText();

        auto wgClients = wg::getClientsStatus(cfg.wg);
        int wgOnline = 0;
        for (auto& c : wgClients) if (c.online) wgOnline++;
        text += "\n\nWireGuard: " + std::to_string(wgClients.size()) + " клиентов, " +
                std::to_string(wgOnline) + " онлайн";

        if (cfg.xray.configured) {
            std::string xrayList = xray::listClients(cfg.xray);
            // берём только первую строку с числом клиентов, без деталей
            std::istringstream iss(xrayList);
            std::string firstLine;
            std::getline(iss, firstLine);
            text += "\nVLESS/Reality: " + firstLine;
        }

        bot.getApi().sendMessage(message->chat->id, text);
    });

    // ---- /list ----
    bot.getEvents().onCommand("list", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        bot.getApi().sendMessage(message->chat->id, "Получаю список клиентов...");
        bot.getApi().sendMessage(message->chat->id, formatAllClients(cfg));
    });

    // ---- /newclient <имя> ----
    bot.getEvents().onCommand("newclient", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;

        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /newclient <имя>");
            return;
        }

        bot.getApi().sendMessage(message->chat->id, "Создаю клиента '" + name + "'...");
        auto result = wg::addClient(cfg.wg, name);

        if (!result.ok) {
            bot.getApi().sendMessage(message->chat->id, "Ошибка: " + result.error);
            return;
        }

        bot.getApi().sendDocument(message->chat->id,
            InputFile::fromFile(result.confPath, "application/octet-stream"));

        if (!result.qrPath.empty()) {
            bot.getApi().sendPhoto(message->chat->id,
                InputFile::fromFile(result.qrPath, "image/png"));
        }

        bot.getApi().sendMessage(message->chat->id,
            "Клиент '" + name + "' успешно создан и добавлен в WireGuard.");
    });

    // ---- /getwg <имя> ----
    bot.getEvents().onCommand("getwg", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;
        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /getwg <имя>");
            return;
        }

        auto result = wg::getClientConfig(cfg.wg, name);
        if (!result.ok) {
            bot.getApi().sendMessage(message->chat->id, "Ошибка: " + result.error);
            return;
        }
        bot.getApi().sendDocument(message->chat->id,
            InputFile::fromFile(result.confPath, "application/octet-stream"));
        if (!result.qrPath.empty()) {
            bot.getApi().sendPhoto(message->chat->id,
                InputFile::fromFile(result.qrPath, "image/png"));
        }
    });

    // ---- /deleteclient <имя> ----
    bot.getEvents().onCommand("deleteclient", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;

        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /deleteclient <имя>");
            return;
        }

        auto result = wg::removeClient(cfg.wg, name);
        bot.getApi().sendMessage(message->chat->id,
            result.ok ? ("Клиент '" + name + "' удалён.") : ("Ошибка: " + result.error));
    });

    // ---- /rename <ip> <новое_имя> ----
    bot.getEvents().onCommand("rename", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, ip, newName;
        iss >> cmd >> ip >> newName;

        if (ip.empty() || newName.empty()) {
            bot.getApi().sendMessage(message->chat->id,
                "Использование: /rename <ip> <новое_имя>\n"
                "IP берите из вывода /list, например: /rename 10.66.66.4 Ноутбук_Рабочий");
            return;
        }

        auto result = wg::renameClient(cfg.wg, ip, newName);
        if (!result.ok) {
            bot.getApi().sendMessage(message->chat->id, "Ошибка: " + result.error);
        } else {
            bot.getApi().sendMessage(message->chat->id,
                "Переименовано: '" + result.oldName + "' -> '" + newName + "'");
        }
    });

    // ---- /newvless <имя> ----
    bot.getEvents().onCommand("newvless", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;

        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /newvless <имя>");
            return;
        }

        bot.getApi().sendMessage(message->chat->id, "Создаю VLESS-клиента '" + name + "'...");
        auto result = xray::addClient(cfg.xray, name);

        if (!result.ok) {
            bot.getApi().sendMessage(message->chat->id, "Ошибка: " + result.error);
            return;
        }

        if (!result.qrPath.empty()) {
            bot.getApi().sendPhoto(message->chat->id,
                InputFile::fromFile(result.qrPath, "image/png"));
        }
        bot.getApi().sendMessage(message->chat->id,
            "Клиент '" + name + "' создан.\n\nСсылка для импорта в Happ:\n" + result.link);
    });

    // ---- /getvl <имя> ----
    bot.getEvents().onCommand("getvl", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;
        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /getvl <имя>");
            return;
        }

        auto result = xray::getClientLink(cfg.xray, name);
        if (!result.ok) {
            bot.getApi().sendMessage(message->chat->id, "Ошибка: " + result.error);
            return;
        }
        bot.getApi().sendPhoto(message->chat->id,
            InputFile::fromFile(result.qrPath, "image/png"));
        bot.getApi().sendMessage(message->chat->id,
            "Ссылка для '" + name + "':\n" + result.link);
    });

    // ---- /deletevless <имя> ----
    bot.getEvents().onCommand("deletevless", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, name;
        iss >> cmd >> name;

        if (name.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /deletevless <имя>");
            return;
        }

        auto result = xray::removeClient(cfg.xray, name);
        bot.getApi().sendMessage(message->chat->id,
            result.ok ? ("VLESS-клиент '" + name + "' удалён.") : ("Ошибка: " + result.error));
    });

    // ---- /logs <сервис> ----
    bot.getEvents().onCommand("logs", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        std::istringstream iss(message->text);
        std::string cmd, service;
        iss >> cmd >> service;

        if (service.empty()) {
            bot.getApi().sendMessage(message->chat->id, "Использование: /logs <имя_сервиса>");
            return;
        }

        std::string logs = sysinfo::getServiceLogs(service);
        bot.getApi().sendMessage(message->chat->id, "```\n" + logs + "\n```", nullptr, nullptr,
                                  nullptr, "MarkdownV2");
    });

    // ---- /update (с подтверждением) ----
    bot.getEvents().onCommand("update", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
        std::vector<InlineKeyboardButton::Ptr> row;

        InlineKeyboardButton::Ptr yes(new InlineKeyboardButton);
        yes->text = "Да, обновить";
        yes->callbackData = "confirm_update";
        row.push_back(yes);

        InlineKeyboardButton::Ptr no(new InlineKeyboardButton);
        no->text = "Отмена";
        no->callbackData = "cancel";
        row.push_back(no);

        keyboard->inlineKeyboard.push_back(row);

        bot.getApi().sendMessage(message->chat->id,
            "Запустить `apt update && apt upgrade -y`?", nullptr, nullptr, keyboard);
    });

    // ---- /reboot (с подтверждением) ----
    bot.getEvents().onCommand("reboot", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;

        InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
        std::vector<InlineKeyboardButton::Ptr> row;

        InlineKeyboardButton::Ptr yes(new InlineKeyboardButton);
        yes->text = "Да, перезагрузить";
        yes->callbackData = "confirm_reboot";
        row.push_back(yes);

        InlineKeyboardButton::Ptr no(new InlineKeyboardButton);
        no->text = "Отмена";
        no->callbackData = "cancel";
        row.push_back(no);

        keyboard->inlineKeyboard.push_back(row);

        bot.getApi().sendMessage(message->chat->id,
            "Перезагрузить сервер? Бот тоже перезапустится.", nullptr, nullptr, keyboard);
    });

    // ---- Обработка нажатий на inline-кнопки ----
    bot.getEvents().onCallbackQuery([&](CallbackQuery::Ptr query) {
        if (!query->from || query->from->id != cfg.allowedUserId) {
            bot.getApi().answerCallbackQuery(query->id, "Доступ запрещён", true);
            return;
        }

        if (query->data == "cancel") {
            bot.getApi().answerCallbackQuery(query->id, "Отменено");
            bot.getApi().editMessageText("Отменено.", query->message->chat->id,
                                          query->message->messageId);
            return;
        }

        if (query->data == "confirm_update") {
            bot.getApi().answerCallbackQuery(query->id, "Запускаю обновление...");
            bot.getApi().editMessageText("Обновляю пакеты, это может занять пару минут...",
                                          query->message->chat->id, query->message->messageId);

            auto [code, out] = shell::run("apt-get update && apt-get upgrade -y");
            if (out.size() > 3500) out = out.substr(out.size() - 3500);
            bot.getApi().sendMessage(query->message->chat->id,
                (code == 0 ? "Обновление завершено:\n\n" : "Обновление завершилось с ошибкой:\n\n") + out);
            return;
        }

        if (query->data == "confirm_reboot") {
            bot.getApi().answerCallbackQuery(query->id, "Перезагружаю...");
            bot.getApi().editMessageText("Перезагружаю сервер...", query->message->chat->id,
                                          query->message->messageId);
            shell::run("systemctl reboot");
            return;
        }
    });

    try {
        std::cout << "Bot username: " << bot.getApi().getMe()->username << std::endl;
        bot.getApi().deleteWebhook();

        TgLongPoll longPoll(bot);
        std::cout << "Бот запущен, ожидаю команды..." << std::endl;
        while (true) {
            longPoll.start();
        }
    } catch (std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
