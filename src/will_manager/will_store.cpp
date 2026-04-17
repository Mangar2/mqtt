#include "will_manager/will_store.h"

#include <string>

namespace mqtt {

void WillStore::store(std::string_view client_id, const WillMessage &will) {
  wills_.insert_or_assign(std::string(client_id), will);
}

std::optional<WillMessage> WillStore::load(std::string_view client_id) const {
  const auto itr = wills_.find(std::string(client_id));
  if (itr == wills_.end()) {
    return std::nullopt;
  }
  return itr->second;
}

void WillStore::remove(std::string_view client_id) {
  wills_.erase(std::string(client_id));
}

std::size_t WillStore::size() const noexcept { return wills_.size(); }

} // namespace mqtt
