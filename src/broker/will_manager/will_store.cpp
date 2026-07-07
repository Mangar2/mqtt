#include "will_manager/will_store.h"

#include <string>

namespace mqtt {

void WillStore::store(std::string_view client_id, const WillMessage &will) {
  std::lock_guard<std::mutex> lock(mutex_);
  wills_.insert_or_assign(std::string(client_id), will);
}

std::optional<WillMessage> WillStore::load(std::string_view client_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto itr = wills_.find(std::string(client_id));
  if (itr == wills_.end()) {
    return std::nullopt;
  }
  return itr->second;
}

void WillStore::remove(std::string_view client_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  wills_.erase(std::string(client_id));
}

std::size_t WillStore::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return wills_.size();
}

} // namespace mqtt
