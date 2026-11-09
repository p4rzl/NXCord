#include <switch.h>

#include <nxcord/base64.hpp>
#include <nxcord/discord_client.hpp>
#include <nxcord/discord_schedule_handler.hpp>
#include <nxcord/discord_session.hpp>
#include <nxcord/discord_udp_client.hpp>
#include <nxcord/discord_websocket.hpp>
#include <nxcord/mbedtls_wrapper.hpp>
#include <nxcord/opus_decoder.hpp>
#include <set>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

DiscordClient::DiscordClient() {
  setScheduleHandler<DiscordScheduleHandler>();
  // Create dummy variables here, so linker won't omit them
  DiscordUDPClient client;
  OpusDecoder opusDecoder(true);
  quit(false, true);
}

DiscordClient::~DiscordClient() { quit(); }

std::string create_acceptkey(const std::string &clientkey) {
  std::string s = clientkey + WS_GUID;
  unsigned char sha1[SHA1_HASH_SIZE];
  sha1CalculateHash(sha1, s.c_str(), s.size());
  return base64_encode(sha1, SHA1_HASH_SIZE);
}

bool DiscordClient::connect(const std::string &uri,
                            GenericMessageReceiver *message_processor,
                            SleepyDiscord::WebsocketConnection &connection) {
  unsigned char random_bytes[16];

  Logger::write("Generating random key\n");
  if (R_FAILED(csrngGetRandomBytes(random_bytes, 16))) {
    Logger::write("Failed to generate random bytes\n");
    message_processor->handleFailToConnect();
    return false;
  }

  std::string client_key = base64_encode(random_bytes, 16);

  bool zlib_compress = false;
  std::string real_uri = uri;
  std::string gateway = "wss://gateway.discord.gg";
  if (uri.substr(0, gateway.size()) == gateway) {
    // Enable compression for client websocket
    if (real_uri.find('?') == std::string::npos) {
      real_uri += "/?v=9&encoding=json&compress=zlib-stream";
    } else {
      real_uri += "&encoding=json&compress=zlib-stream";
    }
    zlib_compress = true;
  }

  DiscordSession session;
  session.setUrl(real_uri);

  std::vector<SleepyDiscord::HeaderPair> header = {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Key", client_key},
      {"Sec-WebSocket-Version", "13"}};
  session.setHeader(header);

  Logger::write("Request connection to websocket\n");

  SleepyDiscord::Response response;
  std::unique_ptr<MBedTLSWrapper> mbedtls_wrapper =
      session.request(SleepyDiscord::Get, &response);

  if (response.statusCode !=
      SleepyDiscord::SWITCHING_PROTOCOLS) {  // error check
    Logger::write("Websocket connection Error: %s\n", response.text.c_str());
    message_processor->handleFailToConnect();
    return false;
  }

  auto it = response.header.find("sec-websocket-accept");
  if (it == response.header.end()) {
    Logger::write("Couldn't parse sec-websocket-accept\n");
    message_processor->handleFailToConnect();
    return false;
  }

  std::string accept_key = create_acceptkey(client_key);
  if (it->second != accept_key) {
    Logger::write("Accept key is invalid\n");
    message_processor->handleFailToConnect();
    return false;
  }

  Logger::write("Connection to websocket established\n");
  connection = std::make_shared<DiscordWebsocket>(
      message_processor, mbedtls_wrapper, zlib_compress);
  message_processor->initialize();
  return true;
}

void DiscordClient::send(std::string message,
                         SleepyDiscord::WebsocketConnection &connection) {
  if (connection) {
    auto discord_websocket =
        std::static_pointer_cast<DiscordWebsocket>(connection);
    std::scoped_lock lock(_websocket_send_mutex);
    int ret = discord_websocket->queue_message(message);
    if (ret != 0) {  // error
      Logger::write("Send error: ");
      switch (ret) {
        case WSLAY_ERR_NO_MORE_MSG:
          Logger::write("Could not queue given message\n");
          break;
        case WSLAY_ERR_INVALID_ARGUMENT:
          Logger::write("The given message is invalid\n");
          break;
        case WSLAY_ERR_NOMEM:
          Logger::write("Out of memory\n");
          break;
        default:
          Logger::write("unknown\n");
          break;
      }
    }
  }
}

