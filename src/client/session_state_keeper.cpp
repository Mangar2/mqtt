#include "client/session_state_keeper.h"

#include <algorithm>
#include <utility>

#include "client/client_error.h"
#include "data_model/session/inflight_direction.h"
#include "store/inflight_store.h"

namespace mqtt {

ClientSessionStateKeeper::ClientSessionStateKeeper(
    std::string client_id, uint32_t session_expiry_interval)
    : session_state_{.client_id = Utf8String{std::move(client_id)},
                     .subscriptions = {},
                     .session_expiry_interval = session_expiry_interval},
      outbound_inflight_{} {}

std::string_view ClientSessionStateKeeper::client_id() const noexcept {
  return session_state_.client_id.value;
}

void ClientSessionStateKeeper::set_session_expiry_interval(
    uint32_t session_expiry_interval) noexcept {
  session_state_.session_expiry_interval = session_expiry_interval;
}

uint32_t ClientSessionStateKeeper::session_expiry_interval() const noexcept {
  return session_state_.session_expiry_interval;
}

void ClientSessionStateKeeper::upsert_subscription(
    const Subscription &subscription) {
  const auto subscription_iter = std::find_if(
      session_state_.subscriptions.begin(), session_state_.subscriptions.end(),
      [&subscription](const Subscription &stored_subscription) {
        return stored_subscription.topic_filter == subscription.topic_filter;
      });

  if (subscription_iter != session_state_.subscriptions.end()) {
    *subscription_iter = subscription;
    return;
  }

  session_state_.subscriptions.push_back(subscription);
}

bool ClientSessionStateKeeper::remove_subscription(
    std::string_view topic_filter) noexcept {
  const auto erase_begin = std::remove_if(
      session_state_.subscriptions.begin(), session_state_.subscriptions.end(),
      [topic_filter](const Subscription &subscription) {
        return subscription.topic_filter.value == topic_filter;
      });

  if (erase_begin == session_state_.subscriptions.end()) {
    return false;
  }

  session_state_.subscriptions.erase(erase_begin,
                                     session_state_.subscriptions.end());
  return true;
}

void ClientSessionStateKeeper::clear_subscriptions() noexcept {
  session_state_.subscriptions.clear();
}

const std::vector<Subscription> &ClientSessionStateKeeper::subscriptions()
    const noexcept {
  return session_state_.subscriptions;
}

void ClientSessionStateKeeper::set_outbound_inflight(
    std::vector<InflightEntry> entries) {
  outbound_inflight_.clear();
  outbound_inflight_.reserve(entries.size());

  for (InflightEntry &entry : entries) {
    if (entry.direction != InflightDirection::Outbound) {
      continue;
    }
    if (entry.packet_id == 0U) {
      continue;
    }
    outbound_inflight_.push_back(std::move(entry));
  }

  sort_outbound_inflight();
}

void ClientSessionStateKeeper::capture_outbound_inflight(
    const InflightStore &inflight_store) {
  std::vector<InflightEntry> captured_entries;
  inflight_store.for_each(
      session_state_.client_id.value, InflightDirection::Outbound,
      [&captured_entries](const InflightEntry &entry) {
        captured_entries.push_back(entry);
      });
  set_outbound_inflight(std::move(captured_entries));
}

const std::vector<InflightEntry> &
ClientSessionStateKeeper::outbound_inflight() const noexcept {
  return outbound_inflight_;
}

ClientSessionRestorePlan ClientSessionStateKeeper::build_restore_plan(
    bool clean_start) const {
  if (clean_start) {
    return ClientSessionRestorePlan{};
  }

  return ClientSessionRestorePlan{
      .subscriptions = session_state_.subscriptions,
      .outbound_inflight = outbound_inflight_,
      .session_expiry_interval = session_state_.session_expiry_interval,
  };
}

ClientSessionSnapshot ClientSessionStateKeeper::snapshot() const {
  return ClientSessionSnapshot{
      .session_state = session_state_,
      .outbound_inflight = outbound_inflight_,
  };
}

void ClientSessionStateKeeper::apply_snapshot(
    const ClientSessionSnapshot &snapshot) {
  if (snapshot.session_state.client_id.value != session_state_.client_id.value) {
    throw ClientException(
        ClientError::InvalidPacket,
        "snapshot client_id does not match session-state keeper client_id");
  }

  session_state_ = snapshot.session_state;
  set_outbound_inflight(snapshot.outbound_inflight);
}

void ClientSessionStateKeeper::sort_outbound_inflight() noexcept {
  std::sort(outbound_inflight_.begin(), outbound_inflight_.end(),
            [](const InflightEntry &left_entry, const InflightEntry &right_entry) {
              if (left_entry.timestamp != right_entry.timestamp) {
                return left_entry.timestamp < right_entry.timestamp;
              }
              return left_entry.packet_id < right_entry.packet_id;
            });
}

} // namespace mqtt
