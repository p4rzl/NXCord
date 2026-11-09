#include "nxcord_custom_gui.hpp"

#include "error_gui.hpp"
#include "nxcord_overlay_utils.hpp"

NXCordCustomGui::NXCordCustomGui(NXCordOverlay& overlay) : _overlay(overlay) {}

void NXCordCustomGui::update() {
  if (Utils::check_interval(_ping_lookup, 1000)) {
    NXCordOverlay& overlay = _overlay;
    if (!overlay.getIPCClient()->ping()) {
      overlay.clearStack();
      overlay.changeTo<ErrorGui>(std::string(NO_SYSMODULE));
    }
  }
}
