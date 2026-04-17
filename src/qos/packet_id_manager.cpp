#include "qos/packet_id_manager.h"

#include <limits>

#include "qos/qos_error.h"

namespace mqtt {

namespace {

/// Maximum allowed Packet Identifier per the MQTT 5.0 specification
/// (Section 2.2.1).
constexpr uint16_t k_max_packet_id = std::numeric_limits<uint16_t>::max();

} // anonymous namespace

// ──────────────────────────────────────────────────────────────────────────────
// PacketIdManager — public API

uint16_t PacketIdManager::allocate() {
  for (uint16_t idx = 0; idx < k_max_packet_id; ++idx) {
    last_id_ = static_cast<uint16_t>((last_id_ % k_max_packet_id) + 1U);
    if (!outbound_ids_.contains(last_id_)) {
      outbound_ids_.insert(last_id_);
      return last_id_;
    }
  }
  throw QosException(QosError::PacketIdExhausted,
                     "all outbound packet IDs are currently in use");
}

bool PacketIdManager::try_register_inbound(uint16_t pid) {
  return inbound_ids_.insert(pid).second;
}

void PacketIdManager::release(uint16_t pid, InflightDirection dir) noexcept {
  if (dir == InflightDirection::Outbound) {
    outbound_ids_.erase(pid);
  } else {
    inbound_ids_.erase(pid);
  }
}

bool PacketIdManager::is_in_use(uint16_t pid,
                                InflightDirection dir) const noexcept {
  if (dir == InflightDirection::Outbound) {
    return outbound_ids_.contains(pid);
  }
  return inbound_ids_.contains(pid);
}

std::size_t PacketIdManager::outbound_count() const noexcept {
  return outbound_ids_.size();
}

std::size_t PacketIdManager::inbound_count() const noexcept {
  return inbound_ids_.size();
}

} // namespace mqtt
