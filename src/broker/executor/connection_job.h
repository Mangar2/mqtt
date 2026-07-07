#pragma once

/**
 * @file connection_job.h
 * @brief Value type for worker-pool jobs (threading refactoring step 02).
 */

#include <cstddef>
#include <cstdint>
#include <variant>

#include "network/tcp_connection.h"

namespace mqtt {

/**
 * @brief Supported job kinds for the executor worker pool.
 */
enum class JobType : std::uint8_t { Accept, Decode, Drain, Close };

/**
 * @brief Payload for an accept job.
 */
struct AcceptJobPayload {
  SocketHandle socket_handle{k_invalid_socket};
  bool websocket_connection{false};
};

/**
 * @brief Payload for a decode job.
 */
struct DecodeJobPayload {
  std::size_t budget_bytes{0U};
};

/**
 * @brief Payload for a drain job.
 */
struct DrainJobPayload {
  std::size_t budget_bytes{0U};
};

/**
 * @brief Payload for a close job.
 */
struct CloseJobPayload {
  bool immediate{false};
};

/**
 * @brief Variant payload for any connection job.
 */
using JobPayload =
    std::variant<AcceptJobPayload, DecodeJobPayload, DrainJobPayload,
                 CloseJobPayload>;

/**
 * @brief Immutable-by-convention value type queued in the executor.
 */
struct ConnectionJob {
  JobType type{JobType::Accept};
  int connection_fd{-1};
  JobPayload payload{AcceptJobPayload{}};
};

} // namespace mqtt

