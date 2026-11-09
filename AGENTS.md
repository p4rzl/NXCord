# AGENTS.md — Documentazione Tecnica per Agenti AI e Sviluppatori (NXCord)

Benvenuto nella documentazione tecnica e architetturale di **NXCord**, il client Discord per **Nintendo Switch** con Custom Firmware (**Atmosphere**).

Questo documento fornisce una panoramica completa dell'architettura multi-processo, dei protocolli Discord moderni implementati (Gateway v9, Voice Gateway v8, crittografia End-to-End **DAVE v1** audio e video, codifica **H.264** e streaming **RTP RFC 6184**), della tabella IPC di Atmosphere, dei problemi ingegneristici risolti e della suite di test di regressione.

---

## 1. Architettura del Progetto

NXCord è strutturato in componenti cooperanti che comunicano tramite il sottosistema IPC di Atmosphere (`Stratosphere` / `nx:cord`):

```text
NXCord/
├── sysmodule/       # Background Service Atmosphere (TitleID: 0x5600000000000000)
├── client/          # Applicazione Homebrew NRO con GUI (Plutonium / SDL2)
├── overlay/         # Overlay in-game (.ovl per Ultrahand / Tesla / libtesla)
├── nxcord/          # Core Discord, Voice Gateway v8, DAVE v1 E2EE, Audio e Screen Share H.264
├── common/          # Contratto IPC, strutture condivise, LoopThread e Logger SD
├── include/         # Header pubblici di common/, nxcord/ e switch_compat/
├── externals/       # Dipendenze integrate (sleepy-discord, Atmosphere-libs, Plutonium, libtesla, wslay, dave)
└── tests/           # Suite di test di regressione host-side (Python + C++)
```

### 1.1 `sysmodule/` (Background Service Atmosphere)
- **Target**: `sysmodule.nsp` -> distribuito su SD in `atmosphere/contents/5600000000000000/exefs.nsp` con `flags/boot2.flag`.
- **Risorse e Scheduling**:
  - Eseguito in background sul core dedicato `default_cpu_id: 3` (priorità thread `44`, stack main thread `64 KiB`, stack thread IPC `128 KiB`, stack thread UDP Voice `128 KiB`).
  - Heap fisso riservato: `INNER_HEAP_SIZE = 0xC00000` (`12 MiB` in `sysmodule/src/main.cpp`).
- **Servizi di sistema Switch utilizzati**:
  - `bsd:s` (`socketInitialize` con buffer TCP/UDP maggiorati per supportare Gateway + RTP Audio + RTP Video H.264).
  - `csrng` (`csrngInitialize` per fornire entropia hardware crittografica a OpenSSL / MLS++ / DAVE).
  - `audin:u` e `audout:u` (cattura microfono cuffie a 48 kHz mono/stereo e riproduzione audio).
  - `hwopus` (decodifica hardware dei pacchetti Opus in arrivo tramite il DSP della Switch).
  - `caps:sc` (inizializzato *on-demand* da `NXCordScreenShare` solo quando la condivisione schermo è attiva, per catturare il framebuffer del gioco in esecuzione).
- **Server IPC**:
  - Registra il servizio `"nx:cord"` (`max_sessions = 4`) tramite `ams::sf::hipc::ServerManager`.

### 1.2 `client/` (Frontend Homebrew NRO)
- **Target**: `NXCord.nro` -> distribuito su SD in `switch/nxcord.nro`.
- **Framework UI**: `Plutonium` (basato su SDL2 / OpenGL).
- **Funzioni**:
  - Login iniziale: Email/Password, verifica 2FA/MFA (**TOTP** a 6 cifre e **Backup Codes** a 8 caratteri) e inserimento diretto del Token.
  - Browser di Server e Canali vocali con ricerca testuale (**X**), paginazione rapida (**L/R**), pannello persistente dello stato vocale e controlli Joy-Con/Pro Controller.
  - Schermata Impostazioni (**Y**): Mute microfono, toggle **Condivisione schermo (`ON/OFF`)**, Guadagno microfono (`1x - 10x`), Soglia di attivazione vocale (Voice Gate), Disconnessione vocale e Logout.

### 1.3 `overlay/` (Ultrahand / Tesla In-Game Overlay)
- **Target**: `overlay.ovl` -> distribuito su SD in `switch/.overlays/nxcord.ovl`.
- **Framework UI**: `libtesla` (compatibile con **Ultrahand Overlay** e Tesla Menu).
- **Funzioni**:
  - Accessibile in qualsiasi momento durante il gioco (`L + D-Pad Down + R3`).
  - Consente di connettersi usando il login salvato, sfogliare server e canali vocali (`NXCordBrowseGui` / `NXCordVoiceChannelsGui`), entrare/uscire/cambiare canale vocale, attivare/disattivare il microfono, **avviare o fermare la Condivisione schermo in diretta**, visualizzare i partecipanti nel canale vocale e regolarne il volume individuale (`0% - 200%`).

