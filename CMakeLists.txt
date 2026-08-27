cmake_minimum_required(VERSION 3.16)
project(DiscordPresence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- 1. Auto-download Discord RPC ---
FetchContent_Declare(
    discord_rpc
    GIT_REPOSITORY https://github.com/discord/discord-rpc.git
    GIT_TAG        master
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(discord_rpc)

# --- 2. Auto-download nlohmann/json ---
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# --- Target Executable ---
add_executable(discord_presence src/main.cpp)

target_include_directories(discord_presence PRIVATE src)
target_link_libraries(discord_presence PRIVATE
    discord-rpc
    nlohmann_json::nlohmann_json
    gdiplus
    shell32
    psapi
    version
    winhttp
    crypt32
)

target_compile_definitions(discord_presence PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_custom_command(TARGET discord_presence POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.json
        $<TARGET_FILE_DIR:discord_presence>/config.json
)