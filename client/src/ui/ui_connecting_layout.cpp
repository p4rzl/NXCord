#include "ui_connecting_layout.hpp"

#include <common/utils.hpp>

UIConnectingLayout::UIConnectingLayout(const Interface &interface)
    : UICustomLayout(interface) {
  addChrome("Connessione a Discord", "Preparazione della sessione e dell'elenco server.",
            "B Annulla   X Annulla   + Chiudi");
  Add(pu::ui::elm::Rectangle::New(36, 220, 1208, 350, UITheme::panel, 12));
  _connecting_text = UITheme::text(76, 266, "Connessione in corso...", 30, UITheme::accent);
  Add(UITheme::text(76, 338, "Gli account con molti server richiedono piu tempo.\nNon serve premere nuovamente A.\nLa sessione restera attiva anche chiudendo questa app.", 23, UITheme::muted));

  if (!interface->isConnected()) {
    interface->startConnection();
  }
  AddThread([this]() {
    if (Utils::check_interval(_connection_looked_up, 1000) &&
        _interface->isConnected()) {
      onResultConnected();
    }
  });

  SetOnInput([this](u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
    if (Down & KEY_X) {
      _interface->stopConnection();
      onResultLoggedIn();
    }
  });

  Add(_connecting_text);
}
