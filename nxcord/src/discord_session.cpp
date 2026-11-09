#include <http_parser.h>
#include <netdb.h>
#include <strings.h>
#include <switch.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <common/logger.hpp>
#include <nxcord/discord_session.hpp>
#include <nxcord/zlib_wrapper.hpp>
#include <nxcord/memory_limits.hpp>
#include <sstream>

#define NEW_LINE "\r\n"
constexpr const char *METHOD_NAMES[] = {"POST", "PATCH", "DELETE", "GET",
                                        "PUT"};

SleepyDiscord::CustomInit SleepyDiscord::Session::init =
    []() -> SleepyDiscord::GenericSession * { return new DiscordSession; };

void DiscordSession::setUrl(const std::string &url) { _url = url; }

void DiscordSession::setBody(const std::string *body) { _body = body; }

void DiscordSession::setHeader(
    const std::vector<SleepyDiscord::HeaderPair> &header) {
  _headers = &header;
}

bool DiscordSession::contains_header(const std::string &key) const {
  if (!_headers) {
    return false;
  }
  for (const SleepyDiscord::HeaderPair &pair : *_headers) {
    if (strcasecmp(pair.name, key.c_str()) == 0) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<MBedTLSWrapper> DiscordSession::request(
    const SleepyDiscord::RequestMethod method,
    SleepyDiscord::Response *response) {
  Logger::write("Requesting %s %s\n", _url.c_str(), METHOD_NAMES[method]);

  int ret;
  std::string hostname, path, port = "443";
  std::string protocol = _url.substr(0, _url.find("://"));
  size_t offset = protocol.length() + 3;
  if (offset < _url.length()) {  // makes sure that there is a hostname
    size_t slash_pos = _url.find('/', offset);
    if (slash_pos != std::string::npos) {
      hostname = _url.substr(offset, slash_pos - offset);
      path = _url.substr(slash_pos);
    } else {
      hostname = _url.substr(offset);
      path = "/";
    }
    size_t colon_pos = hostname.rfind(':');
    if (colon_pos != std::string::npos) {
      std::string parsed_port = hostname.substr(colon_pos + 1);
      hostname.resize(colon_pos);
      if (!parsed_port.empty() && parsed_port != "80" &&
          parsed_port.find_first_not_of("0123456789") == std::string::npos) {
        port = std::move(parsed_port);
      }
    }
  } else {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text = R"({"code":0,"message":"Invalid url"})";
    return nullptr;
  }

  auto mbedtls_wrapper = std::make_unique<MBedTLSWrapper>(hostname);
  if (!mbedtls_wrapper->usable()) {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text =
        R"({"code":0,"message":")" + mbedtls_wrapper->getError() + "\"}";
    return mbedtls_wrapper;
  }

  addrinfo hints{};
  int fd = -1;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res;

  Logger::write("Connecting to %s:%s\n", hostname.c_str(), port.c_str());
  ret = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);
  if (ret != 0) {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text = R"({"code":0,"message":"getaddrinfo: )" +
                     std::string(gai_strerror(ret)) + "\"}";
    return mbedtls_wrapper;
  }

  for (addrinfo *rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd == -1) {
      continue;
    }
    while ((ret = connect(fd, rp->ai_addr, rp->ai_addrlen)) == -1 &&
           errno == EINTR)
      ;
    if (ret == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd == -1) {  // error check
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text =
        R"({"code":431,"message":"Could not connect to the host"})";
    return mbedtls_wrapper;
  }

  Logger::write("Connection established %d\n", fd);
  mbedtls_wrapper->setFd(fd);

  Logger::write("Start handshake\n");
  if (!mbedtls_wrapper->startSSL()) {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text =
        R"({"code":431,"message":")" + mbedtls_wrapper->getError() + "\"}";
    return mbedtls_wrapper;
  }

  std::string host_header = (port == "443") ? hostname : (hostname + ":" + port);
  std::string metadata = METHOD_NAMES[method];
  metadata.append(" ").append(path).append(" HTTP/1.1").append(NEW_LINE);
  metadata.append("Host: ").append(host_header).append(NEW_LINE);

  if (!contains_header("Accept")) {
    metadata.append("Accept: */*").append(NEW_LINE);
  }
  if (!contains_header("User-Agent")) {
    metadata
        .append(
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 "
            "Safari/537.36")
        .append(NEW_LINE);
  }
  if (!contains_header("Connection")) {
    metadata.append("Connection: close").append(NEW_LINE);
  }
  if (!contains_header("Accept-Encoding")) {
    metadata.append("Accept-Encoding: gzip").append(NEW_LINE);
  }

  if (_headers) {
    for (const auto &pair : *_headers) {
      metadata.append(pair.name)
          .append(": ")
          .append(pair.value)
          .append(NEW_LINE);
    }
  }
  if (_body && !contains_header("Content-Length")) {
    metadata.append("Content-Length: ")
        .append(std::to_string(_body->size()))
        .append(NEW_LINE);
  } else if (!_body &&
             (method == SleepyDiscord::Post || method == SleepyDiscord::Put ||
              method == SleepyDiscord::Patch) &&
             !contains_header("Content-Length")) {
    metadata.append("Content-Length: 0").append(NEW_LINE);
  }
  metadata.append(NEW_LINE);

  Logger::write("Sending payload\n");
  auto write_all = [&mbedtls_wrapper](const unsigned char *data,
                                      size_t len) -> int {
    size_t written = 0;
    while (written < len) {
      int r = mbedtls_wrapper->write(data + written, len - written);
      if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        continue;
      }
      if (r < 0) {
        return r;
      }
      if (r == 0) {
        break;
      }
      written += static_cast<size_t>(r);
    }
    return static_cast<int>(written);
  };

  ret = write_all(reinterpret_cast<const unsigned char *>(metadata.c_str()),
                  metadata.size());
  if (ret < 0) {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text =
        R"({"code":431,"message":"Write error )" + std::to_string(ret) + "\"}";
    return mbedtls_wrapper;
  }
  if (_body && !_body->empty()) {
    ret = write_all(reinterpret_cast<const unsigned char *>(_body->c_str()),
                    _body->size());
    if (ret < 0) {
      response->statusCode = SleepyDiscord::GENERAL_ERROR;
      response->text = R"({"code":431,"message":"Write error )" +
                       std::to_string(ret) + "\"}";
      return mbedtls_wrapper;
    }
  }

  Logger::write("Reading response\n");
  constexpr int buf_size = 1024;
  unsigned char buf[buf_size];
  int read_len = 0;

  struct ParserData {
    std::string current_field;
    std::string current_value;
    bool last_was_value = false;
    bool message_complete = false;
    bool too_large = false;
    SleepyDiscord::Response *response = nullptr;

    void flush_header() {
      if (!last_was_value) {
        return;
      }
      std::transform(current_field.begin(), current_field.end(),
                     current_field.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (current_field == "set-cookie") {
        std::string cookie_pair = current_value;
        size_t semi = cookie_pair.find(';');
        if (semi != std::string::npos) {
          cookie_pair = cookie_pair.substr(0, semi);
        }
        if (!cookie_pair.empty()) {
          auto &existing = response->header["set-cookie"];
          if (!existing.empty()) {
            existing += "; ";
          }
          existing += cookie_pair;
        }
      } else {
        response->header[current_field] = std::move(current_value);
      }
      current_field.clear();
      current_value.clear();
      last_was_value = false;
    }
  } parser_data;
  parser_data.response = response;

  http_parser parser;
  http_parser_settings parser_settings;
  http_parser_init(&parser, HTTP_RESPONSE);
  http_parser_settings_init(&parser_settings);
  parser.data = &parser_data;

  parser_settings.on_header_field = [](http_parser *p, const char *at,
                                       size_t length) -> int {
    auto *d = reinterpret_cast<ParserData *>(p->data);
    d->flush_header();
    d->current_field.append(at, length);
    return 0;
  };

  parser_settings.on_header_value = [](http_parser *p, const char *at,
                                       size_t length) -> int {
    auto *d = reinterpret_cast<ParserData *>(p->data);
    d->current_value.append(at, length);
    d->last_was_value = true;
    return 0;
  };

  parser_settings.on_headers_complete = [](http_parser *p) -> int {
    auto *d = reinterpret_cast<ParserData *>(p->data);
    d->flush_header();
    d->response->statusCode = p->status_code;
    return 0;
  };

  parser_settings.on_body = [](http_parser *p, const char *at,
                               size_t length) -> int {
    auto *d = reinterpret_cast<ParserData *>(p->data);
    if (!NXCordLimits::append(d->response->text, at, length,
                               NXCordLimits::http_wire_bytes)) {
      d->too_large = true;
      return -1;
    }
    return 0;
  };

  parser_settings.on_message_complete = [](http_parser *p) -> int {
    auto *d = reinterpret_cast<ParserData *>(p->data);
    d->message_complete = true;
    return 0;
  };

  while (!parser_data.message_complete) {
    read_len = mbedtls_wrapper->read(buf, buf_size);
    if (read_len == MBEDTLS_ERR_SSL_WANT_READ ||
        read_len == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }
    if (read_len <= 0) {
      break;
    }
    size_t nparsed = http_parser_execute(
        &parser, &parser_settings, reinterpret_cast<const char *>(buf),
        static_cast<size_t>(read_len));
    if (parser.upgrade) {
      if (nparsed < static_cast<size_t>(read_len)) {
        mbedtls_wrapper->unread(buf + nparsed,
                                static_cast<size_t>(read_len) - nparsed);
      }
      break;
    }
    if (HTTP_PARSER_ERRNO(&parser) != HPE_OK) {
      Logger::write("HTTP parser error: %s\n",
                    http_errno_description(HTTP_PARSER_ERRNO(&parser)));
      break;
    }
  }
  if ((read_len == 0 || read_len == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) &&
      !parser.upgrade && !parser_data.message_complete) {
    http_parser_execute(&parser, &parser_settings, nullptr, 0);
  }

  if (!parser.upgrade && (!parser_data.message_complete || parser_data.too_large)) {
    response->statusCode = SleepyDiscord::GENERAL_ERROR;
    response->text = R"({"code":0,"message":"Incomplete or oversized HTTP response"})";
    Logger::write("Rejected incomplete or oversized HTTP response\n");
    _body = nullptr;
    _headers = nullptr;
    return mbedtls_wrapper;
  }

  if (!parser.upgrade) {
    auto it = response->header.find("content-encoding");
    if (it != response->header.end() && it->second == "gzip") {
      Logger::write("Decompressing body\n");

      std::stringstream ss;
      ss << response->text;
      response->text.clear();
      response->text.shrink_to_fit();

      std::string decompressed;
      ZlibWrapper zlib_wrapper;
      zlib_wrapper.set_stream(&ss);
      while ((read_len = zlib_wrapper.read(reinterpret_cast<char *>(buf),
                                           sizeof(buf))) > 0) {
        if (!NXCordLimits::append(decompressed, reinterpret_cast<char *>(buf),
                                   read_len, NXCordLimits::http_body_bytes)) {
          parser_data.too_large = true;
          break;
        }
      }

      response->text = std::move(decompressed);
      if (parser_data.too_large || read_len < 0) {
        response->statusCode = SleepyDiscord::GENERAL_ERROR;
        response->text = R"({"code":0,"message":"Invalid or oversized compressed response"})";
        Logger::write("Rejected invalid or oversized compressed HTTP response\n");
      }
    }
    response->text.shrink_to_fit();
  }

  Logger::write("Session done %s (status %d)\n", _url.c_str(),
                response->statusCode);

  _body = nullptr;
  _headers = nullptr;
  return mbedtls_wrapper;
}

SleepyDiscord::Response DiscordSession::request(
    SleepyDiscord::RequestMethod method) {
  SleepyDiscord::Response response;
  request(method, &response);
  return response;
}