### 1.4 Tabella Comandi IPC (`nx:cord`)
Definita in `include/common/nxcord_com_interface.hpp` e implementata in `sysmodule/src/ipc_server.hpp` / `common/src/ipc_client.cpp`:

| ID | Comando | Descrizione |
|---:|---|---|
| `0` | `GetAudioPlayerCount` | Numero di flussi audio utente attivi |
| `1` | `IsConnected` | Stato connessione al Main Gateway Discord |
| `2` | `GetServersSize` | Dimensione buffer IPC per la lista server |
| `3` | `GetServers` | Lista dei server (`IPCServer`, max 200) |
| `4` | `GetChannelsSize` | Dimensione buffer IPC per i canali vocali di un server |
| `5` | `GetChannels` | Lista dei canali vocali (`IPCChannel`, max 500) |
| `6` | `SelectVoiceChannel` | Richiede ingresso/uscita da un canale vocale |
| `7` | `IsVoiceConnected` | Indica se la sessione audio vocale è pronta |
| `8` | `GetVoiceParticipantsSize` | Dimensione buffer IPC per i partecipanti vocali |
| `9` | `GetVoiceParticipants` | Lista partecipanti (`IPCVoiceParticipant`) |
| `10` | `SetMute` | Attiva/disattiva mute microfono |
| `11` | `IsMuted` | Stato mute microfono |
| `12` | `SetToken` | Imposta e salva il token Discord in `/config/nxcord/nxcord.ini` |
| `13` | `HasToken` | Verifica se esiste un token salvato |
| `14` | `StartConnection` | Avvia la connessione al Main Gateway |
| `15` | `Login` | Esegue `POST /api/v9/auth/login` |
| `16` | `Submit2FA` | Esegue verifica MFA (`/mfa/totp`, `/mfa/backup`, `/mfa/sms`) |
| `17` | `SetVolume` | Imposta il volume (`0..200%`) di un partecipante vocale |
| `18` | `Logout` | Disconnette e rimuove il token salvato |
| `19` | `GetVoiceChannelId` | Restituisce il canale vocale attivo |
| `20` | `GetVoiceServerId` | Restituisce il server vocale attivo |
| `21` | `StartCapturingVolume` | Avvia monitoraggio livello microfono per calibrazione |
| `22` | `StopCapturingVolume` | Ferma monitoraggio livello microfono |
| `23` | `GetCaptureVolume` | Legge livello corrente e soglia Voice Gate |
| `24` | `SetCaptureSensitivity` | Imposta soglia Voice Gate (`-40..30 dB`) |
| `25` | `SetMicrophoneGain` | Imposta guadagno microfono (`1..10`) |
| `26` | `GetMicrophoneGain` | Legge guadagno microfono |
| `27` | `GetVoiceStatus` | Stato dettagliato connessione vocale (`VoiceStatus`: connecting, connected, failed, closeCode) |
| `28` | `SetScreenShareEnabled` | Avvia o arresta la condivisione schermo H.264 in-game |
| `29` | `IsScreenShareEnabled` | Verifica se la condivisione schermo è attiva |

---

## 2. Compilazione e Dipendenze

### 2.1 Script di Build (`./build_release.sh`)
Lo script `./build_release.sh`:
1. Configura e compila le dipendenze crittografiche statiche per DAVE v1 in `externals/dave/` (`OpenSSL 3.5.9` per AArch64 bare-metal/libnx, `nlohmann/json`, `MLS++` e `discord/libdave`), verificandone gli hash SHA-256.
2. Configura e compila `sysmodule`, `client` e `overlay` tramite `DevkitA64Libnx.cmake` (o `tools/cmake-shim.py` se la cache è già presente).
3. Assembla `release.zip` e `build/release.zip` includendo le licenze di terze parti.

---

## 3. Protocolli Discord e Stack Multimediale

### 3.1 Main Gateway v9 e Salvaguardia Memoria
- **Endpoint**: `wss://gateway.discord.gg/?v=9&encoding=json&compress=zlib-stream`
- **Streaming Zlib su SD**:
  - I frame compressi del Gateway vengono decompressi incrementalmente tramite `zlib_wrapper` e riversati su una cache temporanea su SD (`8 MiB` limite compresso) senza allocare l'intero messaggio in RAM in `wslay`.
