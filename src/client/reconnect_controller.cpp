#include "client/reconnect_controller.h"

#include <algorithm>
#include <utility>

#include "client/client_error.h"

namespace mqtt {

ReconnectController::ReconnectController(ReconnectBackoffPolicy backoff_policy)
    : backoff_policy_(backoff_policy) {
  backoff_policy_.initial_delay =
      sanitize_initial_delay(backoff_policy_.initial_delay);
  backoff_policy_.max_delay = sanitize_max_delay(backoff_policy_.max_delay);
  backoff_policy_.multiplier = sanitize_multiplier(backoff_policy_.multiplier);
  current_delay_ = backoff_policy_.initial_delay;
}

void ReconnectController::set_negotiate_callback(
    NegotiateFn negotiate_callback) noexcept {
  negotiate_callback_ = std::move(negotiate_callback);
}

void ReconnectController::set_restore_session_callback(
    RestoreSessionFn restore_session_callback) noexcept {
  restore_session_callback_ = std::move(restore_session_callback);
}

void ReconnectController::set_restore_qos_callback(
    RestoreQosFn restore_qos_callback) noexcept {
  restore_qos_callback_ = std::move(restore_qos_callback);
}

void ReconnectController::mark_connected() noexcept {
  state_ = ReconnectState::Connected;
  failed_attempts_ = 0U;
  current_delay_ = backoff_policy_.initial_delay;
  next_retry_at_.reset();
  last_error_message_.reset();
}

void ReconnectController::on_connection_lost(ReconnectTrigger trigger,
                                             Clock::time_point now) noexcept {
  if (trigger == ReconnectTrigger::UserInitiated) {
    state_ = ReconnectState::Disabled;
    next_retry_at_.reset();
    return;
  }

  state_ = ReconnectState::WaitingForRetry;
  next_retry_at_ = now + current_delay_;
}

ReconnectTickResult ReconnectController::tick(Clock::time_point now) {
  ReconnectTickResult tick_result;
  if (!should_attempt_reconnect(now)) {
    tick_result.next_retry_at = next_retry_at_;
    if (last_error_message_.has_value()) {
      tick_result.error_message = last_error_message_;
    }
    return tick_result;
  }

  state_ = ReconnectState::Reconnecting;
  tick_result.attempted = true;

  if (!negotiate_callback_) {
    state_ = ReconnectState::WaitingForRetry;
    last_error_message_ = "reconnect negotiate callback is not configured";
    schedule_next_retry(now);
    tick_result.next_retry_at = next_retry_at_;
    tick_result.error_message = last_error_message_;
    return tick_result;
  }

  try {
    const ConnectionNegotiationResult negotiation_result = negotiate_callback_();
    if (restore_session_callback_) {
      restore_session_callback_(negotiation_result);
    }
    if (restore_qos_callback_) {
      restore_qos_callback_();
    }
    mark_connected();
    tick_result.reconnected = true;
    return tick_result;
  } catch (const std::exception &exception) {
    state_ = ReconnectState::WaitingForRetry;
    last_error_message_ = exception.what();
    schedule_next_retry(now);
    tick_result.next_retry_at = next_retry_at_;
    tick_result.error_message = last_error_message_;
    return tick_result;
  }
}

ReconnectState ReconnectController::state() const noexcept { return state_; }

std::size_t ReconnectController::failed_attempts() const noexcept {
  return failed_attempts_;
}

std::chrono::milliseconds ReconnectController::current_delay() const noexcept {
  return current_delay_;
}

std::optional<ReconnectController::Clock::time_point>
ReconnectController::next_retry_at() const noexcept {
  return next_retry_at_;
}

std::optional<std::string> ReconnectController::last_error_message() const {
  return last_error_message_;
}

std::chrono::milliseconds ReconnectController::sanitize_initial_delay(
    std::chrono::milliseconds delay) const noexcept {
  return std::max(delay, std::chrono::milliseconds::zero());
}

std::chrono::milliseconds ReconnectController::sanitize_max_delay(
    std::chrono::milliseconds delay) const noexcept {
  return std::max(delay, backoff_policy_.initial_delay);
}

double ReconnectController::sanitize_multiplier(double value) const noexcept {
  return std::max(value, 1.0);
}

void ReconnectController::schedule_next_retry(Clock::time_point now) noexcept {
  ++failed_attempts_;
  next_retry_at_ = now + current_delay_;

  const auto next_delay_count = static_cast<long long>(
      static_cast<double>(current_delay_.count()) * backoff_policy_.multiplier);
  const auto bounded_next_delay = std::chrono::milliseconds{std::max(0LL, next_delay_count)};
  current_delay_ = std::min(bounded_next_delay, backoff_policy_.max_delay);
}

bool ReconnectController::should_attempt_reconnect(Clock::time_point now) const noexcept {
  if (state_ != ReconnectState::WaitingForRetry) {
    return false;
  }
  if (!next_retry_at_.has_value()) {
    return false;
  }
  return now >= *next_retry_at_;
}

} // namespace mqtt
