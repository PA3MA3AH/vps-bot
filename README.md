# VPS Control Bot

Telegram-бот на C++ для управления вашим VPS и WireGuard. Отвечает только
пользователю с Telegram ID **1654167900** — все остальные полностью
игнорируются (бот не отвечает и не подтверждает своё существование).

## Возможности

**Клиенты и WireGuard:**
- `/status` — CPU (с ядрами и load average), RAM, диск, аптайм и число клиентов
- `/list` — единый список WireGuard и VLESS/Reality. WireGuard считается онлайн
  по свежему WireGuard handshake (до 3 минут), поэтому телефон не обязан отвечать
  на ICMP ping. VLESS/Reality не выдаёт клиентам внутренние IP, поэтому его
  статус и ping не определяются
- `/newclient <имя>` — создать нового клиента: генерирует ключи, добавляет
  peer в WireGuard "на лету" (без разрыва существующих соединений),
  сохраняет в конфиг для переживания перезагрузки, присылает `.conf` и QR-код
- `/getwg <имя>` — повторно получить сохранённые `.conf` и QR-код без создания
  нового клиента
- `/deleteclient <имя>` — удалить клиента
- `/rename <ip> <новое_имя>` — задать/поменять имя клиенту по его IP (полезно
  для клиентов, которых вы когда-то добавляли в WireGuard вручную — они
  показывались в /list как "без имени", теперь можно проименовать)

**VLESS/Reality (для клиентов вроде Happ, v2rayNG, Shadowrocket):**
- `/newvless <имя>` — создать клиента, прислать ссылку `vless://...` и QR
- `/getvl <имя>` — повторно получить сохранённую VLESS-ссылку и QR без изменения
  клиента или перезапуска Xray
- `/deletevless <имя>` — удалить клиента

**Прочее:**
- `/logs <сервис>` — последние 50 строк `journalctl -u <сервис>`
- `/update` — `apt update && apt upgrade -y`, с подтверждением через кнопку
- `/reboot` — перезагрузка сервера, с подтверждением через кнопку
- Уведомление в Telegram с логами, если бот аварийно упадёт (см. ниже)

## 1. Зависимости на VPS (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential cmake git libboost-system-dev \
    libssl-dev nlohmann-json3-dev qrencode wireguard-tools iputils-ping \
    libcurl4-openssl-dev zlib1g-dev
```

### Сборка и установка tgbot-cpp

В официальных репозиториях библиотеки обычно нет, собираем из исходников:

```bash
git clone https://github.com/reo7sp/tgbot-cpp.git
cd tgbot-cpp
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## 2. Сборка бота

```bash
git clone <ваш_репозиторий_или_скопируйте_папку_vps-bot> vps-bot
cd vps-bot
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Бинарник появится в `build/vpsbot`.

## 3. Настройка

1. Создайте бота через [@BotFather](https://t.me/BotFather), получите токен.
2. Скопируйте пример конфига и заполните его:

```bash
cp config/config.example.json /opt/vpsbot/config.json
```

Обязательно заполните:
- `bot_token` — токен от BotFather
- `wireguard.server_public_key` — публичный ключ сервера (`cat /etc/wireguard/publickey`
  или посмотрите `PublicKey` в выводе `wg show wg0` на стороне сервера — либо
  получите его командой `wg pubkey < /etc/wireguard/privatekey`)
- `wireguard.endpoint` — публичный IP или домен сервера + порт WireGuard
- `wireguard.config_path` — путь к конфигу интерфейса (обычно `/etc/wireguard/wg0.conf`)

`allowed_user_id` уже установлен в `1654167900` — менять не нужно, если это
не ваш ID.

## 4. Установка как systemd-сервис

```bash
sudo mkdir -p /opt/vpsbot
sudo cp build/vpsbot /opt/vpsbot/
sudo cp config/config.example.json /opt/vpsbot/config.json  # и отредактируйте
sudo chmod 600 /opt/vpsbot/config.json

sudo cp systemd/vpsbot.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now vpsbot
sudo systemctl status vpsbot
```

Логи бота: `journalctl -u vpsbot -f`

## 5. VLESS/Reality для Happ (опционально, параллельно с WireGuard)

WireGuard и VLESS/Reality — независимые VPN-стеки, работают на сервере
одновременно и никак друг другу не мешают.

```bash
sudo bash scripts/install_xray.sh            # порт 8443, SNI www.microsoft.com по умолчанию
# либо со своими параметрами:
sudo bash scripts/install_xray.sh 443 www.cloudflare.com
```

Скрипт установит Xray-core, сгенерирует ключи Reality и в конце выведет
готовый JSON-фрагмент — скопируйте его в `/opt/vpsbot/config.json`, в секцию
`"xray"` (см. пример в `config/config.example.json`), затем:

```bash
sudo systemctl restart vpsbot
```

После этого в Telegram станут доступны `/list`, `/newvless <имя>`,
`/deletevless <имя>`. Присланную ботом `vless://` ссылку (или QR) добавляете
в Happ через "Импорт по ссылке" / сканирование QR.

