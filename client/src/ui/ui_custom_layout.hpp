#pragma once

#include <common/nxcord_com_interface.hpp>
#include <pu/Plutonium>
#include <utility>

#include "ui_result.hpp"
#include "ui_theme.hpp"
#include <common/utils.hpp>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

class UICustomLayout : public pu::ui::Layout,
                       public UIResultContainer,
                       public std::enable_shared_from_this<UICustomLayout> {
 public:
  using Interface = std::shared_ptr<NXCordComInterface>;

 protected:
  Interface _interface;
  time_t _voice_ui_lookup = 0;

  void addChrome(const std::string &title, const std::string &subtitle,
                 const std::string &controls) {
    using pu::ui::elm::Rectangle;
    Add(Rectangle::New(0, 0, SCREEN_WIDTH, 82, UITheme::panel));
    Add(UITheme::text(36, 20, "NXCord", 34, UITheme::accent));
    Add(UITheme::text(216, 33, "VOICE / NINTENDO SWITCH", 17, UITheme::muted));
    Add(UITheme::text(928, 33, "ULTRAHAND READY", 17, UITheme::success));
    Add(UITheme::text(36, 106, title, 30));
    Add(UITheme::text(36, 154, subtitle, 19, UITheme::muted));
    Add(Rectangle::New(36, 646, 1208, 1, UITheme::line));
    Add(UITheme::text(36, 670, controls, 18, UITheme::muted));
  }

  void addVoiceCard() {
    Add(pu::ui::elm::Rectangle::New(850, 206, 394, 416, UITheme::panel, 12));
    Add(UITheme::text(874, 228, "SESSIONE VOCALE", 17, UITheme::muted));
    auto state = UITheme::text(874, 267, "Non connesso", 26, UITheme::accent);
    auto channel = UITheme::text(874, 316, "Scegli un canale a sinistra", 18);
    auto detail = UITheme::text(874, 358, "", 18, UITheme::muted);
    auto members = UITheme::text(874, 472, "", 18, UITheme::success);
    Add(state); Add(channel); Add(detail); Add(members);
    Add(UITheme::text(874, 550, "Resta attivo durante il gioco.\nGestisci tutto da Ultrahand.", 17, UITheme::muted));
    AddThread([this, state, channel, detail, members,
               previous = static_cast<IPCStruct::VoiceConnectionState>(99)]() mutable {
      if (!Utils::check_interval(_voice_ui_lookup, 500)) return;
      auto status = _interface->getVoiceStatus();
      using State = IPCStruct::VoiceConnectionState;
      const char *title =
          status.state == State::Connected
              ? (status.screen_sharing ? "In vocale (Schermo ON)" : "In vocale")
          : status.state == State::Connecting ? "Connessione..."
          : status.state == State::Failed     ? "Ingresso non riuscito"
                                              : "Non connesso";
      UITheme::set_text(state, title);
      if (previous != status.state) {
        state->SetColor(status.state == State::Connected ? UITheme::success :
                        status.state == State::Failed ? UITheme::danger : UITheme::accent);
        previous = status.state;
      }
      UITheme::set_text(channel, status.channel.id ? status.channel.name : "Scegli un canale a sinistra");
      std::string message = status.detail;
      if (status.close_code) message += " (" + std::to_string(status.close_code) + ")";
      if (message.empty()) message = "A per entrare. Premi - per uscire dalla sessione vocale.";
      UITheme::set_text(detail, UITheme::wrap(message));
      UITheme::set_text(members, status.state == State::Connected ?
          "Partecipanti: " + std::to_string(_interface->getCurrentVoiceStates().size()) : "");
    });
  }

  inline void onShowLayout(const std::shared_ptr<UICustomLayout> &layout) {
    if (_listener) {
      _listener->onShowLayout(shared_from_this(), layout);
    }
  }

 public:
  explicit UICustomLayout(Interface interface)
      : _interface(std::move(interface)) { SetBackgroundColor(UITheme::background); }

  PU_SMART_CTOR(UICustomLayout)
};
