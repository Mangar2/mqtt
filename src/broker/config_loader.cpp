/**
 * @file config_loader.cpp
 * @brief ConfigLoader implementation — INI parser for broker configuration
 *        (Module 15.1.1).
 */

#include "broker/config_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "broker/broker_error.h"

namespace mqtt {

PasswordCredentialConfig
ConfigLoader::parse_password_credential(std::string_view value) {
  const std::size_t separator_pos = value.find(':');
  if (separator_pos == std::string_view::npos) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "Invalid auth credential format, expected "
                          "username:password");
  }

  const std::string_view user_name = trim(value.substr(0U, separator_pos));
  const std::string_view pass_word = trim(value.substr(separator_pos + 1U));
  if (user_name.empty() || pass_word.empty()) {
    throw BrokerException(
        BrokerError::InvalidConfig,
        "Auth credential username/password must be non-empty");
  }

  return PasswordCredentialConfig{.username = std::string(user_name),
                                  .password = std::string(pass_word)};
}

//
// Public API

BrokerConfig ConfigLoader::load(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "Cannot open config file: " + path.string());
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  if (file.bad()) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "I/O error reading config file: " + path.string());
  }
  return parse(buf.str());
}

BrokerConfig ConfigLoader::parse(std::string_view text) {
  BrokerConfig cfg;
  std::string current_section;

  std::string owned{text};
  std::istringstream stream(owned);
  std::string line;

  while (std::getline(stream, line)) {
    std::string_view view = trim(line);

    // Skip blank lines and comments
    if (view.empty() || view.front() == '#') {
      continue;
    }

    // Section header
    if (view.front() == '[') {
      auto close = view.find(']');
      if (close != std::string_view::npos) {
        current_section = std::string(trim(view.substr(1U, close - 1U)));
        std::ranges::transform(current_section, current_section.begin(),
                               [](unsigned char chr) {
                                 return static_cast<char>(std::tolower(chr));
                               });
      }
      continue;
    }

    // Key = Value
    auto eq_pos = view.find('=');
    if (eq_pos == std::string_view::npos) {
      continue; // malformed line — ignore
    }

    std::string key = std::string(trim(view.substr(0U, eq_pos)));
    std::string value = std::string(trim(view.substr(eq_pos + 1U)));

    std::ranges::transform(key, key.begin(), [](unsigned char chr) {
      return static_cast<char>(std::tolower(chr));
    });

    apply_key(current_section, key, value, cfg);
  }

  validate(cfg);
  return cfg;
}

//
// Private helpers

void ConfigLoader::apply_key(const std::string &section, const std::string &key,
                             const std::string &value, BrokerConfig &cfg) {
  if (section == "network") {
    if (key == "mqtt_port") {
      cfg.mqtt_port = parse_uint16(value);
    } else if (key == "ws_port") {
      cfg.ws_port = parse_uint16(value);
    }
  } else if (section == "broker") {
    if (key == "allow_anonymous") {
      cfg.allow_anonymous = parse_bool(value);
    } else if (key == "max_connections") {
      cfg.max_connections = parse_uint32(value);
    } else if (key == "receive_maximum") {
      cfg.receive_maximum = parse_uint16(value);
    } else if (key == "session_expiry_max") {
      cfg.session_expiry_max = parse_uint32(value);
    } else if (key == "topic_alias_maximum") {
      cfg.topic_alias_maximum = parse_uint16(value);
    } else if (key == "max_queued_messages") {
      cfg.max_queued_messages = parse_uint32(value);
    } else if (key == "qos_retransmit_timeout_seconds") {
      cfg.qos_retransmit_timeout_seconds = parse_uint32(value);
    } else if (key == "tick_interval_ms") {
      cfg.tick_interval_ms = parse_uint32(value);
    }
  } else if (section == "persistence") {
    if (key == "enabled") {
      cfg.persistence_enabled = parse_bool(value);
    } else if (key == "dir") {
      cfg.persistence_dir = value;
    }
  } else if (section == "auth") {
    if (key == "credential") {
      cfg.password_credentials.push_back(parse_password_credential(value));
    }
  }
}

void ConfigLoader::validate(const BrokerConfig &cfg) {
  if (cfg.max_connections < 1U || cfg.max_connections > 100'000U) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "max_connections must be in [1, 100000]");
  }
  if (cfg.receive_maximum < 1U) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "receive_maximum must be at least 1");
  }
  if (cfg.max_queued_messages < 1U || cfg.max_queued_messages > 100'000U) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "max_queued_messages must be in [1, 100000]");
  }
  if (cfg.qos_retransmit_timeout_seconds < 1U) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "qos_retransmit_timeout_seconds must be at least 1");
  }
  if (cfg.tick_interval_ms < 1U) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "tick_interval_ms must be at least 1");
  }
  if (cfg.mqtt_port == 0U && cfg.ws_port == 0U) {
    throw BrokerException(
        BrokerError::NoListenerConfigured,
        "At least one of mqtt_port or ws_port must be non-zero");
  }
}

std::string_view ConfigLoader::trim(std::string_view str) noexcept {
  while (!str.empty() &&
         (std::isspace(static_cast<unsigned char>(str.front())) != 0)) {
    str.remove_prefix(1U);
  }
  while (!str.empty() &&
         (std::isspace(static_cast<unsigned char>(str.back())) != 0)) {
    str.remove_suffix(1U);
  }
  return str;
}

bool ConfigLoader::parse_bool(std::string_view val) {
  std::string lower{val};
  std::ranges::transform(lower, lower.begin(), [](unsigned char chr) {
    return static_cast<char>(std::tolower(chr));
  });

  if (lower == "true" || lower == "1" || lower == "yes") {
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no") {
    return false;
  }
  throw BrokerException(BrokerError::InvalidConfig,
                        "Invalid boolean value: " + std::string(val));
}

uint32_t ConfigLoader::parse_uint32(std::string_view val) {
  if (val.empty()) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "Expected a number, got empty string");
  }
  uint64_t result = 0U;
  for (char chr : val) {
    if (std::isdigit(static_cast<unsigned char>(chr)) == 0) {
      throw BrokerException(BrokerError::InvalidConfig,
                            "Invalid number: " + std::string(val));
    }
    result = (result * 10U) + static_cast<uint64_t>(chr - '0');
    if (result > UINT32_MAX) {
      throw BrokerException(BrokerError::InvalidConfig,
                            "Number out of uint32 range: " + std::string(val));
    }
  }
  return static_cast<uint32_t>(result);
}

uint16_t ConfigLoader::parse_uint16(std::string_view val) {
  uint32_t result = parse_uint32(val);
  if (result > UINT16_MAX) {
    throw BrokerException(BrokerError::InvalidConfig,
                          "Number out of uint16 range: " + std::string(val));
  }
  return static_cast<uint16_t>(result);
}

} // namespace mqtt
