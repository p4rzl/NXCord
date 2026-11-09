# NXCord DAVE: experimental console build

This build integrates Discord's libdave (DAVE version 1), Cisco MLS++, and
OpenSSL 3.5.9. It is **not yet verified against Discord on a physical Switch**.
Host tests exercise actual MLS key exchange and Opus-frame encryption, not a
mock of the cryptographic library. They do not prove live interoperability,
Horizon entropy/exception behavior, resource use, or in-game audio quality.

## Install / test (Italiano)

1. Aggiorna **insieme** `switch/nxcord.nro`, `switch/.overlays/nxcord.ovl` e
   `atmosphere/contents/5600000000000000/exefs.nsp`, mantenendo login e config.
2. Riavvia la console. Nel nuovo log deve comparire
   `voice gateway=8 DAVE=1 (experimental)`.
3. Prova un canale vuoto, poi fai entrare un altro partecipante con Discord
   aggiornato. Il solo ingresso in elenco non verifica la connessione audio.
4. Verifica ascolto e microfono in entrambe le direzioni. Fai uscire/rientrare
   il secondo partecipante per verificare i cambi di chiave e il reset solo.
5. Chiudi l'NRO, avvia un gioco e verifica gli stessi controlli da Ultrahand.
   La rete e la cifratura rimangono nel sysmodule; l'overlay usa solo IPC.
6. Verifica uscita, ingresso in un altro canale e riconnessione dopo un cambio
   di rete. Se fallisce, conserva `/config/nxcord/nxcord-sys.txt` e comunica
   orario del tentativo, codice mostrato e se il canale era vuoto o popolato.

Non pubblicare log vecchi con token/password. Questa build registra solo
opcode, stato e codici di errore, non messaggi MLS, chiavi o audio.

## Limits

- Up to **32 participants including self** in the voice cryptographic session.
  Exceeding that bound fails closed, rather than silently ignoring participants.
- Voice frames remain transport-encrypted with XChaCha20-Poly1305 after DAVE
  encryption; receivers decrypt transport, then DAVE, then decode Opus.
- An unestablished/sole-member MLS group sends **no audio** until a sender key
  exists. It never sends plaintext as a workaround for DAVE negotiation.
- Plaintext frame passthrough is allowed only if the voice server explicitly
  selects protocol version 0 or announces a downgrade transition to it.
- Invalid commit/welcome recovery is bounded to three attempts per connection.
- Local voice/crypto setup failures use diagnostic code **-2**, not a forged
  Discord close code. **4017** is a genuine server DAVE-version rejection;
  **4006** means the voice session is no longer valid.
- Identity fingerprint verification UI and screen sharing are not implemented.

## Build and automated tests

CMake >= 3.20, Perl, make and a C++17 compiler are needed. The first configure
downloads pinned archives and verifies SHA-256 hashes; subsequent builds use
the cache. The Switch build uses a dedicated, statically linked libcrypto,
not the host OpenSSL. Only the MLS/DAVE targets enable C++ exceptions.

```sh
export DEVKITPRO=/opt/devkitpro # or your installation
./build_release.sh           # creates build/release.zip

# Host: system OpenSSL >= 3 with development headers required
cmake -S tests/dave -B build/dave-host -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/dave-host --parallel 4
ctest --test-dir build/dave-host --output-on-failure
```

Upstream pins and archive hashes are in `externals/dave/CMakeLists.txt`.
Licenses for the newly linked dependencies are included in the package.
