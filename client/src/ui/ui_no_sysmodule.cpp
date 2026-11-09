#include "ui_no_sysmodule.hpp"

UINoSysmodule::UINoSysmodule(const UICustomLayout::Interface& interface,
                             bool crashed)
    : UICustomLayout(interface) {
  addChrome("Sysmodule non disponibile", "NXCord richiede il servizio in background per la modalita in-game.",
            "B Chiudi   + Chiudi");
  auto text = UITheme::text(
      36, 260,
      crashed
          ? "Il servizio in background non risponde.\n"
            "Controlla /config/nxcord/nxcord-sys.txt e riavvia la console."
          : "Installa insieme NRO, sysmodule e overlay aggiornati.\n"
            "Riavvia la console e apri di nuovo NXCord.", 23, UITheme::danger);

  Add(text);
}
