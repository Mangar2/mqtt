#include "broker/tick_handler.h"

#include <string>
#include <vector>

#include "monitoring/trace_runtime_command.h"

namespace mqtt {

TickHandler::TickHandler(WillPublisher &will_publisher,
                         SessionManager &session_manager,
                         SysTopicPublisher &sys_publisher,
                         StructuredTracer &structured_tracer)
    : will_publisher_(will_publisher), session_manager_(session_manager),
      sys_publisher_(sys_publisher), structured_tracer_(structured_tracer) {}

bool TickHandler::tick(std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  will_publisher_.publish_due(now);

  const std::vector<std::string> expired_sessions =
      session_manager_.cleanup_expired(now);
  for (const std::string &client_id : expired_sessions) {
    will_publisher_.on_session_expired(client_id);
  }

  return sys_publisher_.tick(now);
}

void TickHandler::apply_trace_system_message(const Message &message) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  apply_trace_runtime_command(structured_tracer_, message);
}

} // namespace mqtt
