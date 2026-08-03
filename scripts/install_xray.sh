#!/usr/bin/env bash
# Устанавливает Xray-core и настраивает инбаунд VLESS+Reality, чтобы к серверу
# можно было подключаться клиентами типа Happ (v2rayNG, Shadowrocket и т.п.),
# независимо и параллельно с уже работающим WireGuard.
#
# Использование:
#   sudo bash install_xray.sh [порт] [sni-домен-для-маскировки]
#
# Примеры:
#   sudo bash install_xray.sh                                  # порт 8443, sni www.microsoft.com
#   sudo bash install_xray.sh 443 www.cloudflare.com

set -euo pipefail

PORT="${1:-8443}"
SNI="${2:-www.microsoft.com}"

if [ "$(id -u)" -ne 0 ]; then
    echo "Запустите скрипт от root (sudo)." >&2
    exit 1
fi

echo "==> Устанавливаю Xray-core..."
bash -c "$(curl -L https://github.com/XTLS/Xray-install/raw/main/install-release.sh)" @ install

mkdir -p /usr/local/etc/xray

echo "==> Генерирую ключи Reality..."
KEY_OUTPUT=$(/usr/local/bin/xray x25519)
PRIVATE_KEY=$(echo "$KEY_OUTPUT" | grep -iE "Private ?Key" | sed 's/^[^:]*: *//')
PUBLIC_KEY=$(echo "$KEY_OUTPUT" | grep -iE "Public ?Key" | sed 's/^[^:]*: *//')

if [ -z "$PRIVATE_KEY" ] || [ -z "$PUBLIC_KEY" ]; then
    echo "Не удалось сгенерировать ключи Reality (неожиданный вывод xray x25519)." >&2
    echo "$KEY_OUTPUT" >&2
    exit 1
fi

SHORT_ID=$(openssl rand -hex 8)

cat > /usr/local/etc/xray/config.json << EOF
{
  "log": { "loglevel": "warning" },
  "inbounds": [
    {
      "listen": "0.0.0.0",
      "port": ${PORT},
      "protocol": "vless",
      "settings": {
        "clients": [],
        "decryption": "none"
      },
      "streamSettings": {
        "network": "tcp",
        "security": "reality",
        "realitySettings": {
          "show": false,
          "dest": "${SNI}:443",
          "xver": 0,
          "serverNames": ["${SNI}"],
          "privateKey": "${PRIVATE_KEY}",
          "shortIds": ["${SHORT_ID}"]
        }
      }
    }
  ],
  "outbounds": [
    { "protocol": "freedom", "tag": "direct" }
  ]
}
EOF

echo "==> Открываю порт ${PORT}/tcp в ufw (если он используется)..."
if command -v ufw > /dev/null 2>&1; then
    ufw allow "${PORT}/tcp" || true
fi

echo "==> Запускаю Xray..."
systemctl enable xray
systemctl restart xray
sleep 1
systemctl --no-pager status xray | head -5

SERVER_IP=$(curl -s -4 ifconfig.me || hostname -I | awk '{print $1}')

echo ""
echo "=================================================="
echo "Xray + VLESS Reality установлен и запущен."
echo ""
echo "Впишите это в config.json бота, в секцию \"xray\":"
echo ""
echo "  \"xray\": {"
echo "    \"config_path\": \"/usr/local/etc/xray/config.json\","
echo "    \"endpoint\": \"${SERVER_IP}\","
echo "    \"port\": ${PORT},"
echo "    \"public_key\": \"${PUBLIC_KEY}\","
echo "    \"short_id\": \"${SHORT_ID}\","
echo "    \"server_name\": \"${SNI}\""
echo "  }"
echo ""
echo "После этого перезапустите бота: systemctl restart vpsbot"
echo "Дальше клиентов добавляйте командой /newvless <имя> прямо в Telegram."
echo "=================================================="
