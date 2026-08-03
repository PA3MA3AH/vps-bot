cmake_minimum_required(VERSION 3.16)
project(vpsbot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# tgbot-cpp устанавливается через `sudo make install` из исходников
# (см. README.md), после чего доступен как CMake-пакет TgBot.
find_package(TgBot CONFIG REQUIRED)

add_executable(vpsbot
    src/main.cpp
    src/system_info.cpp
    src/wireguard_manager.cpp
    src/xray_manager.cpp
    src/config.cpp
    src/shell_exec.cpp
)

target_include_directories(vpsbot PRIVATE include)

target_link_libraries(vpsbot PRIVATE
    TgBot::TgBot
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    pthread
)
