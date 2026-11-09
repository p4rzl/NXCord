# NXCord

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Nintendo Switch (Atmosphere)](https://img.shields.io/badge/Platform-Nintendo%20Switch%20(Atmosphere)-e60012.svg)](https://github.com/Atmosphere-NX/Atmosphere)

**NXCord** è un client Discord non ufficiale per **Nintendo Switch** (Custom Firmware **Atmosphere**) composto da un servizio di sistema in background (`sysmodule`), un'applicazione homebrew grafica (`client` NRO) e un overlay in-game per **Ultrahand / Tesla** (`overlay` OVL).

Permette di entrare nei canali vocali Discord, parlare tramite il microfono delle cuffie e **condividere lo schermo di gioco in diretta (Screen Share H.264)** mentre si gioca a qualsiasi titolo Switch.

> **Disclaimer**: L'utilizzo di client di terze parti viola i Termini di Servizio di Discord. Usa questo software a tuo rischio. Basato originariamente sul progetto di [Grarak/NXCord](https://github.com/Grarak/NXCord) e [yourWaifu/Unofficial-Discord-3DS-Client](https://github.com/yourWaifu/Unofficial-Discord-3DS-Client).

<p align="center">
  <img src="screenshots/1.jpg" width="230"/>
  <img src="screenshots/2.jpg" width="230"/>
  <img src="screenshots/3.jpg" width="230"/>
  <img src="screenshots/4.jpg" width="230"/>
  <img src="screenshots/5.jpg" width="230"/>
</p>

---

## Caratteristiche principali

- **Servizio Background (`sysmodule`)**:
  - Gira in background su un core dedicato (`CPU Core 3`) con memoria riservata, mantenendo attiva la chiamata vocale e lo streaming anche mentre si gioca.
- **Autenticazione Discord API v9 moderna**:
  - Login con **Email e Password**, supporto completo **2FA / MFA** (**TOTP** a 6 cifre e **Backup Codes** a 8 caratteri) e login diretto tramite **Token** (da interfaccia o da `/config/nxcord/nxcord.ini`).
- **Canali Vocali (Voice Gateway v8 + DAVE v1 E2EE)**:
  - Supporto completo a **Discord Voice Gateway v8** e al protocollo di crittografia end-to-end **DAVE v1** (`libdave` + `MLS++` + `OpenSSL 3.5.9` con entropia hardware `csrng` di Switch).
  - Crittografia di trasporto `aead_xchacha20_poly1305_rtpsize` (`libsodium`).
  - Decodifica audio accelerata in hardware tramite il coprocessore **Switch `hwopus`** (`audout`) e cattura microfono tramite `audin` (cuffie/auricolari con microfono).
  - Controllo volume per singolo partecipante, amplificazione microfono (Mic Gain) e soglia di attivazione vocale (Voice Gate).
- **Condivisione Schermo In-Game (`Condivisione schermo`)**:
  - Cattura in tempo reale del gioco in esecuzione tramite il servizio di sistema `caps:sc` (cattura hardware NVJPG + decodifica/scaling DCT 1/2 tramite `libturbojpeg`, con fallback automatico allo stream raw).
  - Codifica **H.264 Constrained Baseline** (`640x360` @ ~10 FPS, `SPS + PPS + IDR` + frame predetti `P` con ottimizzazione `P_Skip`).
  - Crittografia video end-to-end **DAVE v1** (`DAVE_MEDIA_TYPE_VIDEO`, `DAVE_CODEC_H264`) e pacchettizzazione **RTP RFC 6184** (`Single NAL Unit` e frammentazione `FU-A`).
- **Overlay In-Game per Ultrahand / Tesla (`nxcord.ovl`)**:
  - Apribile in qualsiasi momento durante il gioco (`L + D-Pad Down + R3`).
  - Permette di navigare tra server e canali vocali, entrare/uscire/cambiare canale, mutare il microfono, attivare/disattivare la **Condivisione schermo** e regolare il volume degli utenti senza mai chiudere il gioco.
- **Ottimizzazione Memoria per Account Grandi**:
  - Decompressione `zlib-stream` incrementale su cache SD e parsing JSON in streaming (**SAX**) per gestire account con molti server senza saturare l'heap del sysmodule.

---

## Installazione e Utilizzo

1. Scarica l'ultimo pacchetto `release.zip` dalla sezione [Releases](https://github.com/p4rzl/NXCord/releases) (oppure compilalo con `./build_release.sh`).
2. Estrai il contenuto di `release.zip` nella **root della scheda microSD** della tua Nintendo Switch:
   ```text
   sdmc:/
   ├── atmosphere/
   │   └── contents/
   │       └── 5600000000000000/
   │           ├── exefs.nsp
   │           └── flags/
   │               └── boot2.flag
   └── switch/
       ├── nxcord.nro
       └── .overlays/
           └── nxcord.ovl
   ```
3. **Riavvia la Nintendo Switch** per avviare il `sysmodule` in background (`0x5600000000000000`).
4. Apri **NXCord** dall'Homebrew Menu (`switch/nxcord.nro`) ed effettua il login con la tua email/password (con eventuale codice 2FA) oppure inserendo il token.
5. Una volta effettuato il login, puoi entrare nei canali vocali sia dall'app NRO sia direttamente **in-game** aprendo **Ultrahand Overlay** (`L + D-Pad Down + R3` -> **NXCord**).

### Controlli nell'App NRO (`nxcord.nro`)
- **D-Pad / Stick**: Scorri server e canali (con supporto allo scorrimento rapido tenendo premuto).
- **L / R**: Pagina precedente / successiva.
- **A**: Apri il server selezionato / Entra nel canale vocale.
- **B**: Torna alla lista dei server.
- **X**: Cerca server o canale per nome.
- **Y**: Apri le **Impostazioni** (Mute microfono, **Condivisione schermo ON/OFF**, Guadagno microfono, Sensibilità Voice Gate, Disconnessione vocale, Logout).
- **- (Minus)**: Disconnettiti dal canale vocale attivo o annulla una connessione in corso.

### Utilizzo dell'Overlay Ultrahand in-game (`nxcord.ovl`)
- Premi la combinazione di apertura di Ultrahand (`L + D-Pad Down + R3`) e seleziona **NXCord**.
- Quando sei connesso a un canale vocale, la schermata principale dell'overlay mostra:
  - **Mute microphone**: Attiva/disattiva il microfono delle cuffie.
  - **Condivisione schermo**: Avvia o interrompe lo streaming video in diretta della tua partita nel canale vocale Discord.
  - **Server e canali vocali**: Cambia canale vocale o server al volo.
  - **Disconnect**: Esce dal canale vocale.
  - **Lista partecipanti**: Seleziona un utente per regolarne il volume individuale (`0% - 200%`).

### Microfono
La Nintendo Switch non integra un microfono nello chassis: per parlare nei canali vocali è necessario collegare cuffie o auricolari dotati di microfono al jack audio da 3.5mm (o adattatore compatibile supportato da `audin`).

### File di configurazione e Log
- **Configurazione e Token**: `/config/nxcord/nxcord.ini`
- **Log del Sysmodule**: `/config/nxcord/nxcord-sys.txt`
- **Log del Client NRO**: `/config/nxcord/nxcord-client.txt`
- **Log dell'Overlay**: `/config/nxcord/nxcord-ovl.txt`

---

## Compilazione dal Sorgente

### Prerequisiti
Assicurati di avere installato **devkitPro** con `devkitA64`, `libnx` e le `portlibs` per Nintendo Switch:

```bash
sudo dkp-pacman -S switch-dev switch-libsodium switch-mbedtls switch-zlib \
  switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-sdl2_gfx switch-sdl2_mixer \
  switch-mesa switch-glad switch-glm switch-libdrm_nouveau switch-libwebp \
  switch-libpng switch-freetype switch-bzip2 switch-libjpeg-turbo \
  switch-opusfile switch-libopus
```

### Build automatica (`release.zip`)
Dalla root del repository esegui:

```bash
export DEVKITPRO=/opt/devkitpro
./build_release.sh
```

Lo script compila automaticamente le dipendenze crittografiche DAVE (`OpenSSL 3.5.9`, `MLS++`, `libdave`), il `sysmodule` (`exefs.nsp`), il `client` (`nxcord.nro`) e l'`overlay` (`nxcord.ovl`), generando l'archivio pronto per SD in `release.zip` (e `build/release.zip`).

### Test di Regressione Host
Il progetto include una suite di test eseguibili su host Linux (`g++`, `python3`, `zlib`, `ffmpeg` opzionale per la validazione H.264):

```bash
python3 tests/test_gateway_stream.py
python3 tests/test_dialog_return.py
python3 tests/test_account_memory.py
python3 tests/test_websocket_cache.py
python3 tests/test_navigation.py
python3 tests/test_voice_join.py
python3 tests/test_voice_transport.py
python3 tests/test_screen_share.py
```

---

## Documentazione Tecnica
Per dettagli approfonditi sull'architettura interna, sul protocollo Voice Gateway v8 + DAVE v1, sull'encoder H.264 e sulla tabella IPC di Atmosphere, consulta [**AGENTS.md**](AGENTS.md).

## Licenza
Rilasciato sotto licenza [MIT](LICENSE). Le licenze delle librerie di terze parti sono incluse in `externals/`.
