#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <mutex>
#ifdef __SWITCH__
#include <switch.h>
#endif

// OpenSSL has no Linux entropy source on Horizon. Never seed it from a clock
// or rand(). The same OS CSPRNG already used for TLS supplies every random byte.
bool nxcord_init_dave_crypto() {
#ifdef __SWITCH__
  static const RAND_METHOD method = {
      nullptr,
      [](unsigned char *out, int length) -> int {
        return length >= 0 && R_SUCCEEDED(csrngGetRandomBytes(out, length));
      },
      nullptr, nullptr,
      [](unsigned char *out, int length) -> int {
        return length >= 0 && R_SUCCEEDED(csrngGetRandomBytes(out, length));
      },
      []() -> int { return 1; }};
  static std::once_flag once;
  static bool success = false;
  std::call_once(once, [] {
    unsigned char probe[32];
    success = OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG | OPENSSL_INIT_NO_ATEXIT, nullptr) == 1 &&
              RAND_set_rand_method(&method) == 1 && RAND_priv_bytes(probe, sizeof(probe)) == 1;
    OPENSSL_cleanse(probe, sizeof(probe));
  });
  return success;
#else
  return RAND_status() == 1;
#endif
}
