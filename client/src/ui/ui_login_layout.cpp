#include "ui_login_layout.hpp"

UILoginLayout::UILoginLayout(const Interface &interface)
    : UICustomLayout(interface) {
  addChrome("Accedi a Discord", "Configura l'account una volta, poi usa l'app o l'overlay durante il gioco.",
            "X Email / Token   Y Password   A Accedi   + Chiudi");
  Add(pu::ui::elm::Rectangle::New(36, 206, 784, 416, UITheme::panel, 12));
  Add(pu::ui::elm::Rectangle::New(850, 206, 394, 416, UITheme::panel, 12));
  Add(UITheme::text(874, 232, "PRIMA CONFIGURAZIONE", 17, UITheme::accent));
  Add(UITheme::text(874, 286, "Email e password oppure\ntoken senza password.\n\nI controlli funzionano\nanche con i Joy-Con.\n\nUltrahand usa la sessione\ndel sysmodule in background.", 20, UITheme::muted));
  _email_edit_text =
      UIElementEditText::New(56, 226, 744, 84, "Email / Token");
  _password_edit_text =
      UIElementEditText::New(56, 326, 744, 84, "Password",
                             UIElementEditText::InputType::PASSWORD);
  _login_button = Button::New(56, 434, 240, 64, "A  Accedi", UITheme::text_color, UITheme::selection);
  _error_text = UITheme::text(56, 522, "", 18, UITheme::danger);

  auto submit = [this]() {
    _error_text->SetText("Logging in...");
    IPCStruct::LoginResult ret =
        _interface->attemptLogin(IPCStruct::create_login(
            _email_edit_text->getInput(), _password_edit_text->getInput()));
    if (ret.success) {
      onResultLoggedIn();
    } else if (ret.has2fa) {
      onResult2fa();
    } else {
      if (strlen(ret.error_message) == 0) {
        _error_text->SetText("Unknown error.");
      } else {
        _error_text->SetText(UITheme::wrap(ret.error_message, 66));
      }
    }
  };
  _login_button->SetOnClick(submit);
  SetOnInput([this, submit](u64 down, u64, u64, pu::ui::Touch) {
    if (down & KEY_X) _email_edit_text->edit();
    else if (down & KEY_Y) _password_edit_text->edit();
    else if (down & KEY_A) submit();
  });

  AddThread([this]() {
    if (Utils::check_interval(_token_looked_up, 1000) &&
        _interface->tokenAvailable()) {
      onResultLoggedIn();
    }
  });

  Add(_email_edit_text);
  Add(_password_edit_text);
  Add(_login_button);
  Add(_error_text);
}
