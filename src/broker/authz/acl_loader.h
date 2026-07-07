#pragma once

/**
 * @file acl_loader.h
 * @brief AclLoader — parse configuration records into ACL rules and reload
 * at runtime (Module 9.2).
 */

#include "authz/acl_engine.h"
#include "authz/acl_rule.h"
#include <string>
#include <vector>

namespace mqtt {

/**
 * @brief Plain-string representation of one ACL rule as read from a
 * configuration source.
 *
 * All fields are case-sensitive strings.  Accepted values:
 *
 * | Field           | Accepted values |
 * |-----------------|-----------------|
 * | `principal`     | `"*"` or any non-empty string |
 * | `topic_pattern` | Any string |
 * | `action`        | `"publish"` / `"subscribe"` / `"publish_and_subscribe"` |
 * | `effect`        | `"allow"` / `"deny"` |
 */
struct AclRuleConfig {
  std::string principal;     ///< Principal pattern; `"*"` matches everyone.
  std::string topic_pattern; ///< MQTT-style topic filter for this rule.
  std::string action;        ///< Operation covered: publish / subscribe / both.
  std::string effect;        ///< Outcome when matched: allow / deny.
};

/**
 * @brief Parses `AclRuleConfig` records into `AclRule` values and installs
 * them into an `AclEngine` (Module 9.2).
 *
 * Provides two entry points:
 * - `load()` — initial population at start-up (9.2.1).
 * - `reload()` — hot replacement of the rule set while the server is live
 *   (9.2.2).
 *
 * Both methods parse the supplied configuration, then call `AclEngine::reload`
 * to atomically install the new rule set in the associated engine.
 *
 * @throws AuthzException(AuthzError::InvalidAction) for unknown action strings.
 * @throws AuthzException(AuthzError::InvalidEffect) for unknown effect strings.
 *
 * Thread safety: none — external synchronisation required.
 */
class AclLoader {
public:
  /**
   * @brief Construct a loader bound to the given engine.
   *
   * The engine must outlive the loader.
   *
   * @param engine Engine into which parsed rules will be installed.
   */
  explicit AclLoader(AclEngine &engine);

  /**
   * @brief Parse config records and populate the engine for the first time
   * (9.2.1).
   *
   * Replaces any rules already present in the engine.
   *
   * @param config Ordered list of rule config records.
   * @throws AuthzException on unrecognised action or effect strings.
   */
  void load(const std::vector<AclRuleConfig> &config);

  /**
   * @brief Parse config records and hot-reload the engine rule set (9.2.2).
   *
   * Semantically equivalent to `load`; provided as a distinct entry point
   * to make runtime reload intent explicit at call sites.
   *
   * @param config Ordered list of new rule config records.
   * @throws AuthzException on unrecognised action or effect strings.
   */
  void reload(const std::vector<AclRuleConfig> &config);

private:
  AclEngine &engine_; ///< Reference to the engine managed by this loader.

  /**
   * @brief Parse all config records into `AclRule` values.
   *
   * @param config Source config records.
   * @return Parsed rule vector ready for installation.
   * @throws AuthzException on parse errors.
   */
  [[nodiscard]] static std::vector<AclRule> parse(const std::vector<AclRuleConfig> &config);

  /**
   * @brief Convert a single config record into an `AclRule`.
   *
   * @param cfg One config record.
   * @return Fully populated `AclRule`.
   * @throws AuthzException(AuthzError::InvalidAction) for unknown action.
   * @throws AuthzException(AuthzError::InvalidEffect) for unknown effect.
   */
  [[nodiscard]] static AclRule parse_rule(const AclRuleConfig &cfg);
};

} // namespace mqtt
