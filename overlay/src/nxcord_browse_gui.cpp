#include "nxcord_browse_gui.hpp"

#include "nxcord_main_gui.hpp"
#include "nxcord_overlay_utils.hpp"

NXCordBrowseGui::NXCordBrowseGui(NXCordOverlay &overlay, int64_t server_id)
    : NXCordCustomGui(overlay), _server_id(server_id) {}

tsl::elm::Element *NXCordBrowseGui::createUI() {
  auto &ipc = _overlay.getIPCClient();
  std::string title = _server_id ? ipc->getServer(_server_id).name : "Server Discord";
  auto frame = new tsl::elm::OverlayFrame(title.empty() ? "Canali vocali" : title,
                                         "A Seleziona  /  B Indietro");
  auto list = new tsl::elm::List();
  if (!ipc->isConnected()) {
    list->addItem(new tsl::elm::ListItem("Connect to Discord first"));
  } else if (_server_id == 0) {
    auto servers = ipc->getServers();
    std::sort(servers.begin(), servers.end(), [](const auto &a, const auto &b) {
      return std::strcmp(a.name, b.name) < 0;
    });
    for (const auto &server : servers) {
      auto item = new tsl::elm::ListItem(server.name);
      item->setClickListener([this, server](u64 keys) {
        if (!(keys & KEY_A)) return false;
        _overlay.changeTo<NXCordBrowseGui>(_overlay, server.id);
        return true;
      });
      list->addItem(item);
    }
    if (servers.empty()) {
      list->addItem(new tsl::elm::ListItem("No servers available"));
    }
  } else {
    size_t count = 0;
    auto channels = ipc->getChannels(_server_id);
    std::sort(channels.begin(), channels.end(), [](const auto &a, const auto &b) {
      return std::strcmp(a.name, b.name) < 0;
    });
    for (const auto &channel : channels) {
      if (channel.type != IPCStruct::DiscordChannelType::SERVER_VOICE) continue;
      auto item = new tsl::elm::ListItem(channel.name);
      item->setClickListener([this, channel](u64 keys) {
        if (!(keys & KEY_A)) return false;
        NXCordOverlay &overlay = _overlay;
        overlay.getIPCClient()->joinVoiceChannel(channel.serverId, channel.id);
        overlay.clearStack();
        overlay.changeTo<NXCordMainGui>(overlay);
        return true;
      });
      list->addItem(item);
      ++count;
    }
    if (count == 0) {
      list->addItem(new tsl::elm::ListItem("No voice channels available"));
    }
  }
  frame->setContent(list);
  return frame;
}
