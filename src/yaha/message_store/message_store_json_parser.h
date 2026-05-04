#pragma once

/**
 * @file message_store_json_parser.h
 * @brief JSON parser helpers for MessageStore HTTP request payloads.
 */

#include "yaha/message_store/message_tree.h"

#include <cstdint>
#include <string>
#include <vector>

namespace yaha::message_store_json {

/**
 * @brief Parsed sensor-style POST request parameters.
 */
struct SensorPostRequest {
    std::string topicPrefix{};
    bool includeHistory{false};
    bool includeReason{true};
    std::uint32_t levelAmount{1U};
    bool hasNodes{false};
    std::string nodesJson{};
};

/**
 * @brief Parses snapshot diff request body as JSON array.
 * @param body Raw request body.
 * @param out Parsed topic/value snapshot entries.
 * @return True when parsing succeeds.
 */
bool parseSnapshotBody(const std::string& body, std::vector<MessageTreeSnapshotNode>& out);

/**
 * @brief Parses sensor.php-compatible POST JSON object.
 * @param body Raw request body.
 * @param output Parsed sensor request fields.
 * @return True when parsing succeeds.
 */
bool parseSensorPostBody(const std::string& body, SensorPostRequest& output);

} // namespace yaha::message_store_json
