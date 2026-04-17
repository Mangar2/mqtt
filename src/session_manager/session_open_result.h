#pragma once

/**
 * @file session_open_result.h
 * @brief Result type returned by SessionManager::handle_connect (Module 10.1).
 */

namespace mqtt {

/**
 * @brief Result of a client connect attempt.
 *
 * Returned by `SessionManager::handle_connect`.  Callers use this to
 * populate the `session_present` flag in CONNACK and to decide whether the
 * displaced connection needs a final DISCONNECT write.
 */
struct SessionOpenResult {
  bool session_present;   ///< True when an existing session was resumed (Clean
                          ///< Start = 0 and session existed).
  bool takeover_occurred; ///< True when an old connection with the same Client
                          ///< ID was displaced.
};

} // namespace mqtt
