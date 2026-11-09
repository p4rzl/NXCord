#pragma once

#include "nxcord_custom_gui.hpp"

class NXCordMainGui : public NXCordCustomGui {
 private:
  tsl::elm::ListItem *_status = nullptr;
  tsl::elm::ListItem *_voice_status = nullptr;
  tsl::elm::ListItem *_screen_share = nullptr;
  bool _browse_when_ready = false;
  time_t _status_lookup = 0;

 public:
  explicit NXCordMainGui(NXCordOverlay &overlay);

  tsl::elm::Element *createUI() override;
  void update() override;
};