- **Parsing JSON SAX (`include/nxcord/discord_list_parser.hpp` & `gateway_events.hpp`)**:
  - L'evento `READY` e gli eventi `GUILD_CREATE` / `GUILD_UPDATE` vengono filtrati con un parser **RapidJSON SAX** che estrae esclusivamente ID server, nome, canali vocali e `voice_states`, ignorando ruoli, emoji, presenze, messaggi e blob protobuf (`user_settings_proto`).
  - I token stringa SAX sono limitati a `8 KiB` per evitare che campi giganti inutilizzati saturino il buffer del parser.
  - Le liste canali vengono caricate via REST (`GET /api/v9/guilds/{id}/channels`) solo *on-demand* quando l'utente apre un server (cache LRU di 2 server, con il server vocale attivo protetto da espulsione).

### 3.2 Autenticazione e 2FA / MFA (API v9)
- **Endpoint**:
  - `POST https://discord.com/api/v9/auth/login`
  - `POST https://discord.com/api/v9/auth/mfa/totp`
  - `POST https://discord.com/api/v9/auth/mfa/backup`
- **Dettagli critici**:
  - Invio di `X-Super-Properties` (base64 JSON con metadati client desktop aggiornati), `X-Discord-Locale: en-US`, `Origin: https://discord.com` e `Referer: https://discord.com/login`.
  - Conservazione dei cookie `Set-Cookie` ricevuti da `/auth/login` e reinvio automatico nell'header `Cookie` della chiamata `/auth/mfa/*`.
  - Inoltro di `login_instance_id` nel body JSON della richiesta MFA.
  - Sanitizzazione del codice 2FA inserito dalla tastiera software Switch (`swkbd`), rimuovendo spazi, trattini e newline prima dell'invio.

### 3.3 Voice Gateway v8, DAVE v1 Audio E2EE e Gestione Buffer Audio
1. **Connessione Voice Gateway v8**:
   - Si collega a `wss://<endpoint>/?v=8`.
   - `IDENTIFY` (`op: 0`) invia `server_id`, `user_id`, `session_id`, `token`, `"video": true`, `"streams": [{"type":"video","rid":"100","quality":100}]` e `"max_dave_protocol_version": 1`.
   - I messaggi `HEARTBEAT` (`op: 3`) e `RESUME` (`op: 7`) includono `seq_ack` con l'ultimo numero di sequenza ricevuto dal server (`d.seq` per JSON o header `uint16 BE` per i frame binari MLS).
2. **UDP IP Discovery e `SELECT_PROTOCOL` (`op: 1`)**:
   - Alla ricezione di `READY` (`op: 2`), il client salva `ssrc` (audio) e `videoSsrc` (dal blocco `streams`), e avvia il pacchetto UDP IP discovery da **74 byte** (`type = 0x0001`, `length = 70`, `ssrc` big-endian).
   - Quando il thread UDP riceve la risposta di discovery, accoda il messaggio `SELECT_PROTOCOL` (`op: 1`) che viene inviato **sul thread principale del WebSocket** tramite `flushPendingUDP()` (evitando race condition sulle code `wslay`), negoziando:
     - `mode`: `"aead_xchacha20_poly1305_rtpsize"`
     - `codecs`: `opus` (`payload_type: 120`) e `H264` (`payload_type: 101`, `rtx_payload_type: 102`).
3. **Handshake DAVE v1 (MLS End-to-End Encryption)**:
   - Implementato in `nxcord/src/dave_session.cpp` e `nxcord/src/dave_crypto.cpp`.
   - Gestisce `SESSION_DESCRIPTION` (`op: 4`, `dave_protocol_version`), `DAVE_PROTOCOL_PREPARE_TRANSITION` (`op: 21`), `DAVE_PROTOCOL_EXECUTE_TRANSITION` (`op: 22`), `DAVE_PROTOCOL_PREPARE_EPOCH` (`op: 24`), e i frame binari WebSocket (`op: 25` External Sender Package, `op: 27` Proposals, `op: 29` Announce Commit Transition, `op: 30` Welcome), rispondendo con `op: 26` (`KEY_PACKAGE`), `op: 28` (`COMMIT_WELCOME`) e `op: 23` (`TRANSITION_READY`).
   - **Regola anti-4006**: Nessun frame audio o video viene trasmesso finché `SESSION_DESCRIPTION` non è stato ricevuto e, se `dave_protocol_version > 0`, finché `DaveSession::canSendAudio()` non conferma che il key ratchet MLS è attivo.
