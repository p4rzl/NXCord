#include "ui_channels_layout.hpp"

#include <algorithm>
#include "ui_browser_model.hpp"

UIChannelsLayout::UIChannelsLayout(const Interface &interface,
                                   const IPCStruct::DiscordServer &server)
    : UICustomLayout(interface) {
  addChrome(server.name, "Canali vocali  /  Premi A sul canale per inviare la richiesta di ingresso.",
            "A Entra   B Server   X Cerca   L/R Pagina   Y Impostazioni   - Esci vocale");
  _channels_menu = Menu::New(36, 206, 784, UITheme::panel, 56, 7);
  _channels_menu->SetOnFocusColor(UITheme::selection);
  _channels_menu->SetOnSelectionChanged([this]() { updateCaption(); });
  _caption = UITheme::text(36, 610, "Caricamento canali...", 17, UITheme::muted);
  _empty = UITheme::text(60, 256, "Caricamento canali vocali...", 22, UITheme::muted);
  Add(_channels_menu); Add(_caption); Add(_empty);
  addVoiceCard();

  AddThread([this, server]() {
    if (Utils::check_interval(_channels_lookup_time, 1000)) {
      std::vector<IPCStruct::DiscordChannel> new_channels =
          _interface->getChannels(server.id);
      new_channels.erase(
          std::remove_if(new_channels.begin(), new_channels.end(),
                         [](const IPCStruct::DiscordChannel &channel) {
                           return channel.type !=
                                  IPCStruct::DiscordChannelType::SERVER_VOICE;
                         }),
          new_channels.end());

      if (!_loaded || !UIBrowser::equal(new_channels, _all_channels)) {
        _loaded = true;
        _all_channels = std::move(new_channels);
        rebuild();
      }
    }
  });

  SetOnInput([this](u64 down, u64, u64, pu::ui::Touch) {
    if (down & KEY_X) { _search = UITheme::search(_search); rebuild(); }
  });
}

void UIChannelsLayout::rebuild() {
  const int selected = _channels_menu->GetSelectedIndex();
  const int64_t id = selected >= 0 && selected < static_cast<int>(_channels.size()) ? _channels[selected].id : 0;
  _channels = UIBrowser::filter(_all_channels, _search);
  _channels_menu->ClearItems();
  for (const auto &channel : _channels) {
    auto item = MenuItem::New(std::string("  >  ") + channel.name);
    item->SetColor(UITheme::text_color);
    item->AddOnClick([this, channel]() {
      _interface->joinVoiceChannel(channel.serverId, channel.id);
    });
    _channels_menu->AddItem(item);
  }
  _channels_menu->SetSelectedIndex(UIBrowser::selection(_channels, id));
  updateCaption();
}

void UIChannelsLayout::updateCaption() {
  UITheme::set_text(_caption, std::to_string(_channels.empty() ? 0 : _channels_menu->GetSelectedIndex() + 1) +
      " / " + std::to_string(_channels.size()) + " canali vocali" +
      (_search.empty() ? "" : "   |   Filtro: " + _search));
  UITheme::set_text(_empty, _channels.empty() ?
      (_search.empty() ? "Nessun canale vocale accessibile.\nVerifica i permessi del tuo account.\nB per tornare ai server." :
                         "Nessun risultato.\nPremi X per modificare la ricerca.") : "");
}
