#pragma once

#include <vector>

#include "nxcord_custom_gui.hpp"

class NXCordVoiceChannelsGui : public NXCordCustomGui {
 private:
  time_t _states_lookup = 0;
  tsl::elm::List* _list = nullptr;
  int64_t _user_id = 0;
  std::vector<IPCStruct::DiscordVoiceState> _states;

  void populateList();

 public:
  explicit NXCordVoiceChannelsGui(NXCordOverlay& overlay);

  tsl::elm::Element* createUI() override;

  void update() override;
};
