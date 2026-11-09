#pragma once

#include <common/ipc_structures.hpp>
#include <sleepy_discord/rapidjson/memorystream.h>
#include <sleepy_discord/rapidjson/reader.h>
#include <limits>
#include <algorithm>
#include <type_traits>
#include <vector>

#include "memory_limits.hpp"

// REST list responses contain large permission/role/member objects. Retain only
// IDs, names and channel types, without constructing a DOM or Sleepy models.
namespace DiscordLists {
inline int64_t snowflake(const char *value, size_t length) {
  int64_t result = 0;
  for (size_t i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9' ||
        result > (std::numeric_limits<int64_t>::max() - (value[i] - '0')) / 10)
      return 0;
    result = result * 10 + value[i] - '0';
  }
  return result;
}

template <typename Item>
struct Handler : rapidjson::BaseReaderHandler<rapidjson::UTF8<>, Handler<Item>> {
  std::vector<Item> &items;
  int64_t server_id;
  size_t limit;
  unsigned depth = 0;
  bool array_root = false;
  std::string key, name;
  int64_t id = 0;
  int type = -1;

  Handler(std::vector<Item> &out, int64_t guild, size_t max)
      : items(out), server_id(guild), limit(max) {}

  bool StartArray() {
    if (depth == 0) array_root = true;
    ++depth;
    return true;
  }
  bool EndArray(size_t) { --depth; return true; }
  bool StartObject() {
    ++depth;
    if (depth == 2) { id = 0; name.clear(); type = -1; }
    return true;
  }
  bool EndObject(size_t) {
    if (depth == 2 && id != 0 && items.size() < limit) {
      if constexpr (std::is_same_v<Item, IPCStruct::DiscordServer>) {
        items.push_back(IPCStruct::create_discord_server(name, id));
      } else if (type == static_cast<int>(IPCStruct::DiscordChannelType::SERVER_VOICE)) {
        items.push_back(IPCStruct::create_discord_channel(
            name, server_id, id, IPCStruct::DiscordChannelType::SERVER_VOICE));
      }
    }
    --depth;
    return true;
  }
  bool Key(const char *value, size_t length, bool) {
    if (depth == 2) key.assign(value, std::min<size_t>(length, 64));
    return true;
  }
  bool String(const char *value, size_t length, bool) {
    if (depth == 2) {
      if (key == "id") id = snowflake(value, length);
      else if (key == "name") name.assign(value, std::min<size_t>(length, 512));
    }
    return true;
  }
  bool Int(int value) { if (depth == 2 && key == "type") type = value; return true; }
  bool Uint(unsigned value) { return Int(value > 100 ? -1 : static_cast<int>(value)); }
};

template <typename Item>
bool parse(const std::string &json, std::vector<Item> &items, size_t limit,
           int64_t server_id = 0) {
  items.clear();
  Handler<Item> handler(items, server_id, limit);
  rapidjson::MemoryStream stream(json.data(), json.size());
  rapidjson::Reader reader;
  if (!reader.Parse(stream, handler) || !handler.array_root) {
    items.clear();
    return false;
  }
  return true;
}
}  // namespace DiscordLists
