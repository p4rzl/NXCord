#pragma once

#include <sleepy_discord/sleepy_discord.h>

#include <common/logger.hpp>
#include <mutex>
#include "gateway_events.hpp"

class DiscordScheduleHandler;

class DiscordClient : public SleepyDiscord::BaseDiscordClient {
 private:
  static constexpr std::string_view loginURL =
      "https://discord.com/api/v9/auth/login";
  static constexpr std::string_view mfaURL =
      "https://discord.com/api/v9/auth/mfa/totp";
  static constexpr std::string_view mfaBackupURL =
      "https://discord.com/api/v9/auth/mfa/backup";
  static constexpr std::string_view mfaSmsURL =
      "https://discord.com/api/v9/auth/mfa/sms";
  static constexpr std::string_view mfaSmsSendURL =
      "https://discord.com/api/v9/auth/mfa/sms/send";
  static constexpr std::string_view meURL =
      "https://discord.com/api/v9/users/@me";

  std::string _ticket;
  std::string _login_instance_id;
  std::string _auth_cookies;
  std::string _last_email;
  std::string _last_password;
  bool _mfa_totp = true;
  bool _mfa_sms = false;
  bool _mfa_backup = false;
  int _ticket_attempts = 0;
  std::mutex _websocket_send_mutex;
  bool _start_pending = false;

  bool connect(const std::string &, GenericMessageReceiver *,
               SleepyDiscord::WebsocketConnection &) override;

  void send(std::string, SleepyDiscord::WebsocketConnection &) override;
  void sendBinary(std::string, SleepyDiscord::WebsocketConnection &) override;

  void disconnect(unsigned int, std::string,
                  SleepyDiscord::WebsocketConnection &) override;

  void onError(SleepyDiscord::ErrorCode, std::string) override;

  SleepyDiscord::Timer schedule(SleepyDiscord::TimedTask code,
                                time_t milliseconds) override;

  void stopClient() override;

  bool shouldProcessDispatch(const std::string &event) const override {
    return nxcord_gateway_event(event);
  }
  bool useLightweightServerEvents() const override { return true; }

  inline void processMessage(const std::string &message) override {
    Logger::write("Client processing message (%zu bytes)\n", message.size());
    BaseDiscordClient::processMessage(message);
  }

  friend DiscordScheduleHandler;

 protected:
  std::string _token;

 public:
  DiscordClient();

  ~DiscordClient() override;

  bool setLoginCredentials(const std::string &email,
                           const std::string &password, bool *has2fa,
                           std::string *error);

  bool submit2faTicket(const std::string &code, std::string *error = nullptr);

  inline bool tokenAvailable() const { return !_token.empty(); }

  virtual void startConnection();

  void tick();
};
