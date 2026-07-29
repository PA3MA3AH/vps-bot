#include <tgbot/tgbot.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "config.hpp"
#include "shell_exec.hpp"
#include "system_info.hpp"
#include "wireguard_manager.hpp"

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
            "/status — состояние сервера\n"
            "/wg — список клиентов WireGuard\n"
            "/newclient <имя> — добавить клиента\n"
            "/deleteclient <имя> — удалить клиента\n"
            "/logs <сервис> — последние логи systemd-сервиса\n"
            "/update — обновление пакетов (с подтверждением)\n"
            "/reboot — перезагрузка сервера (с подтверждением)");
    });

    // ---- /status ----
    bot.getEvents().onCommand("status", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        bot.getApi().sendMessage(message->chat->id, sysinfo::getStatusText());
    });

    // ---- /wg ----
    bot.getEvents().onCommand("wg", [&](Message::Ptr message) {
        if (!isAllowed(cfg, message)) return;
        bot.getApi().sendMessage(message->chat->id, wg::listClients(cfg.wg));
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
