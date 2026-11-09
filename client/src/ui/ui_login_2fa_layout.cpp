#include "ui_login_2fa_layout.hpp"

#include <cstring>

UILogin2faLayout::UILogin2faLayout(const Interface &interface)
    : UICustomLayout(interface) {
  addChrome("Verifica in due passaggi", "Inserisci il codice dell'app Authenticator o un codice di recupero.",
            "X Modifica codice   A Conferma   B Torna all'accesso   + Chiudi");
  Add(pu::ui::elm::Rectangle::New(36, 206, 1208, 416, UITheme::panel, 12));
  _code_edit_text = UIElementEditText::New(
      60, 242, 720, 84, "Codice TOTP a 6 cifre / Recupero a 8 caratteri");
  _submit_button = Button::New(60, 370, 240, 64, "A  Conferma", UITheme::text_color, UITheme::selection);
  _error_text = UITheme::text(60, 470, "", 20, UITheme::danger);

  auto submit = [this]() {
    _error_text->SetText("");
    std::string code = _code_edit_text->getInput();
    if (code.empty()) {
      _error_text->SetText("Code can't be empty.");
      return;
    }

    IPCStruct::LoginResult ret = _interface->submit2faTicket(code);
    if (ret.success) {
      onResultLoggedIn();
    } else {
      if (strlen(ret.error_message) == 0) {
        _error_text->SetText("Code is not correct.");
      } else {
        _error_text->SetText(UITheme::wrap(ret.error_message, 80));
      }
    }
  };
  _submit_button->SetOnClick(submit);
  SetOnInput([this, submit](u64 down, u64, u64, pu::ui::Touch) {
    if (down & KEY_X) _code_edit_text->edit();
    else if (down & KEY_A) submit();
  });

  Add(_code_edit_text);
  Add(_submit_button);
  Add(_error_text);
}
