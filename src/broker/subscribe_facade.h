#pragma once

/**
 * @file subscribe_facade.h
 * @brief SUBSCRIBE/UNSUBSCRIBE facade extracted from Broker.
 */

#include <string_view>

#include "data_model/packet/subscribe_packets.h"
#include "monitoring/structured_tracer.h"
#include "subscription_manager/subscription_orchestrator.h"

namespace mqtt {

/**
 * @brief Thread-safe SUBSCRIBE/UNSUBSCRIBE facade.
 */
class SubscribeFacade {
public:
  /**
   * @brief Construct a subscribe facade over broker dependencies.
   */
  SubscribeFacade(SubscriptionOrchestrator &subscription_orchestrator,
                  StructuredTracer &structured_tracer);

  /**
   * @brief Handle SUBSCRIBE packet.
   */
  [[nodiscard]] SubackPacket handle_subscribe(std::string_view client_id,
                                              const SubscribePacket &packet);

  /**
   * @brief Handle UNSUBSCRIBE packet.
   */
  [[nodiscard]] UnsubackPacket
  handle_unsubscribe(std::string_view client_id, const UnsubscribePacket &packet);

private:
  SubscriptionOrchestrator &subscription_orchestrator_;
  StructuredTracer &structured_tracer_;
};

} // namespace mqtt
