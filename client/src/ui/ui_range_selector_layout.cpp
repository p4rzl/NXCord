#include "ui_range_selector_layout.hpp"

#include <common/utils.hpp>
#include <utility>

UIRangeSelectorLayout::UIRangeSelectorLayout(const Interface &interface,
                                             pu::String title, int min, int max,
                                             size_t selected)
    : UICustomLayout(interface), _title(std::move(title)), _selected(selected) {
  NXC_ASSERT(min < max);

  for (int i = min; i <= max; ++i) {
    _items.push_back(std::to_string(i));
  }
  init();
}

UIRangeSelectorLayout::UIRangeSelectorLayout(const Interface &interface,
                                             pu::String title,
                                             std::vector<std::string> items,
                                             size_t selected)
    : UICustomLayout(interface),
      _title(std::move(title)),
      _items(std::move(items)),
      _selected(selected) {
  init();
}

void UIRangeSelectorLayout::init() {
  if (_items.empty()) _items.push_back("-");
  _selected = std::min(_selected, _items.size() - 1);
  addChrome(_title.AsUTF8(), "La modifica viene applicata subito.",
            "Sinistra / Destra Regola   B Torna alle impostazioni   + Chiudi");
  Add(pu::ui::elm::Rectangle::New(36, 206, 1208, 416, UITheme::panel, 12));

  int width_sum = 240;
  auto selected_text = UITheme::text(0, 0, _items[_selected], 48, UITheme::accent);
  width_sum += selected_text->GetTextWidth();
  selected_text->SetX(SCREEN_WIDTH / 2 - selected_text->GetTextWidth() / 2);
  selected_text->SetY(SCREEN_HEIGHT / 2 - selected_text->GetTextHeight() / 2);

  auto decrease_btn = Button::New(
      SCREEN_WIDTH / 2 - width_sum / 2, SCREEN_HEIGHT / 2 - 40, 100, 80, "<",
      UITheme::text_color, UITheme::selection);
  auto increase_btn = Button::New(SCREEN_WIDTH / 2 + width_sum / 2 - 100,
                                  SCREEN_HEIGHT / 2 - 40, 100, 80, ">",
                                  UITheme::text_color, UITheme::selection);

  auto decrease_fun = [this, selected_text]() {
    if (_selected > 0) {
      selected_text->SetText(_items[--_selected]);
      selected_text->SetX(SCREEN_WIDTH / 2 - selected_text->GetTextWidth() / 2);
      if (_selected_listener) {
        _selected_listener(_items[_selected], _selected);
      }
    }
  };

  auto increase_fun = [this, selected_text]() {
    if (_selected + 1 < _items.size()) {
      selected_text->SetText(_items[++_selected]);
      selected_text->SetX(SCREEN_WIDTH / 2 - selected_text->GetTextWidth() / 2);
      if (_selected_listener) {
        _selected_listener(_items[_selected], _selected);
      }
    }
  };

  decrease_btn->SetOnClick(decrease_fun);
  increase_btn->SetOnClick(increase_fun);

  SetOnInput([decrease_fun, increase_fun](u64 Down, u64 Up, u64 Held,
                                          pu::ui::Touch Pos) {
    if (Down & KEY_DLEFT || Down & KEY_LSTICK_LEFT || Down & KEY_RSTICK_LEFT) {
      decrease_fun();
    } else if (Down & KEY_DRIGHT || Down & KEY_LSTICK_RIGHT ||
               Down & KEY_RSTICK_RIGHT) {
      increase_fun();
    }
  });

  Add(decrease_btn);
  Add(selected_text);
  Add(increase_btn);
}