**Про порт и SNI:** `target`/`serverNames` в конфиге Reality — это домен,
под который сервер маскируется (Reality проксирует незашифрованным клиентам
трафик реального `www.microsoft.com`, чтобы не выделяться на DPI). Выбирайте
домен, который стабильно открывается с VPS по TCP/443. Если 443 порт уже занят
чем-то другим на сервере — используйте другой порт для Xray (по умолчанию
скрипт берёт 8443), это не критично для маскировки.

Скрипт принудительно использует IPv4 для исходящих подключений Xray. Это
устраняет зависания при неполной IPv6-маршрутизации у VPS. Если IPv6 на сервере
точно настроен и нужен клиентам, замените `UseIPv4` в `outbounds[0].settings`
на подходящую стратегию и перезапустите `xray`.

## Важные замечания по безопасности

1. **Права root.** Бот управляет WireGuard и systemd, поэтому в примере
   запускается от root. Это удобно, но рискованно: если в боте есть баг или
   утечёт токен — под угрозой весь сервер. Для продакшена рассмотрите:
   - выделенного пользователя + точечные `sudo` правила без пароля только
     для конкретных команд (`wg`, `systemctl restart <whitelist>`,
     `journalctl`, `apt-get`), прописанные в `/etc/sudoers.d/vpsbot`;
   - `CAP_NET_ADMIN` вместо root для операций с WireGuard
     (`setcap cap_net_admin+ep /opt/vpsbot/vpsbot`).
2. **Проверка ID отправителя** реализована во всех обработчиках команд и
   callback-кнопок — сообщения от других ID полностью игнорируются.
3. **Подтверждения** для опасных операций (`/update`, `/reboot`) через
   inline-кнопки — уже реализовано.
4. **Файл конфига** с токеном бота имеет права `600` — храните его так же.
5. Формат парсинга `wg0.conf` в этом боте рассчитан на конфиги, созданные
   самим ботом (комментарий `# имя` перед каждым `PublicKey`). Если у вас уже
   есть клиенты, добавленные вручную — допишите к ним такие же комментарии,
   иначе они будут отображаться как "(без имени)".

## Уведомление о падении бота (в файл)

Если `vpsbot` упадёт и не сможет перезапуститься (после 5 попыток за 5 минут),
`systemd` сам, в обход самого бота, отправит вам в Telegram сообщение с
последними 300 строками логов через прямой вызов Telegram Bot API (`curl`).

### Установка

```bash
apt install -y jq curl bsdutils   # jq и curl нужны скрипту уведомителя

cp scripts/notify_down.sh /opt/vpsbot/
chmod +x /opt/vpsbot/notify_down.sh

cp systemd/vpsbot.service /etc/systemd/system/       # обновлённая версия с OnFailure=
cp systemd/vpsbot-notify.service /etc/systemd/system/

systemctl daemon-reload
systemctl restart vpsbot
```

### Как проверить, что работает

Не дожидаясь реального краша, можно вручную запустить уведомитель:

```bash
systemctl start vpsbot-notify.service
```

В Telegram должно прийти сообщение с логами. Если не пришло — смотрите:

```bash
journalctl -u vpsbot-notify.service -n 30 --no-pager
```

Частые причины: не установлен `jq`/`curl`, неверный `bot_token` в
`/opt/vpsbot/config.json`, либо у сервера нет исходящего доступа в интернет
(например, если весь трафик, кроме WireGuard, зарезан firewall'ом).

### Важные ограничения

- Уведомление сработает только при **аварийном** падении (после исчерпания
  лимита перезапусков). Если вы сами остановите бота командой
  `systemctl stop vpsbot` — уведомления не будет, это ожидаемо (вы и так
  знаете, что остановили его сами).
- Если упадёт вся сеть на сервере (а не только бот) — уведомление тоже не
  сможет отправиться, оно ведь тоже использует эту же сеть. Для полной
  защиты от полного падения сервера нужен внешний мониторинг (например,
  Uptime Kuma / Healthchecks.io на другом хосте, который дёргает бота
  снаружи и сам присылает алерт, если тот не отвечает) — можем сделать
  это отдельным шагом, если понадобится.

## Дальнейшие шаги

- v0.2 ✅ (`/newclient`, `/deleteclient`, QR)
- v0.3 ✅ (`/logs`, `/update`, `/reboot`, уведомление о падении)
- v0.4 ✅ (VLESS/Reality параллельно с WireGuard, реальный пинг-статус,
  `/rename`)
- Дальше: меню на reply-кнопках, поддержка нескольких VPS (несколько блоков
  в config.json + выбор через команду), `/restart-service <имя>` по
  аналогии с `/logs`, внешний мониторинг на случай падения всего сервера
  целиком (см. "Важные ограничения" выше)
