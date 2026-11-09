#pragma once

#include <string>

namespace SleepyDiscord {

class JsonInputStream {
 public:
  typedef char Ch;

  [[nodiscard]] virtual Ch Peek() const = 0;
  virtual Ch Take() = 0;
  [[nodiscard]] virtual size_t Tell() const = 0;

  Ch* PutBegin() { return nullptr; }
  void Put(Ch ch) {}
  void Flush() {}
  size_t PutEnd(Ch* ch) { return 0; }

  [[nodiscard]] const Ch* Peek4() const { return nullptr; }

  virtual void SetCache(bool enable) = 0;
  [[nodiscard]] virtual const std::string& GetCache() const = 0;
};

// Limit the SAX reader's temporary string storage too, not just the JSON cache.
// Huge unused settings/protobuf strings in READY must not exhaust the heap.
// The underlying stream still consumes/caches the original bytes and preserves
// sequence fields after a long string, regardless of JSON field order.
class JsonTokenStream : public JsonInputStream {
  JsonInputStream &source;
  size_t bytes = 0;
  bool in_string = false, escaped = false;
  unsigned unicode_left = 0;
  unsigned unicode_value = 0;
  bool high_surrogate = false;

  bool shorten() const {
    const auto next = static_cast<unsigned char>(source.Peek());
    return in_string && bytes >= 8192 && !escaped && unicode_left == 0 && !high_surrogate &&
           (next & 0xC0) != 0x80;
  }

 public:
  explicit JsonTokenStream(JsonInputStream &input) : source(input) {}
  Ch Peek() const override { return shorten() ? '"' : source.Peek(); }
  Ch Take() override {
    if (shorten()) {
      bool escape = false;
      for (char c; (c = source.Take()) != '\0';) {
        if (escape) escape = false;
        else if (c == '\\') escape = true;
        else if (c == '"') break;
      }
      in_string = false;
      return '"';
    }
    const char c = source.Take();
    if (!in_string) {
      if (c == '"') {
        in_string = true; bytes = 0; escaped = false;
        unicode_left = 0; high_surrogate = false;
      }
    } else {
      ++bytes;
      if (unicode_left) {
        unicode_value = (unicode_value << 4) |
            (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10);
        if (--unicode_left == 0) high_surrogate = unicode_value >= 0xD800 && unicode_value <= 0xDBFF;
      }
      else if (escaped) {
        escaped = false;
        if (c == 'u') { unicode_left = 4; unicode_value = 0; }
      }
      else if (c == '\\') escaped = true;
      else if (c == '"') in_string = false;
    }
    return c;
  }
  size_t Tell() const override { return source.Tell(); }
  void SetCache(bool enable) override { source.SetCache(enable); }
  const std::string &GetCache() const override { return source.GetCache(); }
};
}  // namespace SleepyDiscord
