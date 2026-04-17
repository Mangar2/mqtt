#pragma once

/**
 * @file topic_alias_table.h
 * @brief TopicAliasTable — inbound and outbound topic alias storage
 * (Module 7.3).
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mqtt {

/**
 * @brief Manages inbound and outbound Topic Alias mappings for a single MQTT
 * connection (Module 7.3).
 *
 * Topic Aliases reduce wire overhead by replacing topic strings with compact
 * 16-bit integers (MQTT 5.0 §3.3.2.3.4).  Two independent maps are maintained:
 *
 * - **Inbound** (client → broker): the client chooses the alias; the broker
 *   resolves it back to the full topic name before routing.
 * - **Outbound** (broker → client): the broker assigns aliases when sending
 *   PUBLISH to subscribers.
 *
 * Both maps enforce the Topic Alias Maximum negotiated during CONNECT (7.3.4).
 * Alias value 0 is never valid (MQTT 5.0 §3.3.2.3.4).
 *
 * Calling `reset()` clears both maps (7.3.3), which must be done on connection
 * close.
 *
 * Thread safety: none — external synchronisation required.
 */
class TopicAliasTable {
public:
  /**
   * @brief Construct with the negotiated Topic Alias Maximum.
   *
   * A `max_aliases` value of 0 effectively disables topic aliases —
   * all `set_*` calls will throw `AliasOutOfRange`.
   *
   * @param max_aliases Topic Alias Maximum from CONNECT/CONNACK (7.3.4).
   */
  explicit TopicAliasTable(uint16_t max_aliases) noexcept;

  /**
   * @brief Store an inbound alias → topic mapping (7.3.1).
   *
   * @param alias  Alias value chosen by the client; must be in [1,
   * max_aliases].
   * @param topic  Full topic string to associate with the alias.
   * @throws ConnectionException(AliasOutOfRange) if alias == 0 or alias >
   * max_aliases.
   */
  void set_inbound(uint16_t alias, std::string topic);

  /**
   * @brief Resolve an inbound alias to its topic string (7.3.1).
   *
   * @param alias  Alias value sent by the client.
   * @return The topic string associated with the alias.
   * @throws ConnectionException(AliasNotFound) if no mapping exists for alias.
   * @throws ConnectionException(AliasOutOfRange) if alias == 0 or alias >
   * max_aliases.
   */
  [[nodiscard]] const std::string &get_inbound(uint16_t alias) const;

  /**
   * @brief Store an outbound topic → alias mapping (7.3.2).
   *
   * @param topic  Full topic string.
   * @param alias  Alias value to assign; must be in [1, max_aliases].
   * @throws ConnectionException(AliasOutOfRange) if alias == 0 or alias >
   * max_aliases.
   */
  void set_outbound(std::string topic, uint16_t alias);

  /**
   * @brief Look up the alias assigned to an outbound topic (7.3.2).
   *
   * @param topic  Full topic string.
   * @return The alias if one has been assigned, or `std::nullopt`.
   */
  [[nodiscard]] std::optional<uint16_t>
  get_outbound(std::string_view topic) const;

  /**
   * @brief Clear all inbound and outbound mappings (7.3.3).
   *
   * Must be called when the connection is closed.
   */
  void reset() noexcept;

  /**
   * @brief Return the negotiated Topic Alias Maximum (7.3.4).
   * @return Maximum permitted alias value.
   */
  [[nodiscard]] uint16_t max_aliases() const noexcept;

private:
  /**
   * @brief Validate that alias is in [1, max_aliases_].
   * @param alias Value to validate.
   * @throws ConnectionException(AliasOutOfRange) on violation.
   */
  void validate_alias(uint16_t alias) const;

  uint16_t max_aliases_; ///< Negotiated Topic Alias Maximum.
  std::unordered_map<uint16_t, std::string>
      inbound_; ///< alias → topic (inbound direction).
  std::unordered_map<std::string, uint16_t>
      outbound_; ///< topic → alias (outbound direction).
};

} // namespace mqtt
