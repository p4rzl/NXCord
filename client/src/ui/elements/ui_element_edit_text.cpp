#include "ui_element_edit_text.hpp"

#include <cstring>
#include "../ui_theme.hpp"

UIElementEditText::UIElementEditText(s32 x, s32 y, s32 w, s32 h,
                                     const pu::String &title,
                                     InputType inputType)
    : _outer_rec(
          Rectangle::New(x, y, w, h, UITheme::line, 8)),
      _inner_title_text(UITheme::text(x, y, pu::String(title).AsUTF8(), 17, UITheme::muted)),
      _inner_input_text(UITheme::text(x, y + h / 3, "Tocca per modificare", 22)),
      _x(x),
      _y(y),
      _w(w),
      _h(h),
      _inputType(inputType) {}

void UIElementEditText::OnRender(pu::ui::render::Renderer::Ref &Drawer, s32 X,
                                 s32 Y) {
  _outer_rec->OnRender(Drawer, X, Y);
  _inner_title_text->OnRender(Drawer, X + 16, Y + 8);
  _inner_input_text->OnRender(Drawer, X + 16, Y + 36);
}

void UIElementEditText::OnInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
  _outer_rec->OnInput(Down, Up, Held, Pos);
  _inner_title_text->OnInput(Down, Up, Held, Pos);
  _inner_input_text->OnInput(Down, Up, Held, Pos);

  if (!Pos.IsEmpty() && Pos.X > _x && Pos.X < _x + _w && Pos.Y > _y &&
      Pos.Y < _y + _h) {
    edit();
  }
}

void UIElementEditText::edit() {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
      return;
    }

    char output[0x100] = {0};

    switch (_inputType) {
      case InputType::NORMAL:
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetReturnButtonFlag(&kbd, 0);
        swkbdConfigSetDicFlag(&kbd, 0);
        break;
      case InputType::USERNAME:
        swkbdConfigMakePresetUserName(&kbd);
        swkbdConfigSetReturnButtonFlag(&kbd, 0);
        swkbdConfigSetDicFlag(&kbd, 0);
        break;
      case InputType::PASSWORD:
        swkbdConfigMakePresetPassword(&kbd);
        break;
    }
    swkbdConfigSetInitialText(&kbd, _input.c_str());

    rc = swkbdShow(&kbd, output, sizeof(output));

    if (R_SUCCEEDED(rc)) {
      size_t out_len = strlen(output);
      while (out_len > 0 &&
             (output[out_len - 1] == '\r' || output[out_len - 1] == '\n')) {
        output[--out_len] = '\0';
      }
      _input = std::string(output);
      if (_inputType == InputType::PASSWORD) {
        if (strlen(output) > 0) {
          std::memset(output, '*', 6);
          output[6] = '\0';
        } else {
          output[0] = '\0';
        }
      }
      _inner_input_text->SetText(_inputType != InputType::PASSWORD &&
          _input.size() > 40 && _input.find('@') == std::string::npos ? "Token inserito" : output);
    }
    swkbdClose(&kbd);
}
