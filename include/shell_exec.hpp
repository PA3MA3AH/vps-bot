#pragma once
#include <string>
#include <utility>

namespace shell {

// Запускает команду через popen(), возвращает {exit_code, stdout+stderr}.
// ВНИМАНИЕ: команда собирается через vararg-подобные строки — вызывающий код
// обязан сам экранировать/санитизировать любые пользовательские данные,
// прежде чем подставлять их в cmd (см. sanitizeName в wireguard_manager).
std::pair<int, std::string> run(const std::string& cmd);

// Проверяет, что строка содержит только [A-Za-z0-9_-], безопасно для
// использования в shell-командах и как имя файла.
bool isSafeToken(const std::string& s);

}  // namespace shell
