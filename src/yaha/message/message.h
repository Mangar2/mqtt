#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yaha {

enum class Qos : std::uint8_t {
    AtMostOnce  = 0U,
    AtLeastOnce = 1U,
    ExactlyOnce = 2U
};

struct ReasonEntry {
    std::string message;
    std::string timestamp;
};

using Value = std::variant<std::string, double>;

class Message {
public:
    Message(std::string topic, Value value,
            Qos qos = Qos::AtLeastOnce, bool retain = false, bool dup = false);

    [[nodiscard]] const std::string&              topic()  const noexcept;
    [[nodiscard]] const Value&                    value()  const noexcept;
    [[nodiscard]] Qos                             qos()    const noexcept;
    [[nodiscard]] bool                            retain() const noexcept;
    [[nodiscard]] bool                            dup()    const noexcept;
    [[nodiscard]] const std::vector<ReasonEntry>& reason() const noexcept;
    [[nodiscard]] const std::optional<std::string>& rawPayload() const noexcept;

    [[nodiscard]] bool isOn() const noexcept;

    void addReason(std::string text);
    void addReason(std::string text, std::string timestamp);
    void setDup(bool dup) noexcept;
    void setRawPayload(std::string payload);
    void clearRawPayload() noexcept;

    [[nodiscard]] Message clone() const;

    static void validate(const Message& msg);

private:
    std::string              topic_;
    Value                    value_;
    Qos                      qos_;
    bool                     retain_;
    bool                     dup_;
    std::vector<ReasonEntry> reason_;
    std::optional<std::string> raw_payload_;
};

} // namespace yaha
