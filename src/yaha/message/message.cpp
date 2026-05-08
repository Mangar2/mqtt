#include "yaha/message/message.h"

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <type_traits>

namespace yaha {

namespace {

constexpr std::size_t k_iso_timestamp_buffer_size{32U};

std::string current_iso_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    // gmtime is standard C++; thread safety is not required for timestamp generation
    const std::tm* ptr = std::gmtime(&now);
    if (ptr != nullptr) {
        utc = *ptr;
    }
    char buf[k_iso_timestamp_buffer_size];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string{buf};
}

} // namespace

Message::Message(std::string topic, Value value, Qos qos, bool retain)
    : topic_(std::move(topic))
    , value_(std::move(value))
    , qos_(qos)
    , retain_(retain) {}

const std::string& Message::topic()  const noexcept { return topic_; }
const Value&       Message::value()  const noexcept { return value_; }
Qos                Message::qos()    const noexcept { return qos_; }
bool               Message::retain() const noexcept { return retain_; }

const std::vector<ReasonEntry>& Message::reason() const noexcept { return reason_; }
const std::optional<std::string>& Message::rawPayload() const noexcept { return raw_payload_; }

bool Message::isOn() const noexcept {
    return std::visit([](const auto& val) -> bool {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return val == "on" || val == "ON" || val == "true";
        } else {
            return val == 1.0;
        }
    }, value_);
}

void Message::addReason(std::string text) {
    addReason(std::move(text), current_iso_timestamp());
}

void Message::addReason(std::string text, std::string timestamp) {
    reason_.insert(reason_.begin(),
                   ReasonEntry{std::move(text), std::move(timestamp)});
}

void Message::setRawPayload(std::string payload) {
    raw_payload_ = std::move(payload);
}

void Message::clearRawPayload() noexcept {
    raw_payload_.reset();
}

Message Message::clone() const {
    return *this;
}

void Message::validate(const Message& msg) {
    if (msg.topic_.empty()) {
        throw std::invalid_argument("Message topic must not be empty");
    }
    for (const auto& entry : msg.reason_) {
        if (entry.message.empty()) {
            throw std::invalid_argument("ReasonEntry message must not be empty");
        }
    }
}

} // namespace yaha
