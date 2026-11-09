#pragma once

#include <pu/Plutonium>
#include <sstream>

namespace UITheme {
inline const pu::ui::Color background(16, 19, 30, 255);
inline const pu::ui::Color panel(27, 33, 49, 255);
inline const pu::ui::Color line(45, 55, 78, 255);
inline const pu::ui::Color text_color(235, 240, 255, 255);
inline const pu::ui::Color muted(153, 169, 196, 255);
inline const pu::ui::Color accent(102, 162, 255, 255);
inline const pu::ui::Color selection(49, 75, 132, 255);
inline const pu::ui::Color success(84, 218, 165, 255);
inline const pu::ui::Color danger(255, 134, 145, 255);

inline pu::ui::elm::TextBlock::Ref text(int x, int y, const std::string &value,
    int size = 22, pu::ui::Color color = text_color) {
  auto label = pu::ui::elm::TextBlock::New(x, y, value, size);
  label->SetColor(color);
  return label;
}

inline void set_text(const pu::ui::elm::TextBlock::Ref &label, const std::string &value) {
  if (label->GetText().AsUTF8() != value) label->SetText(value);
}

inline std::string wrap(const std::string &value, size_t width = 31) {
  std::istringstream words(value);
  std::string result, word;
  size_t column = 0;
  while (words >> word) {
    if (column && column + 1 + word.size() > width) { result += '\n'; column = 0; }
    if (column) { result += ' '; ++column; }
    result += word;
    column += word.size();
  }
  return result;
}

inline std::string search(const std::string &current) {
  SwkbdConfig keyboard;
  if (R_FAILED(swkbdCreate(&keyboard, 0))) return current;
  swkbdConfigMakePresetDefault(&keyboard);
  swkbdConfigSetInitialText(&keyboard, current.c_str());
  swkbdConfigSetHeaderText(&keyboard, "Cerca per nome (vuoto = tutti)");
  char value[96]{};
  const Result result = swkbdShow(&keyboard, value, sizeof(value));
  swkbdClose(&keyboard);
  return R_SUCCEEDED(result) ? std::string(value) : current;
}
}  // namespace UITheme