4. **Ciclo di vita `audin` / `audout` (Fix crash su Rejoin)**:
   - Su Nintendo Switch (`libnx`), `audinStopAudioIn()` e `audoutStopAudioOut()` rilasciano tutti i buffer pendenti nel driver. Se si richiama `audinAppendAudioInBuffer` / `audoutAppendAudioOutBuffer` al successivo ingresso in vocale senza aver prima svuotato la coda dei buffer rilasciati con `audinGetReleasedAudioInBuffer` / `audoutGetReleasedAudioOutBuffer`, il kernel restituisce `0x101` e causa il crash del sysmodule.
   - `NXCordAudioCapture::stop()` e `NXCordAudioPlayer::stop()` eseguono sempre il drain completo dei buffer rilasciati. Inoltre, `removeVoiceConnectionAndContext` invalida sempre `voiceContext.token` ed `endpoint` per forzare l'attesa di credenziali fresche da Discord al rejoin.

### 3.4 Condivisione Schermo In-Game (`640x360` H.264 + DAVE Video + RTP RFC 6184)
1. **Cattura Schermo (`NXCordScreenShare` in `nxcord/src/nxcord_screen_share.cpp`)**:
   - Inizializza `caps:sc` solo quando l'utente attiva la condivisione schermo (`capsscInitialize()`) e lo rilascia subito allo stop (`capsscExit()`), liberando tutti i buffer (`releaseBuffers()`).
   - Cattura a ~10 FPS (`100 ms` per frame) tramite l'encoder hardware NVJPG (`capsscCaptureJpegScreenShot`) e decodifica direttamente in piani YUV `640x360` usando `libturbojpeg` (`tjDecompressToYUVPlanes` con fattore di scala DCT 1/2 da `1280x720` a `640x360`, convertendo automaticamente eventuale chroma `4:2:2` in `4:2:0`).
   - Se `capsscCaptureJpegScreenShot` non è disponibile in uno specifico contesto, effettua fallback automatico a `capsscOpenRawScreenShotReadStream` con downsampling box-filter `2x2` da RGBA `1280x720` a YUV420 `640x360`.
2. **Codifica H.264 (`NXCord::H264Encoder` in `nxcord/src/h264_encoder.cpp`)**:
   - Encoder software **H.264 Constrained Baseline** (`profile_idc = 66`, `constraint_set0_flag = 1`, `constraint_set1_flag = 1`, `level_idc = 30`), risoluzione `640x360` (`40x23` macroblocchi con `frame_crop_bottom_offset = 4`), `QP = 30` (adattivo fino a `42` se un frame supera il budget di 48 KiB).
   - Emette keyframe Annex-B (`SPS` + `PPS` + `IDR` con macroblocchi `I_4x4`, predizione `Intra_4x4_DC` e codifica entropica CAVLC) ogni 12 frame (~1.2s) e frame predetti `P` con `P_Skip` (`mb_skip_run`) per i macroblocchi statici o quasi invariati.
3. **Signaling Video, Crittografia DAVE Video e Pacchettizzazione RTP (`VoiceConnection`)**:
   - `VoiceConnection::setVideoEnabled(bool)` invia `VOICE_STATE_UPDATE` (`op: 4` con `"self_video": true/false`) sul Main Gateway e l'opcode `12` (`VIDEO`) sul Voice Gateway con `audio_ssrc`, `video_ssrc`, `rtx_ssrc` e descrittore stream `100` (`640x360` @ 10 FPS, `max_bitrate: 800000`).
   - `VoiceConnection::sendVideoFrame` cifra il frame Annex-B completo tramite `DaveSession::encryptVideo` (`DAVE_MEDIA_TYPE_VIDEO`, `DAVE_CODEC_H264` — `libdave` preserva in chiaro gli header NAL di 1 byte ed esegue lo stuffing dei byte `0x00 0x00` per evitare sequenze di start-code emulation), quindi divide il frame codificato in NAL unit e le trasmette su RTP (`payload_type = 101`, clock `90 kHz` = `+9000` per frame a 10 FPS) secondo **RFC 6184**:
     - **Single NAL Unit** se la NAL è `<= 1150` byte.
     - **FU-A (`type = 28`)** con bit `S` sul primo frammento, `E` sull'ultimo frammento e bit RTP Marker `M = 1` esclusivamente sull'ultimo pacchetto RTP del frame.
   - Ogni pacchetto RTP viene cifrato a livello trasporto con `aead_xchacha20_poly1305_rtpsize` e inviato tramite `DiscordUDPClient::send`, che gestisce la backpressure del socket non-bloccante (`EAGAIN` / `EWOULDBLOCK`) con retry temporizzati.

