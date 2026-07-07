#pragma once

/**
 * @file subscription_orchestrator.h
 * @brief Orchestrates MQTT SUBSCRIBE and UNSUBSCRIBE flows for Broker.
 */

#include <functional>
#include <mutex>
#include <string_view>

#include "authz/acl_engine.h"
#include "data_model/packet/subscribe_packets.h"
#include "message_router/message_router.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "store/session_store.h"
#include "store/subscription_store.h"

namespace mqtt {

/**
 * @brief Encapsulates SUBSCRIBE/UNSUBSCRIBE orchestration for broker facade calls.
 *
 * The class validates shared-subscription syntax and protocol constraints,
 * performs ACL checks, updates subscription state stores/dispatchers, and
 * produces SUBACK/UNSUBACK packets with one reason code per filter.
 */
class SubscriptionOrchestrator {
public:
  /**
   * @brief Create an orchestrator bound to broker-owned collaborators.
   *
   * @param acl_engine ACL engine used for subscribe authorization checks.
  * @param session_store Store for session-state snapshots used by persistence.
   * @param subscription_store Store for regular (non-shared) subscriptions.
   * @param shared_dispatcher Dispatcher for shared subscription membership.
   * @param message_router Message router for retained-message delivery.
   */
  SubscriptionOrchestrator(AclEngine &acl_engine,
                  SessionStore &session_store,
                           SubscriptionStore &subscription_store,
                           SharedSubscriptionDispatcher &shared_dispatcher,
                           MessageRouter &message_router) noexcept;

  /**
   * @brief Handle a complete SUBSCRIBE packet and build SUBACK results.
   *
   * @param client_id Subscribing client identifier.
   * @param packet Incoming SUBSCRIBE packet.
   * @return SUBACK with one reason code per requested filter.
   */
  [[nodiscard]] SubackPacket handle_subscribe(std::string_view client_id,
                                              const SubscribePacket &packet);

  /**
   * @brief Handle a complete UNSUBSCRIBE packet and build UNSUBACK results.
   *
   * @param client_id Unsubscribing client identifier.
   * @param packet Incoming UNSUBSCRIBE packet.
   * @return UNSUBACK with one reason code per requested filter.
   */
  [[nodiscard]] UnsubackPacket
  handle_unsubscribe(std::string_view client_id,
                     const UnsubscribePacket &packet);

  /**
   * @brief Register a write-through callback invoked after every session
   *        snapshot mutation triggered by SUBSCRIBE or UNSUBSCRIBE (13.4).
   *
   * Called after apply_subscribe_to_session_snapshot() and after
   * apply_unsubscribe_to_session_snapshot(). The callback must be noexcept
   * — any exception is silently swallowed.
   *
   * @param callback Callback to register; pass {} to clear.
   */
  void set_on_session_changed(std::function<void()> callback) noexcept;

private:
  /**
   * @brief Snapshot session-changed callback.
   * @return Callback copy.
   */
  [[nodiscard]] std::function<void()> snapshot_on_session_changed() const;
  /**
   * @brief Install session-changed callback.
   * @param callback Callback to install.
   */
  void set_on_session_changed_callback(std::function<void()> callback) noexcept;
  /**
   * @brief Emit session-changed callback when registered.
   */
  void emit_on_session_changed() const noexcept;

  mutable std::mutex on_session_changed_callback_mutex_;
  AclEngine &acl_engine_;
  SessionStore &session_store_;
  SubscriptionStore &subscription_store_;
  SharedSubscriptionDispatcher &shared_dispatcher_;
  MessageRouter &message_router_;
  std::function<void()> on_session_changed_{}; ///< Write-through callback (13.4).
};

} // namespace mqtt
