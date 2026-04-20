#pragma once

/**
 * @file enhanced_auth_registry.h
 * @brief Thread-safe storage for pending and active enhanced-auth contexts.
 */

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "auth/enhanced_auth_handler.h"
#include "data_model/packet/connect_packet.h"

namespace mqtt {

/**
 * @brief In-progress enhanced-auth connect context.
 */
struct PendingEnhancedAuthContext {
  EnhancedAuthHandler handler;
  ConnectPacket connect_packet;
  std::function<void()> close_callback;
  std::optional<std::string> assigned_client_id;
};

/**
 * @brief Thread-safe enhanced-auth context registry.
 */
class EnhancedAuthRegistry {
public:
  /**
   * @brief Erase pending and active context for a client.
   * @param client_id Client identifier.
   */
  void erase_client(std::string_view client_id);

  /**
   * @brief Insert or replace pending context for a client.
   * @param client_id Client identifier.
   * @param context Pending auth context.
   */
  void upsert_pending(std::string client_id, PendingEnhancedAuthContext context);

  /**
   * @brief Insert or replace active context for a client.
   * @param client_id Client identifier.
   * @param handler Active enhanced auth handler.
   */
  void upsert_active(std::string client_id, EnhancedAuthHandler handler);

  /**
   * @brief Erase pending context for a client.
   * @param client_id Client identifier.
   */
  void erase_pending(std::string_view client_id);

  /**
   * @brief Erase active context for a client.
   * @param client_id Client identifier.
   */
  void erase_active(std::string_view client_id);

  /**
   * @brief Execute a callback while holding the registry lock.
   *
   * Use this for multi-step pending/active state transitions that must be
   * serialized across connect/auth/disconnect workflows.
   *
   * @tparam CallbackType Callable signature:
   *         `Result(PendingMap&, ActiveMap&)`.
   * @param callback Callback executed under lock.
   * @return Callback return value.
   */
  template <typename CallbackType>
  decltype(auto) with_lock(CallbackType &&callback) {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    return std::forward<CallbackType>(callback)(pending_enhanced_auth_,
                                                active_enhanced_auth_);
  }

private:
  std::mutex mutex_;
  std::unordered_map<std::string, PendingEnhancedAuthContext>
      pending_enhanced_auth_;
  std::unordered_map<std::string, EnhancedAuthHandler> active_enhanced_auth_;
};

} // namespace mqtt
