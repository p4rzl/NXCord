#include "error_gui.hpp"
#include <sstream>

ErrorGui::ErrorGui(std::string error) {
  std::istringstream words(error);
  std::string word;
  size_t column = 0;
  while (words >> word) {
    if (column && column + word.size() + 1 > 27) { _error += '\n'; column = 0; }
    if (column) { _error += ' '; ++column; }
    _error += word;
    column += word.size();
  }
}

tsl::elm::Element *ErrorGui::createUI() {
  auto rootFrame = defaultOverlayFrame();

  rootFrame->setContent(new tsl::elm::CustomDrawer(
      [this](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->drawString(_error.c_str(), false, x + 12, y + 34, 22,
                             tsl::gfx::Color(0xff, 0xff, 0xff, 0xff));
      }));

  return rootFrame;
}
