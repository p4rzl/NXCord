#include "nxcord_voice_channels_gui.hpp"

#include "nxcord_overlay_utils.hpp"
#include "nxcord_user_settings.hpp"
#include "nxcord_main_gui.hpp"

NXCordVoiceChannelsGui::NXCordVoiceChannelsGui(NXCordOverlay& overlay)
    : NXCordCustomGui(overlay) {}

void NXCordVoiceChannelsGui::populateList() {
  if (!_list) {
    return;
  }
  removeFocus();
  _list->clear();
  int64_t user_id = _user_id;
  for (const auto& state : _states) {
    auto item = new tsl::elm::ListItem(state.name);
    item->setClickListener([this, user_id, state](u64 keys) {
      if (keys & KEY_A && user_id != state.userId) {
        _overlay.changeTo<NXCordUserSettings>(state, _overlay);
        return true;
      }
      return false;
    });
    _list->addItem(item);
  }
  if (getTopElement() != nullptr) {
    requestFocus(getTopElement(), tsl::FocusDirection::None);
  }
}

tsl::elm::Element* NXCordVoiceChannelsGui::createUI() {
  auto& ipc_client = _overlay.getIPCClient();
  IPCStruct::DiscordChannel channel = ipc_client->getConnectedVoiceChannel();
  IPCStruct::DiscordServer server = ipc_client->getServer(channel.serverId);
  _user_id = ipc_client->getUserID();

  auto rootFrame = new tsl::elm::OverlayFrame(server.name, channel.name);
  _list = new tsl::elm::List();

  _states = _overlay.getIPCClient()->getCurrentVoiceStates();
  std::sort(_states.begin(), _states.end(),
            [](const IPCStruct::DiscordVoiceState& a,
               const IPCStruct::DiscordVoiceState& b) {
              return std::strcmp(a.name, b.name) < 0;
            });

  populateList();

  rootFrame->setContent(_list);
  return rootFrame;
}

void NXCordVoiceChannelsGui::update() {
  NXCordOverlay& overlay = _overlay;
  NXCordCustomGui::update();
  if (overlay.getCurrentGui().get() != this) {
    return;
  }

  if (Utils::check_interval(_states_lookup, 1000)) {
    if (!_overlay.getIPCClient()->isConnectedVoiceChannel()) {
      overlay.clearStack();
      overlay.changeTo<NXCordMainGui>(overlay);
      return;
    }
    std::vector<IPCStruct::DiscordVoiceState> new_states =
        _overlay.getIPCClient()->getCurrentVoiceStates();
    std::sort(new_states.begin(), new_states.end(),
              [](const IPCStruct::DiscordVoiceState& a,
                 const IPCStruct::DiscordVoiceState& b) {
                return std::strcmp(a.name, b.name) < 0;
              });

    bool changed = new_states.size() != _states.size();
    if (!changed) {
      for (size_t i = 0; i < new_states.size(); ++i) {
        if (new_states[i].userId != _states[i].userId ||
            std::strcmp(new_states[i].name, _states[i].name) != 0) {
          changed = true;
          break;
        }
      }
    }

    if (changed) {
      _states = std::move(new_states);
      populateList();
    }
  }
}
