#pragma once

#include <algorithm>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace UIBrowser {
inline size_t range_index(float value, float step, size_t count) {
  if (count == 0 || step <= 0) return 0;
  if (!std::isfinite(value)) value = step;
  value = std::clamp(value, step, step * static_cast<float>(count));
  return std::min(count - 1, static_cast<size_t>(std::lround(value / step) - 1));
}
inline std::string lower(std::string value) {
  for (char &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
template <typename Row>
std::vector<Row> filter(std::vector<Row> rows, const std::string &query) {
  const std::string needle = lower(query);
  rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto &row) {
    return lower(row.name).find(needle) == std::string::npos;
  }), rows.end());
  std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
    return lower(a.name) < lower(b.name);
  });
  return rows;
}
template <typename Row>
bool equal(const std::vector<Row> &a, const std::vector<Row> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].id != b[i].id || std::strcmp(a[i].name, b[i].name) != 0) return false;
  return true;
}
template <typename Row>
int selection(const std::vector<Row> &rows, int64_t id) {
  auto found = std::find_if(rows.begin(), rows.end(), [id](const auto &row) { return row.id == id; });
  return found == rows.end() ? 0 : static_cast<int>(found - rows.begin());
}
}  // namespace UIBrowser
