#include "nxcord_main_gui.hpp"

#include "nxcord_overlay_utils.hpp"
#include "nxcord_voice_channels_gui.hpp"
#include "nxcord_browse_gui.hpp"

NXCordMainGui::NXCordMainGui(NXCordOverlay &overlay)
    : NXCordCustomGui(overlay) {}

tsl::elm::Element *NXCordMainGui::createUI() {
  auto rootFrame = defaultOverlayFrame();
  auto list = new tsl::elm::List();
  _status = new tsl::elm::ListItem("Discord");
  list->addItem(_status);

  auto connect = new tsl::elm::ListItem("Server e canali vocali");
  connect->setClickListener([this](u64 keys) {
    if (!(keys & KEY_A)) return false;
    auto &ipc = _overlay.getIPCClient();
    if (ipc->isConnected()) {
      _overlay.changeTo<NXCordBrowseGui>(_overlay);
    } else if (ipc->tokenAvailable()) {
      _browse_when_ready = true;
      ipc->startConnection();
    } else {
      _overlay.changeTo<ErrorGui>(std::string("Log in using NXCord first"));
    }
    return true;
  });
  list->addItem(connect);

  _voice_status = new tsl::elm::ListItem("Sessione vocale");
  list->addItem(_voice_status);
  auto show_current_channel = new tsl::elm::ListItem("Dettagli / partecipanti");
  auto details = [this](u64 keys) {
    if (keys & KEY_A) {
      if (_overlay.getIPCClient()->isConnectedVoiceChannel()) {
        _overlay.changeTo<NXCordVoiceChannelsGui>(_overlay);
      } else {
        const auto voice = _overlay.getIPCClient()->getVoiceStatus();
        std::string message = voice.detail;
        if (message.empty()) message = "Scegli Server e canali vocali, poi premi A sul canale per entrare.";
        if (voice.close_code) message += " (" + std::to_string(voice.close_code) + ")";
        _overlay.changeTo<ErrorGui>(message);
      }
      return true;
    }
    return false;
  };
  show_current_channel->setClickListener(details);
  _voice_status->setClickListener(details);

  auto leave_channel = new tsl::elm::ListItem("Esci / annulla ingresso");
  leave_channel->setClickListener([this](u64 keys) {
    if (keys & KEY_A) {
      _overlay.getIPCClient()->disconnectVoiceChannel();
      return true;
    }
    return false;
  });

  list->addItem(show_current_channel);
  list->addItem(leave_channel);

  _screen_share = new tsl::elm::ListItem("Condivisione schermo");
  _screen_share->setValue("Non in vocale", true);
  _screen_share->setClickListener([this](u64 keys) {
    if (!(keys & KEY_A)) return false;
    auto &ipc = _overlay.getIPCClient();
    if (!ipc->isConnectedVoiceChannel()) {
      _overlay.changeTo<ErrorGui>(std::string(
          "Entra prima in un canale vocale per avviare la condivisione schermo."));
      return true;
    }
    const bool next_state = !ipc->isScreenSharing();
    ipc->setScreenSharing(next_state);
    const auto voice = ipc->getVoiceStatus();
    _screen_share->setValue(
        voice.screen_sharing ? "In corso (A ferma)" : "Pronta (A avvia)",
        !voice.screen_sharing);
    return true;
  });
  list->addItem(_screen_share);

  rootFrame->setContent(list);

  return rootFrame;
}

void NXCordMainGui::update() {
  NXCordOverlay &overlay = _overlay;
  NXCordCustomGui::update();
  if (overlay.getCurrentGui().get() != this) return;
  if (_status && Utils::check_interval(_status_lookup, 1000)) {
    auto &ipc = _overlay.getIPCClient();
    _status->setValue(ipc->isConnected() ? "Connesso" :
                     ipc->isConnecting() ? "Connessione..." : "Offline");
    const auto voice = ipc->getVoiceStatus();
    using State = IPCStruct::VoiceConnectionState;
    _voice_status->setValue(voice.state == State::Connected ? "In vocale" :
                           voice.state == State::Connecting ? "Ingresso..." :
                           voice.state == State::Failed ? "Errore (A dettagli)" : "Non connesso");
    if (_screen_share) {
      if (voice.state == State::Connected) {
        _screen_share->setValue(
            voice.screen_sharing ? "In corso (A ferma)" : "Pronta (A avvia)",
            !voice.screen_sharing);
      } else {
        _screen_share->setValue("Non in vocale", true);
      }
    }
    if (_browse_when_ready && ipc->isConnected()) {
      _browse_when_ready = false;
      overlay.changeTo<NXCordBrowseGui>(overlay);
    }
  }
}
