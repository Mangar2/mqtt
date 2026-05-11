#pragma once

/**
 * @file zwave_devices_mapper.h
 * @brief ZWave device mapping and set-value conversion helpers.
 */

#include "yaha/message/message.h"
#include "yaha/zwave/zwave_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace yaha {

/**
 * @brief One incoming ZWave value metadata object used for topic resolution.
 */
struct ZwaveValueDescriptor {
    std::uint16_t nodeId{0U};
    std::uint16_t classId{0U};
    std::uint8_t instance{1U};
    std::uint8_t index{0U};
    std::optional<std::string> label{};
    std::optional<std::uint64_t> valueId{};
};

/**
 * @brief Resolved topic mapping and type hint.
 */
struct ZwaveTopicMapping {
    std::string topic{};
    std::string type{"bool"};
};

/**
 * @brief One searchable value object for class-id resolution by label.
 */
struct ZwaveNodeObject {
    std::uint16_t classId{0U};
    std::string label{};
    std::uint8_t instance{1U};
    std::uint8_t index{0U};
    std::string type{"bool"};
};

using ZwaveNodeMap = std::unordered_map<std::uint16_t, std::vector<ZwaveNodeObject>>;

/**
 * @brief Fully resolved ZWave target id for write operations.
 */
struct ZwaveResolvedId {
    std::uint16_t nodeId{0U};
    std::uint16_t classId{0U};
    std::uint8_t instance{1U};
    std::uint8_t index{0U};
    std::string type{"bool"};
};

/**
 * @brief Write operation kind.
 */
enum class ZwaveWriteKind : std::uint8_t {
    SetValue,
    SetConfigParam
};

/**
 * @brief Normalized write request after type conversion.
 */
struct ZwaveWriteRequest {
    ZwaveWriteKind kind{ZwaveWriteKind::SetValue};
    ZwaveResolvedId target{};
    std::variant<bool, double, std::string> value{false};
};

/**
 * @brief Resolves device mappings and write conversions for the ZWave client.
 */
class ZwaveDevicesMapper {
public:
    explicit ZwaveDevicesMapper(std::vector<ZwaveDeviceConfig> devices);

    /**
     * @brief Resolves outbound topic for one incoming ZWave value.
     * @param descriptor Incoming value descriptor.
     * @return Topic mapping when found.
     */
    [[nodiscard]] std::optional<ZwaveTopicMapping> valueToTopicAndType(
        const ZwaveValueDescriptor& descriptor);

    /**
     * @brief Resolves full ZWave id from MQTT topic and optional label.
     * @param nodes Node/object registry for class resolution by label.
     * @param topic Device topic prefix without trailing /set.
     * @param label Optional label token from topic path.
     * @return Resolved ZWave id.
     * @throws std::runtime_error on missing device mapping or invalid mapping data.
     */
    [[nodiscard]] ZwaveResolvedId topicToZwaveId(
        const ZwaveNodeMap& nodes,
        const std::string& topic,
        const std::optional<std::string>& label) const;

    /**
     * @brief Converts MQTT value into normalized write request payload.
     * @param target Resolved ZWave target metadata.
     * @param input Incoming MQTT value.
     * @return Converted write request.
     */
    [[nodiscard]] static ZwaveWriteRequest buildWriteRequest(
        const ZwaveResolvedId& target,
        const Value& input);

private:
    [[nodiscard]] std::optional<ZwaveTopicMapping> findBestMatch(
        const ZwaveValueDescriptor& descriptor) const;

    [[nodiscard]] static std::string makeTopicCacheKey(
        const std::string& topic,
        const std::optional<std::string>& label);

    std::vector<ZwaveDeviceConfig> devices_{};
    std::unordered_map<std::uint64_t, ZwaveTopicMapping> valueIdToTopicCache_{};
};

} // namespace yaha
