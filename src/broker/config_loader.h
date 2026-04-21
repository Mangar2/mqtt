#pragma once

/**
 * @file config_loader.h
 * @brief ConfigLoader — INI-format configuration file parser (Module 15.1.1).
 */

#include <filesystem>
#include <string_view>
#include <vector>

#include "broker/broker_config.h"

namespace mqtt {

/**
 * @brief Parses INI-format configuration text or files into a `BrokerConfig`
 *        (Module 15.1.1).
 *
 * ### File format
 * - Lines starting with `#` (after leading whitespace) are comments.
 * - `[section]` lines introduce a new section.
 * - `key = value` lines set a parameter within the current section.
 * - Whitespace around `=`, keys, and values is trimmed.
 * - Unknown sections and keys are silently ignored.
 *
 * ### Validation
 * After parsing, `validate()` is called internally to check all numeric
 * ranges and the at-least-one-listener rule.  Any violation throws
 * `BrokerException(InvalidConfig)`.  A missing or zero mqtt_port *and*
 * ws_port throws `BrokerException(NoListenerConfigured)`.
 *
 * Thread safety: stateless — all methods are static.
 */
class ConfigLoader {
public:
  /**
   * @brief Load and parse a configuration file (15.1.1).
   *
   * Opens @p path, reads its contents, and delegates to `parse()`.
   *
   * @param path  Path to the INI configuration file.
   * @return Validated `BrokerConfig`.
   * @throws BrokerException(InvalidConfig) on I/O failure or invalid value.
   * @throws BrokerException(NoListenerConfigured) when both ports are 0.
   */
  [[nodiscard]] static BrokerConfig load(const std::filesystem::path &path);

  /**
   * @brief Parse configuration from an in-memory string (15.1.1).
   *
   * Processes @p text line by line, applies recognised key-value pairs to
   * a default `BrokerConfig`, then validates the result.
   *
   * @param text  INI-format configuration text.
   * @return Validated `BrokerConfig`.
   * @throws BrokerException(InvalidConfig) on any invalid value.
   * @throws BrokerException(NoListenerConfigured) when both ports are 0.
   */
  [[nodiscard]] static BrokerConfig parse(std::string_view text);

private:
  /**
   * @brief Parse one `username:password` credential entry.
   *
   * The first `:` separates username and password. Both parts must be
   * non-empty.
   *
   * @param value Raw value from INI key `credential`.
   * @return Parsed credential entry.
   * @throws BrokerException(InvalidConfig) on malformed input.
   */
  [[nodiscard]] static PasswordCredentialConfig
  parse_password_credential(std::string_view value);

  /**
   * @brief Parse one ACL rule entry from `effect,principal,action,topic`.
   *
   * @param value Raw value from INI key `rule` in section `[acl]`.
   * @return Parsed ACL rule config.
   * @throws BrokerException(InvalidConfig) on malformed input.
   */
  [[nodiscard]] static AclRuleConfig parse_acl_rule(std::string_view value);

  /**
   * @brief Validate all fields of @p cfg.
   *
   * @throws BrokerException(InvalidConfig) when any field violates its range.
   * @throws BrokerException(NoListenerConfigured) when both ports are zero.
   */
  static void validate(const BrokerConfig &cfg);

  /**
   * @brief Trim leading and trailing ASCII whitespace from @p str.
   * @return View into @p str without surrounding whitespace.
   */
  [[nodiscard]] static std::string_view trim(std::string_view str) noexcept;

  /**
   * @brief Parse a boolean value from a string.
   *
   * Accepts `"true"`, `"1"`, `"yes"` as true and
   * `"false"`, `"0"`, `"no"` as false (case-insensitive).
   *
   * @param val  String to parse.
   * @return Parsed boolean.
   * @throws BrokerException(InvalidConfig) if @p val is not recognised.
   */
  [[nodiscard]] static bool parse_bool(std::string_view val);

  /**
   * @brief Parse an unsigned 32-bit integer from a string.
   *
   * @param val  String to parse.
   * @return Parsed value.
   * @throws BrokerException(InvalidConfig) if @p val is not a valid number
   *         or is out of range for uint32_t.
   */
  [[nodiscard]] static uint32_t parse_uint32(std::string_view val);

  /**
   * @brief Parse an unsigned 16-bit integer from a string.
   *
   * @param val  String to parse.
   * @return Parsed value.
   * @throws BrokerException(InvalidConfig) if @p val is not a valid number
   *         or exceeds 65 535.
   */
  [[nodiscard]] static uint16_t parse_uint16(std::string_view val);

  /**
   * @brief Parse a comma-separated module list.
   *
   * Empty entries are ignored after trimming.
   *
   * @param val Input text, e.g. `"broker,connection"`.
   * @return Parsed module names in input order.
   */
  [[nodiscard]] static std::vector<std::string>
  parse_csv_modules(std::string_view val);

  /**
   * @brief Parse a structured tracing level.
   *
   * @param val Textual level name.
   * @return Parsed trace level.
   * @throws BrokerException(InvalidConfig) when the value is unknown.
   */
  [[nodiscard]] static TraceLevel parse_trace_level_or_throw(std::string_view val);

  /**
   * @brief Parse persistence mode text.
   *
   * Accepted values (case-insensitive):
   * - `"full"`
   * - `"off"`
   * - `"no-states"`
   *
   * @param val Textual persistence mode.
   * @return Parsed persistence mode enum value.
   * @throws BrokerException(InvalidConfig) when the value is unknown.
   */
  [[nodiscard]] static PersistenceMode
  parse_persistence_mode_or_throw(std::string_view val);

  /**
   * @brief Apply one parsed key-value pair from @p section to @p cfg.
   *
   * Unknown sections or keys are silently ignored.
   *
   * @param section  Lower-case section name (e.g. `"network"`).
   * @param key      Lower-case key name.
   * @param value    Raw (trimmed) value string.
   * @param cfg      Configuration struct to update.
   */
  static void apply_key(const std::string &section, const std::string &key,
                        const std::string &value, BrokerConfig &cfg);
};

} // namespace mqtt