void DiscordClient::disconnect(unsigned int code, const std::string reason,
                               SleepyDiscord::WebsocketConnection &connection) {
  Logger::write("Disconnecting client %s\n", reason.c_str());
  if (connection) {
    auto discord_websocket =
        std::static_pointer_cast<DiscordWebsocket>(connection);
    discord_websocket->disconnect(code, reason);
    connection.reset();
  }
}

void DiscordClient::sendBinary(std::string message,
                               SleepyDiscord::WebsocketConnection &connection) {
  if (!connection) return;
  std::scoped_lock lock(_websocket_send_mutex);
  const int result = std::static_pointer_cast<DiscordWebsocket>(connection)->queue_message(message, true);
  if (result) Logger::write("Voice binary queue failed: %d\n", result);
}

void DiscordClient::onError(SleepyDiscord::ErrorCode errorCode,
                            const std::string error_message) {
  Logger::write("Error %i: %s\n", errorCode, error_message.c_str());
}

SleepyDiscord::Timer DiscordClient::schedule(SleepyDiscord::TimedTask code,
                                             const time_t milliseconds) {
  return dynamic_cast<DiscordScheduleHandler &>(getScheduleHandler())
      .schedule(std::move(code), milliseconds);
}

void DiscordClient::stopClient() {
  _start_pending = false;
  dynamic_cast<DiscordScheduleHandler &>(getScheduleHandler()).clear();
}

namespace {
constexpr const char *kSuperProperties =
    "eyJvcyI6IldpbmRvd3MiLCJicm93c2VyIjoiQ2hyb21lIiwiZGV2aWNlIjoiIiwic3lzdGVtX2"
    "xvY2FsZSI6ImVuLVVTIiwiYnJvd3Nlcl91c2VyX2FnZW50IjoiTW96aWxsYS81LjAgKFdpbmRv"
    "d3MgTlQgMTAuMDsgV2luNjQ7IHg2NCkgQXBwbGVXZWJLaXQvNTM3LjM2IChLSFRNTCwgbGlrZS"
    "BHZWNrbykgQ2hyb21lLzEyNC4wLjAuMCBTYWZhcmkvNTM3LjM2IiwiYnJvd3Nlcl92ZXJzaW9u"
    "IjoiMTI0LjAuMC4wIiwib3NfdmVyc2lvbiI6IjEwIiwicmVmZXJyZXIiOiIiLCJyZWZlcnJpbm"
    "dfZG9tYWluIjoiIiwicmVmZXJyZXJfY3VycmVudCI6IiIsInJlZmVycmluZ19kb21haW5fY3Vy"
    "cmVudCI6IiIsInJlbGVhc2VfY2hhbm5lbCI6InN0YWJsZSIsImNsaWVudF9idWlsZF9udW1iZX"
    "IiOjI5MDAwMCwiY2xpZW50X2V2ZW50X3NvdXJjZSI6bnVsbH0=";

std::string trim_whitespace(const std::string &str) {
  size_t first = 0;
  while (first < str.size() &&
         (str[first] == ' ' || str[first] == '\t' || str[first] == '\r' ||
          str[first] == '\n')) {
    ++first;
  }
  size_t last = str.size();
  while (last > first &&
         (str[last - 1] == ' ' || str[last - 1] == '\t' ||
          str[last - 1] == '\r' || str[last - 1] == '\n')) {
    --last;
  }
  return str.substr(first, last - first);
}

std::string clean_mfa_code(const std::string &str) {
  std::string out;
  out.reserve(str.size());
  for (char c : str) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '-') {
      out.push_back(c);
    }
  }
  return out;
}
}  // namespace

