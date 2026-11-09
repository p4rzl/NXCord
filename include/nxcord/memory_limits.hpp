#pragma once

#include <cstddef>
#include <string>

// Resource budgets are independent of the number of guilds/events in an account.
namespace NXCordLimits {
constexpr size_t http_wire_bytes = 1024 * 1024;
constexpr size_t http_body_bytes = 2 * 1024 * 1024;
constexpr size_t cached_guilds = 2;
constexpr size_t channels_per_guild = 500;
constexpr size_t voice_channels = 32;
constexpr size_t users_per_channel = 100;

inline bool append(std::string &target, const char *data, size_t size,
                   size_t limit) {
  if (target.size() > limit || size > limit - target.size()) return false;
  target.append(data, size);
  return true;
}

// Keep the active voice guild/channel when evicting unused cache entries.
template <typename Map, typename Key>
bool make_room(Map &cache, const Key &key, const Key &active, size_t limit) {
  if (cache.find(key) != cache.end()) return true;
  while (cache.size() >= limit) {
    auto victim = cache.begin();
    while (victim != cache.end() && victim->first == active) ++victim;
    if (victim == cache.end()) return false;
    cache.erase(victim);
  }
  return true;
}
}  // namespace NXCordLimits
