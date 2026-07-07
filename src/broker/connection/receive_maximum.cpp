#include "connection/receive_maximum.h"

#include "connection/connection_error.h"

namespace mqtt {

namespace {

constexpr uint16_t k_default_max = 65535U;

} // namespace

ReceiveMaximum::ReceiveMaximum(uint16_t max) noexcept
    : max_(max == 0U ? k_default_max : max) {}

bool ReceiveMaximum::acquire() noexcept {
  if (inflight_ >= max_) {
    return false;
  }
  ++inflight_;
  return true;
}

void ReceiveMaximum::release() {
  if (inflight_ == 0U) {
    throw ConnectionException(
        ConnectionError::InvalidState,
        "release() called with no inflight packets outstanding");
  }
  --inflight_;
}

bool ReceiveMaximum::is_paused() const noexcept { return inflight_ >= max_; }

uint16_t ReceiveMaximum::available() const noexcept {
  return static_cast<uint16_t>(max_ - inflight_);
}

uint16_t ReceiveMaximum::max() const noexcept { return max_; }

} // namespace mqtt
