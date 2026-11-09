#include "ui_logged_in.hpp"

UILoggedIn::UILoggedIn(const Interface &interface) : UICustomLayout(interface) {
  addChrome("Accesso completato", "Accesso salvato. Il sysmodule gestisce la sessione in background.",
            "A Connetti   X Esci dall'account   + Chiudi");
  Add(pu::ui::elm::Rectangle::New(36, 220, 1208, 350, UITheme::panel, 12));
  auto text = UITheme::text(76, 266, "Il tuo account e pronto.", 30);
  Add(UITheme::text(76, 330, "Premi A per caricare i server.\nScegli un canale vocale, poi torna al gioco.\nL'overlay Ultrahand rimane il tuo pannello in-game.", 23, UITheme::muted));
  Add(UITheme::text(76, 490, "DAVE/E2EE integrato: build sperimentale, da verificare sulla console.", 18, UITheme::muted));

  SetOnInput([this](u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
    if (Down & KEY_A) {
      onResultConnecting();
    }
  });

  AddThread([this]() {
    if (Utils::check_interval(_connection_looked_up, 1000)) {
      if (_interface->isConnecting()) {
        onResultConnecting();
      } else if (_interface->isConnected()) {
        onResultConnected();
      }
    }
  });

  Add(text);
}
