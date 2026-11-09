#pragma once

#include "nxcord_custom_gui.hpp"

// Browsing only uses IPC; the Discord session stays in the sysmodule.
class NXCordBrowseGui : public NXCordCustomGui {
 private:
  int64_t _server_id;

 public:
  explicit NXCordBrowseGui(NXCordOverlay &overlay, int64_t server_id = 0);
  tsl::elm::Element *createUI() override;
};
