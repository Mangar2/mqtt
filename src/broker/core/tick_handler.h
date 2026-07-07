#pragma once

/**
 * @file tick_handler.h
 * @brief Housekeeping tick facade extracted from Broker.
 */

#include <chrono>
#include <mutex>

#include "broker/data_model/message/message.h"
#include "broker/monitoring/structured_tracer.h"
#include "broker/monitoring/sys_topic_publisher.h"
#include "broker/session_manager/session_manager.h"
#include "broker/will_manager/will_publisher.h"

namespace mqtt {

/**
 * @brief Thread-safe housekeeping and runtime trace command handler.
 */
class TickHandler {
public:
  /**
   * @brief Construct a tick handler over broker dependencies.
   */
  TickHandler(WillPublisher &will_publisher, SessionManager &session_manager,
              SysTopicPublisher &sys_publisher,
              StructuredTracer &structured_tracer);

  /**
   * @brief Run one broker housekeeping tick.
   */
  [[nodiscard]] bool tick(std::chrono::steady_clock::time_point now);

  /**
   * @brief Apply one runtime tracing system message.
   */
  void apply_trace_system_message(const Message &message);

private:
  WillPublisher &will_publisher_;
  SessionManager &session_manager_;
  SysTopicPublisher &sys_publisher_;
  StructuredTracer &structured_tracer_;
  std::mutex mutex_;
};

} // namespace mqtt
