#include "ui_servers_layout.hpp"
#include "ui_browser_model.hpp"

UIServersLayout::UIServersLayout(const Interface &interface)
    : UICustomLayout(interface) {
  addChrome("I tuoi server", "Trova il server, apri i canali e scegli dove parlare.",
            "A Apri   X Cerca   L/R Pagina   Y Impostazioni   - Esci vocale   + Chiudi");
  _server_menu = Menu::New(36, 206, 784, UITheme::panel, 56, 7);
  _server_menu->SetOnFocusColor(UITheme::selection);
  _server_menu->SetOnSelectionChanged([this]() { updateCaption(); });
  _caption = UITheme::text(36, 610, "Caricamento server...", 17, UITheme::muted);
  _empty = UITheme::text(60, 256, "", 22, UITheme::muted);
  Add(_server_menu); Add(_caption); Add(_empty);
  addVoiceCard();
  AddThread([this]() {
    if (Utils::check_interval(_server_lookup_time, 1000)) {
      auto rows = _interface->getServers();
      if (!UIBrowser::equal(rows, _all_servers)) {
        _all_servers = std::move(rows);
        rebuild();
      }
      updateCaption();
    }
  });
  SetOnInput([this](u64 down, u64, u64, pu::ui::Touch) {
    if (down & KEY_X) { _search = UITheme::search(_search); rebuild(); }
  });
}

void UIServersLayout::rebuild() {
  const int selected = _server_menu->GetSelectedIndex();
  const int64_t id = selected >= 0 && selected < static_cast<int>(_servers.size()) ? _servers[selected].id : 0;
  _servers = UIBrowser::filter(_all_servers, _search);
  _server_menu->ClearItems();
  for (const auto &server : _servers) {
    auto item = MenuItem::New(server.name);
    item->SetColor(UITheme::text_color);
    item->AddOnClick([this, server]() { onResultServerClick(server); });
    _server_menu->AddItem(item);
  }
  _server_menu->SetSelectedIndex(UIBrowser::selection(_servers, id));
  updateCaption();
}

void UIServersLayout::updateCaption() {
  UITheme::set_text(_caption, std::to_string(_servers.empty() ? 0 : _server_menu->GetSelectedIndex() + 1) +
      " / " + std::to_string(_servers.size()) + " server" +
      (_search.empty() ? "   |   Elenco alfabetico" : "   |   Filtro: " + _search));
  UITheme::set_text(_empty, _servers.empty() ?
      (_search.empty() ? "Nessun server disponibile.\nControlla la connessione Discord." :
                         "Nessun risultato.\nPremi X per modificare la ricerca.") : "");
}
