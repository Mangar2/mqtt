#pragma once

/**
 * @file subscription_orchestrator.h
 * @brief Orchestrates MQTT SUBSCRIBE and UNSUBSCRIBE flows for Broker.
 */

#include <string_view>

#include "authz/acl_engine.h"
#include "data_model/packet/subscribe_packets.h"
#include "message_router/message_router.h"
#include "message_router/shared_subscription_dispatcher.h"
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
   * @param subscription_store Store for regular (non-shared) subscriptions.
   * @param shared_dispatcher Dispatcher for shared subscription membership.
   * @param message_router Message router for retained-message delivery.
   */
  SubscriptionOrchestrator(AclEngine &acl_engine,
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

private:
  AclEngine &acl_engine_;
  SubscriptionStore &subscription_store_;
  SharedSubscriptionDispatcher &shared_dispatcher_;
  MessageRouter &message_router_;
};

} // namespace mqtt
