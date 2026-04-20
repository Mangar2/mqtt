#pragma once

/**
 * @file publish_facade.h
 * @brief Inbound publish facade extracted from Broker.
 */

#include <mutex>
#include <string_view>

#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/reason_code/reason_code.h"
#include "message_router/message_router.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/structured_tracer.h"

namespace mqtt {

/**
 * @brief Thread-safe inbound publish facade.
 */
class PublishFacade {
public:
  /**
   * @brief Construct a publish facade over broker dependencies.
   */
  PublishFacade(MessageRouter &message_router,
                StatisticsCollector &statistics_collector,
                StructuredTracer &structured_tracer);

  /**
   * @brief Handle inbound publish routing and reason-code mapping.
   */
  [[nodiscard]] ReasonCode handle_publish(Message &message,
                                          std::string_view client_id,
                                          std::string_view username,
                                          TopicAliasTable &alias_table);

private:
  MessageRouter &message_router_;
  StatisticsCollector &statistics_collector_;
  StructuredTracer &structured_tracer_;
  std::mutex mutex_;
};

} // namespace mqtt