---

## 4. Storico Problemi Risolti (Post-Mortem Ingegneristico)

1. **Rifiuto codice 2FA TOTP a 6 cifre**:
   - *Causa*: Mancanza di `X-Super-Properties`, mancato inoltro dei cookie di sessione `Set-Cookie` da `/auth/login` a `/auth/mfa/totp`, assenza di `login_instance_id` e caratteri newline/spazi aggiunti dalla tastiera software Switch.
   - *Soluzione*: Sanitizzazione rigorosa dell'input 2FA, persistenza dei cookie HTTP e header v9 completi.
2. **Blocco su `"Establishing connection..."` dopo il login**:
   - *Causa*: Doppia chiamata concorrente a `startConnection()` da `UILoggedIn` e `UIConnectingLayout`, `Content-Length: 0` inviato nelle richieste `GET` HTTP e saturazione dell'heap da 3 MB del sysmodule sui grandi eventi Gateway v9.
   - *Soluzione*: Avvio idempotente della connessione, rimozione di `Content-Length` sulle `GET`, heap portato a `12 MiB` e streaming Gateway con filtro SAX.
3. **Crash `2144-0001` chiudendo i dialoghi di Plutonium**:
   - *Causa*: Callback lambda dichiarata `bool` senza `return` in `ui_Dialog.cpp` di Plutonium, compilata da GCC con istruzione trap `BRK #0x3e8`.
   - *Soluzione*: Aggiunto `return true;` esplicito e flag `-Werror=return-type`.
4. **Voice Close Code `4017` (`DAVE protocol required`)**:
   - *Causa*: Discord richiede obbligatoriamente il protocollo E2EE DAVE v1 (`max_dave_protocol_version: 1`) sui canali vocali moderni.
   - *Soluzione*: Porting completo di `libdave`, `MLS++` e `OpenSSL 3.5.9` su Nintendo Switch con entropia hardware `csrng`.
5. **Voice Close Code `4002` (`DECODE_ERROR`)**:
   - *Causa*: Invio duplicato di `SELECT_PROTOCOL` da thread concorrenti e invio degli opcode MLS `26` (`KEY_PACKAGE`) e `28` (`COMMIT_WELCOME`) come testo JSON invece che come frame WebSocket binari (`[opcode:uint8][payload]`).
   - *Soluzione*: Accodamento thread-safe di `SELECT_PROTOCOL` e invio binario tramite `wslay_event_queue_msg` con `WSLAY_BINARY_frame`.
6. **Voice Close Code `4006` (`Session is no longer valid`)**:
   - *Causa*: Invio prematuro di `SPEAKING` (`op: 5`) o pacchetti RTP prima della ricezione di `SESSION_DESCRIPTION` (`op: 4`) e del completamento della transizione MLS DAVE, oppure riutilizzo di token/endpoint vocali scaduti.
   - *Soluzione*: Gating rigoroso di `sendSpeaking` e dell'invio RTP su `sessionDescriptionReceived && canSendAudio()`, e invalidazione immediata di token/endpoint vocali alla disconnessione.
7. **Crash del sysmodule uscendo e rientrando in un canale vocale (`error 0x101`)**:
   - *Causa*: Mancato drain dei puntatori `AudioInBuffer` / `AudioOutBuffer` rilasciati da `audinStopAudioIn()` / `audoutStopAudioOut()` prima di riaccodarli nella sessione successiva.
   - *Soluzione*: Drain completo dei buffer rilasciati in `NXCordAudioCapture::stop()` e `NXCordAudioPlayer::stop()`.

---

## 5. Suite di Test di Regressione e Linee Guida

Prima di generare una build o aprire una Pull Request, eseguire sempre tutti gli 8 test host:

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

### Regole d'Oro per lo Sviluppo su NXCord
- **Budget Heap del Sysmodule**: Non allocare mai interi messaggi Gateway o risposte REST come `std::string` o DOM completi senza limiti; usare sempre i parser SAX e i limiti definiti in `include/nxcord/memory_limits.hpp`.
- **Sincronizzazione Thread**: Acquisire sempre `_client_mutex` per accedere a `NXCordClient` dai comandi IPC, e non chiamare mai `wslay_event_queue_*` direttamente dal thread UDP (usare le code protette da mutex svuotate in `flushPendingUDP()`).
- **Sicurezza dei Log**: Non scrivere mai token utente, password o chiavi crittografiche MLS nei file di log `/config/nxcord/*.txt`.
