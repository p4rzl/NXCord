#pragma once

#include <sleepy_discord/message_receiver.h>
#include <sleepy_discord/websocket.h>
#include <wslay/wslay.h>

#include <common/utils.hpp>
#include <memory>
#include <vector>

#include "mbedtls_wrapper.hpp"
#include "zlib_wrapper.hpp"

class DiscordWebsocket : public SleepyDiscord::GenericWebsocketConnection {
 private:
  static constexpr char zlib_suffix[] = {0x0, 0x0, 0xff, 0xff};

  SleepyDiscord::GenericMessageReceiver *_message_processor;
  std::unique_ptr<MBedTLSWrapper> _mbedtls_wrapper;

  bool _zlib_compress;
  std::ofstream _zlib_buf;
  ZlibWrapper _zlib_wrapper;
  bool _frame_is_data = false;
  size_t _compressed_bytes = 0;
  uint8_t _compressed_tail[4]{};
  int _pending_close = 0;

  wslay_event_context_ptr _wslay_event_context = nullptr;
  wslay_event_callbacks _wslay_event_callbacks{};

  bool pollSocket(uint16_t events);

  friend ssize_t recv_callback(wslay_event_context_ptr ctx, uint8_t *buf,
                               size_t len, int flags, void *user_data);

  friend ssize_t send_callback(wslay_event_context_ptr ctx, const uint8_t *data,
                               size_t len, int flags, void *user_data);

  friend int genmask_callback(wslay_event_context_ptr ctx, uint8_t *buf,
                              size_t len, void *user_data);

  friend void on_msg_recv_callback(wslay_event_context_ptr ctx,
                                   const wslay_event_on_msg_recv_arg *arg,
                                    void *user_data);

  friend void on_frame_start_callback(wslay_event_context_ptr,
      const wslay_event_on_frame_recv_start_arg *, void *);
  friend void on_frame_chunk_callback(wslay_event_context_ptr,
      const wslay_event_on_frame_recv_chunk_arg *, void *);

 public:
  DiscordWebsocket(SleepyDiscord::GenericMessageReceiver *message_processor,
                   std::unique_ptr<MBedTLSWrapper> &mbedtls_wrapper,
                   bool zlib_compress = false);

  ~DiscordWebsocket() override;

  int queue_message(const std::string &message, bool binary = false);

  void disconnect(unsigned int, const std::string &);

  void tick();
};
