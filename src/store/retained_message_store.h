#pragma once

/**
 * @file retained_message_store.h
 * @brief In-memory retained message store (Module 4.2).
 */

#include <cstddef>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief Retained message plus the time it was stored by the broker.
 */
struct RetainedMessageRecord {
  Message message;                                   ///< Stored retained message.
  std::chrono::steady_clock::time_point stored_at;  ///< Broker-side store time.
};

/**
 * @brief In-memory store for MQTT 5.0 retained messages (Module 4.2).
 *
 * Maps exact topic names (no wildcards) to their latest retained message.
 * A publish with `retain = true` and a non-empty payload creates or overwrites
 * the stored entry; an empty payload deletes it.
 *
 * Wildcard lookup at subscribe time uses `TopicMatcher` internally.
 */
class RetainedMessageStore {
public:
  /**
   * @brief Store, overwrite, or delete a retained message (4.2.1 + 4.2.2).
   *
   * - Non-empty payload: insert or overwrite the entry for `msg.topic`.
   * - Empty payload: delete the entry for `msg.topic` if present.
   *
   * @param msg The message to store.  `msg.topic` must be a valid topic name
   *            (no wildcards).  `msg.retain` is informational and not checked
   * here.
  * @param stored_at Timestamp captured when the broker stores the retained
  *                  message.
   */
  void store(
      const Message &msg,
      std::chrono::steady_clock::time_point stored_at =
          std::chrono::steady_clock::now());

  /**
   * @brief Return all retained messages matching a topic filter (4.2.3).
   *
   * Applies full MQTT 5.0 wildcard matching (exact, `+`, `#`,
   * system-topic exclusion) using `TopicMatcher`.
   *
   * @param topic_filter The subscriber's topic filter; may contain `+` or `#`.
   * @return Vector of matching messages; order is unspecified.
   */
  [[nodiscard]] std::vector<Message> find(std::string_view topic_filter) const;

  /**
   * @brief Return copies of all currently stored retained messages.
   *
   * Unlike `find()`, this method returns every stored message including
   * those whose topic begins with `$` (system topics).  Use this method
   * when persisting the full retained-message snapshot.
   *
   * @return Vector of all retained messages; order is unspecified.
   */
  [[nodiscard]] std::vector<Message> all() const;

  /**
   * @brief Return retained records matching a topic filter including
   *        per-record store timestamps.
   *
   * @param topic_filter The subscriber's topic filter; may contain `+` or `#`.
   * @return Vector of matching retained records; order is unspecified.
   */
  [[nodiscard]] std::vector<RetainedMessageRecord>
  find_records(std::string_view topic_filter) const;

  /**
   * @brief Return the number of currently stored retained messages.
   * @return Count of entries in the store.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, RetainedMessageRecord>
      messages_; ///< Topic-name → retained message with store timestamp.
};

} // namespace mqtt