bool DiscordClient::setLoginCredentials(const std::string &email,
                                        const std::string &password,
                                        bool *has2fa, std::string *error) {
  quit();

  std::string clean_email = trim_whitespace(email);
  std::string clean_password = password;
  while (!clean_password.empty() &&
         (clean_password.back() == '\r' || clean_password.back() == '\n')) {
    clean_password.pop_back();
  }

  if (clean_email.empty()) {
    if (error) {
      *error = "Email or Token cannot be empty.";
    }
    return false;
  }

  // Allow direct token login when password is empty (or when email is a token)
  if (clean_password.empty() && clean_email.find('@') == std::string::npos) {
    std::string candidate_token = clean_email;
    while (!candidate_token.empty() &&
           (candidate_token.front() == ' ' || candidate_token.front() == '"' ||
            candidate_token.front() == '\'')) {
      candidate_token.erase(candidate_token.begin());
    }
    while (!candidate_token.empty() &&
           (candidate_token.back() == ' ' || candidate_token.back() == '"' ||
            candidate_token.back() == '\'' || candidate_token.back() == '\r' ||
            candidate_token.back() == '\n')) {
      candidate_token.pop_back();
    }

    DiscordSession session;
    session.setUrl(std::string(meURL));
    std::vector<SleepyDiscord::HeaderPair> header = {
        {"Authorization", candidate_token},
        {"X-Discord-Locale", "en-US"},
        {"X-Super-Properties", kSuperProperties}};
    session.setHeader(header);

    SleepyDiscord::Response response = session.request(SleepyDiscord::Get);
    if (response.statusCode == SleepyDiscord::ErrorCode::OK) {
      _token = candidate_token;
      return true;
    }
    if (error) {
      if (response.statusCode == SleepyDiscord::GENERAL_ERROR) {
        *error = "Failed to establish connection";
      } else {
        *error = "Invalid Discord Token (HTTP " +
                 std::to_string(response.statusCode) + ")";
      }
    }
    return false;
  }

  _last_email = clean_email;
  _last_password = clean_password;
  _ticket.clear();
  _login_instance_id.clear();
  _ticket_attempts = 0;

  DiscordSession session;
  session.setUrl(std::string(loginURL));

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("login");
  writer.String(clean_email.c_str(), clean_email.size());
  writer.Key("password");
  writer.String(clean_password.c_str(), clean_password.size());
  writer.Key("undelete");
  writer.Bool(false);
  writer.Key("login_source");
  writer.Null();
  writer.Key("gift_code_sku_id");
  writer.Null();
  writer.EndObject();
  std::string request_body(buffer.GetString(), buffer.GetSize());

  std::vector<SleepyDiscord::HeaderPair> header = {
      {"Content-Type", "application/json"},
      {"Origin", "https://discord.com"},
      {"Referer", "https://discord.com/login"},
      {"X-Discord-Locale", "en-US"},
      {"X-Super-Properties", kSuperProperties}};
  session.setHeader(header);
  session.setBody(&request_body);

  SleepyDiscord::Response response = session.request(SleepyDiscord::Post);
  auto cookie_it = response.header.find("set-cookie");
  if (cookie_it != response.header.end() && !cookie_it->second.empty()) {
    _auth_cookies = cookie_it->second;
  }

  rapidjson::Document values;
  values.Parse(response.text.c_str(), response.text.length());

  if (!values.HasParseError() && values.IsObject()) {
    if (response.statusCode == SleepyDiscord::ErrorCode::OK) {
      bool sms = values.HasMember("sms") && values["sms"].IsBool() &&
                 values["sms"].GetBool();
      bool mfa = values.HasMember("mfa") && values["mfa"].IsBool() &&
                 values["mfa"].GetBool();
      bool totp = !values.HasMember("totp") ||
                  (values["totp"].IsBool() && values["totp"].GetBool());
      bool backup = values.HasMember("backup") && values["backup"].IsBool() &&
                    values["backup"].GetBool();
      if (sms || mfa) {
        if (has2fa) {
          *has2fa = true;
        }
        _mfa_totp = totp;
        _mfa_sms = sms;
        _mfa_backup = backup;
        if (values.HasMember("ticket") && values["ticket"].IsString()) {
          _ticket = std::string(values["ticket"].GetString());
        }
        if (values.HasMember("login_instance_id") &&
            values["login_instance_id"].IsString()) {
          _login_instance_id =
              std::string(values["login_instance_id"].GetString());
        }
        if (_mfa_sms && !_mfa_totp && !_ticket.empty()) {
          DiscordSession sms_session;
          sms_session.setUrl(std::string(mfaSmsSendURL));
          rapidjson::StringBuffer sms_buf;
          rapidjson::Writer<rapidjson::StringBuffer> sms_writer(sms_buf);
          sms_writer.StartObject();
          sms_writer.Key("ticket");
          sms_writer.String(_ticket.c_str(), _ticket.size());
          sms_writer.EndObject();
          std::string sms_body(sms_buf.GetString(), sms_buf.GetSize());
          std::vector<SleepyDiscord::HeaderPair> sms_header = {
              {"Content-Type", "application/json"},
              {"Origin", "https://discord.com"},
              {"Referer", "https://discord.com/login"},
              {"X-Discord-Locale", "en-US"},
              {"X-Super-Properties", kSuperProperties}};
          if (!_auth_cookies.empty()) {
            sms_header.push_back({"Cookie", _auth_cookies.c_str()});
          }
          sms_session.setHeader(sms_header);
          sms_session.setBody(&sms_body);
          sms_session.request(SleepyDiscord::Post);
        }
        return false;
      }
      if (values.HasMember("token") && values["token"].IsString()) {
        _token = std::string(values["token"].GetString());
        _last_password.clear();
        return true;
      }
    } else if (response.statusCode == SleepyDiscord::GENERAL_ERROR) {
      if (error) {
        *error = "Failed to establish connection";
        return false;
      }
    }

    if (error) {
      if (values.HasMember("captcha_key")) {
        *error =
            "Captcha required by Discord. Enter your Token in Email/Token "
            "(leave Password empty) or in /config/nxcord/nxcord.ini";
      } else if (values.HasMember("errors") && values["errors"].IsObject()) {
        const auto &errs = values["errors"];
        const char *fields[] = {"login", "email", "password"};
        bool found_err = false;
        for (const char *field : fields) {
          if (errs.HasMember(field) && errs[field].IsObject() &&
              errs[field].HasMember("_errors") &&
              errs[field]["_errors"].IsArray() &&
              !errs[field]["_errors"].Empty() &&
              errs[field]["_errors"][0].IsObject() &&
              errs[field]["_errors"][0].HasMember("message") &&
              errs[field]["_errors"][0]["message"].IsString()) {
            *error = std::string(
                errs[field]["_errors"][0]["message"].GetString());
            found_err = true;
            break;
          }
        }
        if (!found_err && values.HasMember("message") &&
            values["message"].IsString()) {
          *error = std::string(values["message"].GetString());
        }
      } else if (values.HasMember("message") && values["message"].IsString()) {
        *error = std::string(values["message"].GetString());
      } else if (values.HasMember("login") && values["login"].IsArray() &&
                 !values["login"].Empty() && values["login"][0].IsString()) {
        *error = std::string(values["login"][0].GetString());
      } else if (values.HasMember("email") && values["email"].IsArray() &&
                 !values["email"].Empty() && values["email"][0].IsString()) {
        *error = std::string(values["email"][0].GetString());
      } else if (values.HasMember("password") && values["password"].IsArray() &&
                 !values["password"].Empty() &&
                 values["password"][0].IsString()) {
        *error = std::string(values["password"][0].GetString());
      }
    }
  } else if (error) {
    if (response.statusCode == SleepyDiscord::GENERAL_ERROR) {
      *error = "Failed to establish connection";
    } else {
      *error = "Failed to parse login response (HTTP " +
               std::to_string(response.statusCode) + ")";
    }
  }
  return false;
}

