#include "nxcord_overlay.hpp"

#include "nxcord_main_gui.hpp"

void NXCordOverlay::initServices() {
  _ipc_client = std::make_unique<IPCClient>();
}

void NXCordOverlay::exitServices() {
  _ipc_client.reset();
}

std::unique_ptr<tsl::Gui> NXCordOverlay::loadInitialGui() {
  return initially<NXCordMainGui>(*this);
}
