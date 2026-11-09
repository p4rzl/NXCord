#pragma once

#include <common/ipc_structures.hpp>
#include <common/utils.hpp>

#include "ui_custom_layout.hpp"

using pu::ui::elm::Menu;
using pu::ui::elm::MenuItem;
using pu::ui::elm::TextBlock;

class UIChannelsLayout : public UICustomLayout {
 private:
  Menu::Ref _channels_menu;
  std::vector<IPCStruct::DiscordChannel> _channels;
  std::vector<IPCStruct::DiscordChannel> _all_channels;
  std::string _search;
  TextBlock::Ref _caption, _empty;
  bool _loaded = false;
  void rebuild();
  void updateCaption();
  time_t _channels_lookup_time = 0;

 public:
  UIChannelsLayout(const Interface &interface,
                   const IPCStruct::DiscordServer &server);

  PU_SMART_CTOR(UIChannelsLayout)
};
