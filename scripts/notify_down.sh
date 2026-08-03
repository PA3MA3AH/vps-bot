#!/usr/bin/env bash
# Вызывается systemd (через OnFailure=) когда vpsbot.service переходит
# в состояние failed. Отправляет сообщение и файл с логами напрямую через
# Telegram Bot API (curl), в обход самого бота — он ведь как раз не работает.

set -euo pipefail

CONFIG_PATH="/opt/vpsbot/config.json"
LOG_FILE="/tmp/vpsbot_crash_$(date +%Y%m%d_%H%M%S).log"

if [ ! -f "$CONFIG_PATH" ]; then
    logger -t vpsbot-notify "config.json не найден по пути $CONFIG_PATH"
    exit 1
fi

BOT_TOKEN=$(jq -r '.bot_token' "$CONFIG_PATH")
CHAT_ID=$(jq -r '.allowed_user_id' "$CONFIG_PATH")

if [ -z "$BOT_TOKEN" ] || [ "$BOT_TOKEN" = "null" ]; then
    logger -t vpsbot-notify "Не удалось прочитать bot_token из config.json"
    exit 1
fi

# Последние 300 строк логов сервиса
journalctl -u vpsbot.service -n 300 --no-pager > "$LOG_FILE" 2>&1 || true

HOSTNAME_STR=$(hostname)
TIME_STR=$(date '+%Y-%m-%d %H:%M:%S')

CAPTION="🔴 vpsbot упал и не смог перезапуститься
Сервер: ${HOSTNAME_STR}
Время: ${TIME_STR}
Логи во вложении."

# Пробуем отправить файл с логами
HTTP_CODE=$(curl -s -o /tmp/vpsbot_notify_response.json -w "%{http_code}" \
    -F "chat_id=${CHAT_ID}" \
    -F "caption=${CAPTION}" \
    -F "document=@${LOG_FILE}" \
    "https://api.telegram.org/bot${BOT_TOKEN}/sendDocument" || echo "000")

if [ "$HTTP_CODE" != "200" ]; then
    # Файл не отправился (например, нет сети или сам Telegram недоступен) —
    # пробуем хотя бы короткое текстовое сообщение
    logger -t vpsbot-notify "sendDocument вернул HTTP $HTTP_CODE, пробую sendMessage"
    curl -s -X POST \
        -d "chat_id=${CHAT_ID}" \
        -d "text=${CAPTION}" \
        "https://api.telegram.org/bot${BOT_TOKEN}/sendMessage" > /dev/null || \
        logger -t vpsbot-notify "Не удалось отправить уведомление ни одним из способов"
fi

rm -f "$LOG_FILE"
