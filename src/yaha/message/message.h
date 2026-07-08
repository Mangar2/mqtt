/**
 * @file message.h
 * @brief Universal YAHA message value type shared across all clients and transports.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yaha {

/**
 * @brief MQTT quality-of-service level.
 */
enum class Qos : std::uint8_t {
    AtMostOnce  = 0U,  ///< QoS 0, fire-and-forget delivery.
    AtLeastOnce = 1U,  ///< QoS 1, default delivery guarantee for YAHA messages.
    ExactlyOnce = 2U   ///< QoS 2, exactly-once delivery.
};

/**
 * @brief One entry in a message's reason chain.
 */
struct ReasonEntry {
    std::string message;    ///< Human-readable reason text.
    std::string timestamp;  ///< ISO 8601 UTC timestamp of the reason entry.
};

/**
 * @brief Ordered list of reason entries; index 0 is the most recent entry.
 */
using ReasonList = std::vector<ReasonEntry>;

/**
 * @brief Message payload value, either a string or a double.
 */
using Value = std::variant<std::string, double>;

/**
 * @brief Universal message value type sent and received by every YAHA client.
 *
 * Carries topic, value, reason chain, and MQTT publish flags (qos/retain/dup) as one
 * value-semantics object. Callers derive independent copies via clone() combined with
 * the setTopic()/setQos()/setRetain()/setDup() mutators instead of reconstructing a
 * Message field by field.
 */
class Message {
public:
    /**
     * @brief Constructs a message.
     * @param topic MQTT topic path; must not be empty (enforced by validate()).
     * @param value Payload value, string or double.
     * @param qos MQTT quality-of-service level.
     * @param retain MQTT retain flag.
     * @param dup MQTT DUP flag for publish semantics.
     */
    Message(std::string topic, Value value,
            Qos qos = Qos::AtLeastOnce, bool retain = false, bool dup = false);

    /**
     * @brief Returns the MQTT topic path.
     * @return Reference to the stored topic string.
     */
    [[nodiscard]] const std::string&              topic()  const noexcept;
    /**
     * @brief Returns the payload value.
     * @return Reference to the stored value variant.
     */
    [[nodiscard]] const Value&                    value()  const noexcept;
    /**
     * @brief Returns the MQTT quality-of-service level.
     * @return Stored QoS level.
     */
    [[nodiscard]] Qos                             qos()    const noexcept;
    /**
     * @brief Returns the MQTT retain flag.
     * @return True if the message is a retained publish.
     */
    [[nodiscard]] bool                            retain() const noexcept;
    /**
     * @brief Returns the MQTT DUP flag.
     * @return True if this is a duplicate delivery.
     */
    [[nodiscard]] bool                            dup()    const noexcept;
    /**
     * @brief Returns the reason chain.
     * @return Reference to the reason list, most-recent entry first.
     */
    [[nodiscard]] const ReasonList& reason() const noexcept;
    /**
     * @brief Returns the original transport payload, if stored.
     * @return Reference to the optional raw payload string.
     */
    [[nodiscard]] const std::optional<std::string>& rawPayload() const noexcept;

    /**
     * @brief Evaluates whether the value represents an "on" state.
     * @return True for value == 1.0, or string "on"/"ON"/"true".
     */
    [[nodiscard]] bool isOn() const noexcept;

    /**
     * @brief Prepends a reason entry with an auto-generated ISO 8601 UTC timestamp.
     * @param text Reason message text.
     */
    void addReason(std::string text);
    /**
     * @brief Prepends a reason entry with a caller-supplied timestamp.
     * @param text Reason message text.
     * @param timestamp Caller-supplied timestamp for the entry.
     */
    void addReason(std::string text, std::string timestamp);
    /**
     * @brief Updates the topic in place.
     * @param topic New MQTT topic path.
     */
    void setTopic(std::string topic);
    /**
     * @brief Updates the MQTT quality-of-service level in place.
     * @param qos New QoS level.
     */
    void setQos(Qos qos) noexcept;
    /**
     * @brief Updates the MQTT retain flag in place.
     * @param retain New retain flag value.
     */
    void setRetain(bool retain) noexcept;
    /**
     * @brief Updates the MQTT DUP flag in place.
     * @param dup New DUP flag value.
     */
    void setDup(bool dup) noexcept;
    /**
     * @brief Stores the original transport payload bytes for lossless forwarding.
     * @param payload Raw payload text as received or to be forwarded unchanged.
     */
    void setRawPayload(std::string payload);
    /**
     * @brief Removes the optional raw payload, if any.
     */
    void clearRawPayload() noexcept;

    /**
     * @brief Returns a deep, independent copy of this message.
     * @return New Message with the same field values.
     */
    [[nodiscard]] Message clone() const;

    /**
     * @brief Validates required message invariants.
     * @param msg Message to validate.
     */
    static void validate(const Message& msg);

private:
    std::string              topic_;
    Value                    value_;
    Qos                      qos_;
    bool                     retain_;
    bool                     dup_;
    ReasonList reason_;
    std::optional<std::string> raw_payload_;
};

} // namespace yaha
