#include "connection/topic_alias_table.h"

#include <utility>

#include "connection/connection_error.h"

namespace mqtt {

TopicAliasTable::TopicAliasTable(uint16_t max_aliases) noexcept
    : max_aliases_(max_aliases) {}

void TopicAliasTable::validate_alias(uint16_t alias) const {
  if (alias == 0U || alias > max_aliases_) {
    throw ConnectionException(
        ConnectionError::AliasOutOfRange,
        "Topic alias value out of permitted range [1, max_aliases]");
  }
}

void TopicAliasTable::set_inbound(uint16_t alias, std::string topic) {
  validate_alias(alias);
  inbound_.insert_or_assign(alias, std::move(topic));
}

const std::string &TopicAliasTable::get_inbound(uint16_t alias) const {
  validate_alias(alias);
  auto iter = inbound_.find(alias);
  if (iter == inbound_.end()) {
    throw ConnectionException(
        ConnectionError::AliasNotFound,
        "No inbound topic alias mapping found for given alias");
  }
  return iter->second;
}

void TopicAliasTable::set_outbound(std::string topic, uint16_t alias) {
  validate_alias(alias);
  outbound_.insert_or_assign(std::move(topic), alias);
}

std::optional<uint16_t>
TopicAliasTable::get_outbound(std::string_view topic) const {
  auto iter = outbound_.find(std::string(topic));
  if (iter == outbound_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

void TopicAliasTable::reset() noexcept {
  inbound_.clear();
  outbound_.clear();
}

uint16_t TopicAliasTable::max_aliases() const noexcept { return max_aliases_; }

} // namespace mqtt
