#pragma once
#include <discord_rpc.h>
#include <string>
#include <cstdio>

inline void rpc_init(const std::string& app_id) {
    DiscordEventHandlers h{};
    h.ready = [](const DiscordUser* u) {
        printf("[RPC] Connected as %s#%s\n", u->username, u->discriminator);
    };
    h.disconnected = [](int code, const char* msg) {
        printf("[RPC] Disconnected: %d %s\n", code, msg);
    };
    h.errored = [](int code, const char* msg) {
        printf("[RPC] Error: %d %s\n", code, msg);
    };
    Discord_Initialize(app_id.c_str(), &h, 1, nullptr);
}

inline void rpc_set(const std::string& details,
                    const std::string& state,
                    const std::string& large_key,
                    const std::string& large_text,
                    int64_t start_ts) {
    DiscordRichPresence p{};
    p.details        = details.empty()   ? nullptr : details.c_str();
    p.state          = state.empty()     ? nullptr : state.c_str();
    p.largeImageKey  = large_key.empty() ? nullptr : large_key.c_str();
    p.largeImageText = large_text.empty()? nullptr : large_text.c_str();
    p.startTimestamp = start_ts;
    Discord_UpdatePresence(&p);
}

inline void rpc_clear() { Discord_ClearPresence(); }
inline void rpc_shutdown() { Discord_ClearPresence(); Discord_Shutdown(); }