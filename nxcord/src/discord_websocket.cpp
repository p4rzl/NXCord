#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <switch.h>

#include <common/logger.hpp>
#include <cstring>
#include <nxcord/discord_websocket.hpp>
#include <nxcord/websocket_zlib_stream.hpp>
#include <sstream>

ssize_t recv_callback(wslay_event_context_ptr ctx, uint8_t *buf, size_t len,
                      int, void *user_data) {
  auto discord_websocket = static_cast<DiscordWebsocket *>(user_data);

  int ret = discord_websocket->_mbedtls_wrapper->read(buf, len);
  if (ret < 0) {
    if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
        ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ((ret == -1 || ret == MBEDTLS_ERR_NET_RECV_FAILED) &&
         (errno == EAGAIN || errno == EWOULDBLOCK))) {
      errno = EWOULDBLOCK;
      wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
    } else {
      Logger::write("Read fail: %d\n", ret);
      wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
    return -1;
  } else if (ret == 0) {
    wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return ret;
}

ssize_t send_callback(wslay_event_context_ptr ctx, const uint8_t *data,
                      size_t len, int, void *user_data) {
  auto discord_websocket = static_cast<DiscordWebsocket *>(user_data);

  int ret = discord_websocket->_mbedtls_wrapper->write(data, len);
  if (ret < 0) {
    if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
        ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ((ret == -1 || ret == MBEDTLS_ERR_NET_SEND_FAILED) &&
         (errno == EAGAIN || errno == EWOULDBLOCK))) {
      errno = EWOULDBLOCK;
      wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
    } else {
      Logger::write("Write fail: %d\n", ret);
      wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
    return -1;
  } else if (ret == 0) {
    wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return ret;
}

int genmask_callback(wslay_event_context_ptr, uint8_t *buf, size_t len,
                     void *) {
  if (R_FAILED(csrngGetRandomBytes(buf, len))) {
    Logger::write("Failed to generate random bytes\n");
    return -1;
  }
  return 0;
}

void on_frame_start_callback(wslay_event_context_ptr,
    const wslay_event_on_frame_recv_start_arg *arg, void *user_data) {
  auto websocket = static_cast<DiscordWebsocket *>(user_data);
  websocket->_frame_is_data = !wslay_is_ctrl_frame(arg->opcode);
}

void on_frame_chunk_callback(wslay_event_context_ptr ctx,
    const wslay_event_on_frame_recv_chunk_arg *arg, void *user_data) {
  auto websocket = static_cast<DiscordWebsocket *>(user_data);
  if (!websocket->_zlib_compress || !websocket->_frame_is_data ||
      websocket->_pending_close || arg->data_length == 0) return;
  auto &cache = websocket->_zlib_buf;
  if (!cache.is_open()) {
    cache.open(LOG_PATH "/.websocket_cache", std::ios::binary | std::ios::trunc);
    websocket->_compressed_bytes = 0;
  }
  // Spool large READY frames directly to SD rather than letting wslay retain
  // a second, account-sized copy in the sysmodule heap.
  constexpr size_t max_compressed_bytes = 8 * 1024 * 1024;
  if (arg->data_length > max_compressed_bytes - websocket->_compressed_bytes) {
    websocket->_pending_close = 1006;
  } else {
    cache.write(reinterpret_cast<const char *>(arg->data), arg->data_length);
    websocket->_compressed_bytes += arg->data_length;
    if (!cache.good()) websocket->_pending_close = 1006;
  }
  if (websocket->_pending_close) {
    Logger::write("Gateway cache write failed or compressed payload too large\n");
    wslay_event_shutdown_read(ctx);
    return;
  }
  const size_t tail_size = std::min<size_t>(4, arg->data_length);
  std::memmove(websocket->_compressed_tail, websocket->_compressed_tail + tail_size,
               4 - tail_size);
  std::memcpy(websocket->_compressed_tail + 4 - tail_size,
              arg->data + arg->data_length - tail_size, tail_size);
}

void on_msg_recv_callback(wslay_event_context_ptr,
                          const wslay_event_on_msg_recv_arg *arg,
                          void *user_data) {
  auto discord_websocket = static_cast<DiscordWebsocket *>(user_data);
  if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
    Logger::write("Websocket closed with status %d\n", arg->status_code);
    // Do not free the wslay context while its receive callback is on-stack.
    discord_websocket->_pending_close = arg->status_code != 0 ? arg->status_code : 1000;
    return;
  }
  if (wslay_is_ctrl_frame(arg->opcode)) {
    return;
  }
  if (!discord_websocket->_zlib_compress && arg->opcode == WSLAY_BINARY_FRAME) {
    discord_websocket->_message_processor->processBinaryMessage(arg->msg, arg->msg_length);
    return;
  }

  const char *msg = reinterpret_cast<const char *>(arg->msg);
  std::string str_msg;

  if (discord_websocket->_zlib_compress) {
    std::ofstream &zlib_buf = discord_websocket->_zlib_buf;
    if (zlib_buf.is_open() && discord_websocket->_compressed_bytes >= 4 &&
        !discord_websocket->_pending_close &&
        std::memcmp(discord_websocket->_compressed_tail,
                     DiscordWebsocket::zlib_suffix, 4) == 0) {
      Logger::write("Decompressing websocket\n");
      zlib_buf.close();

      std::ifstream read_stream(LOG_PATH "/.websocket_cache", std::ios::binary);
      if (!read_stream.good()) {
        discord_websocket->_pending_close = 1006;
        return;
      }
      discord_websocket->_zlib_wrapper.set_stream(&read_stream);
      WebsocketZlibStream websocket_zlib_stream(
          discord_websocket->_zlib_wrapper);
      discord_websocket->_message_processor->processStream(
          websocket_zlib_stream);
      while (websocket_zlib_stream.Take() !=
             '\0')  // read rest of stream to satisfy zlib
        ;
      read_stream.close();
    }
  } else {
    str_msg = std::string(msg, msg + arg->msg_length);
  }

  if (!str_msg.empty() && (str_msg[0] == '{' || str_msg[0] == '[')) {
    discord_websocket->_message_processor->processMessage(str_msg);
    Logger::write("Message processed\n");
  }
}

DiscordWebsocket::DiscordWebsocket(
    SleepyDiscord::GenericMessageReceiver *message_processor,
    std::unique_ptr<MBedTLSWrapper> &mbedtls_wrapper, bool zlib_compress)
    : _message_processor(message_processor),
      _mbedtls_wrapper(std::move(mbedtls_wrapper)),
      _zlib_compress(zlib_compress) {
  // Make fd non blocking
  int flags, r;
  while ((flags = fcntl(_mbedtls_wrapper->getFd(), F_GETFL, 0)) == -1 &&
         errno == EINTR)
    ;
  while ((r = fcntl(_mbedtls_wrapper->getFd(), F_SETFL, flags | O_NONBLOCK)) ==
             -1 &&
         errno == EINTR)
    ;
  NXC_ASSERT(flags != -1 && r != -1);

  _wslay_event_callbacks = {
      recv_callback, send_callback, genmask_callback, on_frame_start_callback,
      on_frame_chunk_callback, nullptr, on_msg_recv_callback,
  };

  wslay_event_context_client_init(&_wslay_event_context,
                                   &_wslay_event_callbacks, this);
  wslay_event_config_set_no_buffering(_wslay_event_context, _zlib_compress);
  wslay_event_config_set_max_recv_msg_length(_wslay_event_context,
      _zlib_compress ? 8 * 1024 * 1024 : 256 * 1024);
}

DiscordWebsocket::~DiscordWebsocket() { disconnect(1000, ""); }

int DiscordWebsocket::queue_message(const std::string &message, bool binary) {
  if (!_wslay_event_context) {
    return -1;
  }
  wslay_event_msg msg = {static_cast<uint8_t>(binary ? WSLAY_BINARY_FRAME : WSLAY_TEXT_FRAME), reinterpret_cast<const uint8_t *>(message.c_str()),
                         message.size()};
  Logger::write("Send websocket message (%zu bytes)\n", message.size());
  return wslay_event_queue_msg(_wslay_event_context, &msg);
}

void DiscordWebsocket::disconnect(unsigned int code,
                                  const std::string &reason) {
  if (_wslay_event_context) {
    wslay_event_queue_close(_wslay_event_context, code,
                            reinterpret_cast<const uint8_t *>(reason.c_str()),
                            reason.length());
    wslay_event_send(_wslay_event_context);
    wslay_event_context_free(_wslay_event_context);
    _wslay_event_context = nullptr;
  }
}

bool DiscordWebsocket::pollSocket(uint16_t events) {
  pollfd pol{};
  pol.fd = _mbedtls_wrapper->getFd();
  pol.events = events;

  if (poll(&pol, 1, 0) == 1) {
    return pol.revents & events;
  }
  return false;
}

void DiscordWebsocket::tick() {
  if (!_wslay_event_context) {
    return;
  }
  int ret;
  if (wslay_event_want_read(_wslay_event_context) &&
      (_mbedtls_wrapper->hasPendingData() || pollSocket(POLLIN))) {
    if ((ret = wslay_event_recv(_wslay_event_context)) != 0) {
      Logger::write("Wslay read failed %d\n", ret);
      _message_processor->processCloseCode(_pending_close ? _pending_close : 1006);
      return;
    }
  }

  if (_pending_close) {
    const int code = _pending_close;
    _pending_close = 0;
    _message_processor->processCloseCode(code);
    return;
  }

  if (!_wslay_event_context) {
    return;
  }
  if (wslay_event_want_write(_wslay_event_context) && pollSocket(POLLOUT)) {
    if ((ret = wslay_event_send(_wslay_event_context)) != 0) {
      Logger::write("Wslay write failed %d\n", ret);
      _message_processor->processCloseCode(1006);
    }
  }
}
