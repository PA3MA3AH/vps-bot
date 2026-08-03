# VPS Control Bot

Telegram-бот на C++ для управления вашим VPS и WireGuard. Отвечает только
пользователю с Telegram ID **1654167900** — все остальные полностью
игнорируются (бот не отвечает и не подтверждает своё существование).

## Возможности

- `/status` — CPU, RAM, диск, аптайм, число клиентов WireGuard
- `/wg` — список клиентов WireGuard (кто активен прямо сейчас)
- `/newclient <имя>` — создать нового клиента: генерирует ключи, добавляет
  peer в WireGuard "на лету" (без разрыва существующих соединений),
  сохраняет в конфиг для переживания перезагрузки, присылает `.conf` и QR-код
- `/deleteclient <имя>` — удалить клиента
- `/logs <сервис>` — последние 50 строк `journalctl -u <сервис>`
- `/update` — `apt update && apt upgrade -y`, с подтверждением через кнопку
- `/reboot` — перезагрузка сервера, с подтверждением через кнопку

## 1. Зависимости на VPS (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential cmake git libboost-system-dev \
    libssl-dev nlohmann-json3-dev qrencode wireguard-tools
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

## Дальнейшие шаги (по этапам из вашего плана)

- v0.2 ✅ реализовано в этой версии (`/newclient`, `/deleteclient`, QR)
- v0.3 частично: `/logs`, `/update` есть; можно добавить `/restart-service <имя>`
  по аналогии с `/logs`
- v1.0: меню на reply-кнопках, поддержка нескольких VPS (несколько блоков
  в config.json + выбор через команду), уведомления о падении диска/сервиса
  через периодический таймер (`std::thread` + `sleep` или systemd timer,
  который дергает отдельный endpoint бота)
