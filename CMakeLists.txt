cmake_minimum_required(VERSION 3.16...3.30)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

project(DiscordPresence LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Matikan build examples discord-rpc
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# --- 1. Discord RPC (dengan auto-fix file .clang-format) ---
FetchContent_Declare(
    discord_rpc
    GIT_REPOSITORY https://github.com/discord/discord-rpc.git
    GIT_TAG        master
)

FetchContent_GetProperties(discord_rpc)
if(NOT discord_rpc_POPULATED)
    FetchContent_Populate(discord_rpc)
    
    # Hapus file .clang-format yang korup dari discord-rpc
    file(REMOVE "${discord_rpc_SOURCE_DIR}/.clang-format")
    
    # Masukkan sebagai subproject
    add_subdirectory(${discord_rpc_SOURCE_DIR} ${discord_rpc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# --- 2. JSON Parser ---
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
