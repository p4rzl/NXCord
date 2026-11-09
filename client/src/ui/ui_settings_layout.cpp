#include "ui_settings_layout.hpp"

#include "ui_microphone_sensitivity_layout.hpp"
#include "ui_range_selector_layout.hpp"
#include "ui_browser_model.hpp"

UISettingsLayout::UISettingsLayout(const UICustomLayout::Interface& interface)
    : UICustomLayout(interface) {
  addChrome("Impostazioni audio", "Regola microfono e ascolto senza perdere la posizione nel tuo server.",
            "A Modifica   B Torna alla schermata precedente   - Esci vocale   + Chiudi");
  auto menu = Menu::New(36, 206, 784, UITheme::panel, 72, 5);
  menu->SetOnFocusColor(UITheme::selection);
  addVoiceCard();

  auto microphone_amplifier = MenuItem::New("Amplificazione microfono");
  microphone_amplifier->SetColor(UITheme::text_color);
  microphone_amplifier->AddOnClick([this]() {
    std::vector<std::string> items;
    for (int i = 1; i <= 49; ++i) {
      items.push_back(
          std::to_string(0.2f * static_cast<float>(i)).substr(0, 3) + "x");
    }

    auto selector = UIRangeSelectorLayout::New(
        _interface, "Microphone amplifier", items,
        UIBrowser::range_index(_interface->getMicrophoneAmplifier(), 0.2f, items.size()));
    selector->setOnSelectedListener(
        [this](const std::string& value, size_t selected) {
          _interface->setMicrophoneAmplifier((selected + 1.0f) * 0.2f);
        });
    onShowLayout(selector);
  });
  menu->AddItem(microphone_amplifier);

  auto global_audio_volume = MenuItem::New("Volume in cuffia");
  global_audio_volume->SetColor(UITheme::text_color);
  global_audio_volume->AddOnClick([this]() {
    std::vector<std::string> items;
    for (int i = 1; i <= 20; ++i) {
      items.push_back(
          std::to_string(0.1f * static_cast<float>(i)).substr(0, 3) + "x");
    }

    auto selector = UIRangeSelectorLayout::New(
        _interface, "Global audio volume", items,
        UIBrowser::range_index(_interface->getGlobalAudioVolume(), 0.1f, items.size()));
    selector->setOnSelectedListener(
        [this](const std::string& value, size_t selected) {
          _interface->setGlobalAudioVolume((selected + 1.0f) * 0.1f);
        });
    onShowLayout(selector);
  });
  menu->AddItem(global_audio_volume);

  auto microphone_sensitivity = MenuItem::New("Soglia di attivazione microfono");
  microphone_sensitivity->SetColor(UITheme::text_color);
  microphone_sensitivity->AddOnClick([this]() {
    auto microphone_sensitivity_layout =
        UIMicrophoneSensitivityLayout::New(_interface);
    onShowLayout(microphone_sensitivity_layout);
  });
  menu->AddItem(microphone_sensitivity);

  auto screen_share = MenuItem::New("Condivisione schermo (attiva / ferma)");
  screen_share->SetColor(UITheme::text_color);
  screen_share->AddOnClick([this]() {
    if (_interface->isConnectedVoiceChannel()) {
      _interface->setScreenSharing(!_interface->isScreenSharing());
    }
  });
  menu->AddItem(screen_share);

  auto leave = MenuItem::New("Esci dal canale vocale");
  leave->SetColor(UITheme::text_color);
  leave->AddOnClick([this]() { _interface->disconnectVoiceChannel(); });
  menu->AddItem(leave);
  auto disconnect = MenuItem::New("Disconnetti Discord");
  disconnect->SetColor(UITheme::danger);
  disconnect->AddOnClick([this]() { _interface->stopConnection(); onResultLoggedIn(); });
  menu->AddItem(disconnect);
  Add(menu);
}