bool DiscordClient::submit2faTicket(const std::string &code,
                                    std::string *error) {
  std::string clean_code = clean_mfa_code(code);
  if (clean_code.empty()) {
    if (error) {
      *error = "Code can't be empty.";
    }
    return false;
  }

  if (_ticket.empty() && !_last_email.empty() && !_last_password.empty()) {
    bool dummy_2fa = false;
    setLoginCredentials(_last_email, _last_password, &dummy_2fa, nullptr);
  }
  if (_ticket.empty()) {
    if (error) {
      *error = "Missing 2FA session ticket. Press B and log in again.";
    }
    return false;
  }

  auto try_mfa_endpoint = [&](std::string_view endpoint_url,
                              SleepyDiscord::Response &out_resp,
                              rapidjson::Document &out_doc) -> bool {
    DiscordSession session;
    session.setUrl(std::string(endpoint_url));

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("code");
    writer.String(clean_code.c_str(), clean_code.size());
    writer.Key("ticket");
    writer.String(_ticket.c_str(), _ticket.size());
    if (!_login_instance_id.empty()) {
      writer.Key("login_instance_id");
      writer.String(_login_instance_id.c_str(), _login_instance_id.size());
    }
    writer.Key("login_source");
    writer.Null();
    writer.Key("gift_code_sku_id");
    writer.Null();
    writer.EndObject();
    std::string request_body(buffer.GetString(), buffer.GetSize());

    std::vector<SleepyDiscord::HeaderPair> header = {
        {"Content-Type", "application/json"},
        {"Origin", "https://discord.com"},
        {"Referer", "https://discord.com/login"},
        {"X-Discord-Locale", "en-US"},
        {"X-Super-Properties", kSuperProperties}};
    if (!_auth_cookies.empty()) {
      header.push_back({"Cookie", _auth_cookies.c_str()});
    }
    session.setHeader(header);
    session.setBody(&request_body);

    out_resp = session.request(SleepyDiscord::Post);
    out_doc.Parse(out_resp.text.c_str(), out_resp.text.length());

    if (!out_doc.HasParseError() && out_doc.IsObject() &&
        out_resp.statusCode == SleepyDiscord::ErrorCode::OK &&
        out_doc.HasMember("token") && out_doc["token"].IsString()) {
      _token = std::string(out_doc["token"].GetString());
      _ticket.clear();
      _login_instance_id.clear();
      _last_password.clear();
      return true;
    }
    return false;
  };

  std::string_view primary_url = mfaURL;
  if (clean_code.size() == 8) {
    primary_url = mfaBackupURL;
  } else if (_mfa_sms && !_mfa_totp) {
    primary_url = mfaSmsURL;
  }

  SleepyDiscord::Response response;
  rapidjson::Document values;
  int prior_attempts = _ticket_attempts++;

  if (try_mfa_endpoint(primary_url, response, values)) {
    return true;
  }

  // If user also has SMS or Backup enabled, try fallback endpoint before
  // refreshing ticket
  if (primary_url == mfaURL && _mfa_sms) {
    SleepyDiscord::Response sms_resp;
    rapidjson::Document sms_doc;
    if (try_mfa_endpoint(mfaSmsURL, sms_resp, sms_doc)) {
      return true;
    }
  } else if (primary_url == mfaBackupURL && _mfa_totp) {
    SleepyDiscord::Response totp_resp;
    rapidjson::Document totp_doc;
    if (try_mfa_endpoint(mfaURL, totp_resp, totp_doc)) {
      return true;
    }
  }

  // If the ticket might have been invalidated by a previous attempt or expired,
  // refresh the ticket once via /auth/login and retry with the new ticket.
  bool ticket_error =
      (!values.HasParseError() && values.IsObject() &&
       ((values.HasMember("code") && values["code"].IsInt() &&
         values["code"].GetInt() == 60003) ||
        (values.HasMember("errors") && values["errors"].IsObject() &&
         values["errors"].HasMember("ticket"))));

  if ((prior_attempts >= 1 || ticket_error) && !_last_email.empty() &&
      !_last_password.empty()) {
    bool dummy_2fa = false;
    std::string login_err;
    setLoginCredentials(_last_email, _last_password, &dummy_2fa, &login_err);
    if (!_ticket.empty()) {
      _ticket_attempts = 1;
      if (try_mfa_endpoint(primary_url, response, values)) {
        return true;
      }
    }
  }

  if (error) {
    if (response.statusCode == SleepyDiscord::GENERAL_ERROR) {
      *error = "Failed to establish connection";
    } else if (!values.HasParseError() && values.IsObject()) {
      int err_code = (values.HasMember("code") && values["code"].IsInt())
                         ? values["code"].GetInt()
                         : 0;
      std::string msg;
      if (values.HasMember("errors") && values["errors"].IsObject()) {
        const auto &errs = values["errors"];
        const char *fields[] = {"code", "ticket", "login_instance_id"};
        for (const char *field : fields) {
          if (errs.HasMember(field) && errs[field].IsObject() &&
              errs[field].HasMember("_errors") &&
              errs[field]["_errors"].IsArray() &&
              !errs[field]["_errors"].Empty() &&
              errs[field]["_errors"][0].IsObject() &&
              errs[field]["_errors"][0].HasMember("message") &&
              errs[field]["_errors"][0]["message"].IsString()) {
            msg = errs[field]["_errors"][0]["message"].GetString();
            break;
          }
        }
      }
      if (msg.empty() && values.HasMember("message") &&
          values["message"].IsString()) {
        msg = values["message"].GetString();
      }
      if (err_code == 60008) {
        *error =
            "Invalid 2FA code (60008). Sync your phone clock to Automatic "
            "time, or enter an 8-char Discord backup code.";
      } else if (!msg.empty()) {
        if (err_code != 0) {
          *error = msg + " (" + std::to_string(err_code) + ")";
        } else {
          *error = msg;
        }
      } else {
        *error = "2FA failed (HTTP " + std::to_string(response.statusCode) +
                 ")";
      }
    } else {
      *error = "Failed to parse 2FA response (HTTP " +
               std::to_string(response.statusCode) + ")";
    }
  }
  return false;
}

