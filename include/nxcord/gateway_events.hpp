#pragma once

#include <string>

inline bool nxcord_gateway_event(const std::string &event) {
  return event == "READY" || event == "RESUMED" ||
         event == "GUILD_CREATE" || event == "GUILD_UPDATE" ||
         event == "GUILD_DELETE" || event == "CHANNEL_CREATE" ||
         event == "CHANNEL_UPDATE" || event == "CHANNEL_DELETE" ||
         event == "VOICE_STATE_UPDATE" || event == "VOICE_SERVER_UPDATE";
}
