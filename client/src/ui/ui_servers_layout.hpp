#pragma once

#include <common/utils.hpp>

#include "ui_custom_layout.hpp"

using pu::ui::elm::Menu;
using pu::ui::elm::MenuItem;

class UIServersLayout : public UICustomLayout {
 private:
  Menu::Ref _server_menu;
  std::vector<IPCStruct::DiscordServer> _servers;
  std::vector<IPCStruct::DiscordServer> _all_servers;
  std::string _search;
  pu::ui::elm::TextBlock::Ref _caption, _empty;
  void rebuild();
  void updateCaption();
  time_t _server_lookup_time = 0;

 public:
  explicit UIServersLayout(const Interface &interface);

  PU_SMART_CTOR(UIServersLayout)
};