void DiscordClient::startConnection() {
  if (!connection && !_start_pending && !_token.empty()) {
    _start_pending = true;
    schedule([this]() {
      _start_pending = false;
      if (!connection) {
        start(_token, 1);
      }
    }, 0);
  }
}

void DiscordClient::tick() {
  if (connection) {
    auto discord_websocket =
        std::static_pointer_cast<DiscordWebsocket>(connection);
    discord_websocket->tick();
  }

  // Voice connections can remove themselves from the list
  // Consider this when looping through
  std::set<SleepyDiscord::VoiceContext *> already_called;
  size_t index = 0;
  while (index < voiceConnections.size()) {
    auto it = voiceConnections.begin();
    for (size_t i = 0; i < index; ++i) {
      ++it;
    }
    SleepyDiscord::VoiceConnection &voice_connection = *it;
    SleepyDiscord::VoiceContext &context = voice_connection.getContext();
    if (already_called.find(&context) != already_called.end()) {
      ++index;
    } else {
      if (voice_connection.connection) {
        voice_connection.flushPendingUDP();
        auto voice_websocket = std::static_pointer_cast<DiscordWebsocket>(
            voice_connection.connection);
        voice_websocket->tick();
      }
      already_called.insert(&context);
      index = 0;
    }
  }

  dynamic_cast<DiscordScheduleHandler &>(getScheduleHandler()).tick();
}
