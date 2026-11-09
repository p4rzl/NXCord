#include <common/logger.hpp>
#include <nxcord/zlib_wrapper.hpp>

ZlibWrapper::ZlibWrapper() {
  _infstream.zalloc = Z_NULL;
  _infstream.zfree = Z_NULL;

  inflateInit2(&_infstream, MAX_WBITS + 32);
}

ZlibWrapper::~ZlibWrapper() { inflateEnd(&_infstream); }

void ZlibWrapper::set_stream(std::istream *stream) {
  _stream = stream;

  _infstream.avail_in = 0;
  _infstream.total_in = 0;

  _infstream.total_out = 0;

  stream->clear();
  stream->seekg(0);
}

int64_t ZlibWrapper::read(char *buf, size_t size) {
  _infstream.avail_out = size;
  _infstream.next_out = reinterpret_cast<Bytef *>(buf);

  while (_infstream.avail_out == size) {
    if (_infstream.avail_in == 0) {
      _stream->read(_in_buf, sizeof(_in_buf));
      _stream_read_size = _stream->gcount();
      if (_stream_read_size <= 0) {
        return 0;
      }
      _infstream.avail_in = _stream_read_size;
    }

    _infstream.next_in = reinterpret_cast<Bytef *>(
        _in_buf + _stream_read_size - _infstream.avail_in);

    int ret = inflate(&_infstream, Z_SYNC_FLUSH);
    if (ret == Z_STREAM_END) {
      break;
    }
    if (ret != Z_OK && ret != Z_BUF_ERROR) {
      Logger::write("Failed to decompress zlib %d %s\n", ret,
                    _infstream.msg ? _infstream.msg : "");
      return ret;
    }
    if (ret == Z_BUF_ERROR && _infstream.avail_in != 0) {
      break;
    }
  }
  return size - _infstream.avail_out;
}
