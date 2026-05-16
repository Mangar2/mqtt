#include "yaha/broker_connector/relay_component.h"

#include <cctype>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace yaha {

namespace {

std::string escapeJsonString(const std::string& inputText) {
    std::string escaped{};
    escaped.reserve(inputText.size());
    for (const char character : inputText) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }
    return escaped;
}

bool tryFindObjectRange(const std::string& text,
                        const std::string& key,
                        std::size_t& objectStart,
                        std::size_t& objectEnd) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = text.find(keyToken);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t cursor = text.find(':', keyPos + keyToken.size());
    if (cursor == std::string::npos) {
        return false;
    }
    ++cursor;

    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != '{') {
        return false;
    }

    int depth = 0;
    for (std::size_t position = cursor; position < text.size(); ++position) {
        const char currentCharacter = text[position];
        if (currentCharacter == '{') {
            ++depth;
        } else if (currentCharacter == '}') {
            --depth;
            if (depth == 0) {
                objectStart = cursor;
                objectEnd = position;
                return true;
            }
        }
    }

    return false;
}

bool tryFindStringValueRange(const std::string& objectText,
                             const std::size_t searchStart,
                             const std::size_t searchEnd,
                             const std::string& key,
                             std::size_t& valueStart,
                             std::size_t& valueEnd) {
    if (searchStart >= objectText.size() || searchEnd >= objectText.size() || searchStart > searchEnd) {
        return false;
    }

    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = objectText.find(keyToken, searchStart);
    if (keyPos == std::string::npos || keyPos > searchEnd) {
        return false;
    }

    std::size_t cursor = objectText.find(':', keyPos + keyToken.size());
    if (cursor == std::string::npos || cursor > searchEnd) {
        return false;
    }
    ++cursor;

    while (cursor <= searchEnd && std::isspace(static_cast<unsigned char>(objectText[cursor])) != 0) {
        ++cursor;
    }
    if (cursor > searchEnd || objectText[cursor] != '"') {
        return false;
    }

    const std::size_t stringStart = cursor + 1U;
    std::size_t stringEnd = stringStart;
    bool escaped = false;
    for (; stringEnd <= searchEnd; ++stringEnd) {
        const char currentCharacter = objectText[stringEnd];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (currentCharacter == '\\') {
            escaped = true;
            continue;
        }
        if (currentCharacter == '"') {
            valueStart = stringStart;
            valueEnd = stringEnd;
            return true;
        }
    }

    return false;
}

std::optional<std::string> tryRewriteForwardedEnvelopeTopic(const std::string& rawPayload,
                                                            const std::string& targetTopic) {
    std::size_t messageStart = 0U;
    std::size_t messageEnd = 0U;
    if (!tryFindObjectRange(rawPayload, "message", messageStart, messageEnd)) {
        return std::nullopt;
    }

    std::size_t topicValueStart = 0U;
    std::size_t topicValueEnd = 0U;
    if (!tryFindStringValueRange(rawPayload,
                                 messageStart,
                                 messageEnd,
                                 "topic",
                                 topicValueStart,
                                 topicValueEnd)) {
        return std::nullopt;
    }

    std::string rewrittenPayload = rawPayload;
    rewrittenPayload.replace(topicValueStart,
                             topicValueEnd - topicValueStart,
                             escapeJsonString(targetTopic));
    return rewrittenPayload;
}

} // namespace

BrokerConnectorComponent::BrokerConnectorComponent(RelayPolicyConfig config)
    : config_(config) {}

BrokerConnectorComponent::~BrokerConnectorComponent() {
    close();
}

void BrokerConnectorComponent::setSourceAdapter(SourceHttpBrokerAdapter& sourceAdapter,
                                                SourceLifecycleConfig lifecycleConfig) {
    sourceAdapter.setIncomingPublishCallback(
        [this](const Message& message, const SourcePublishMeta& sourceMeta) {
            (void)onIncomingPublish(message, sourceMeta);
        });

    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    sourceLifecycle_ = std::make_unique<SourceLifecycleManager>(sourceAdapter, lifecycleConfig);
}

SubscriptionMap BrokerConnectorComponent::getSubscriptions() const {
    return {};
}

void BrokerConnectorComponent::handleMessage(const Message& message) {
    (void)message;
}

void BrokerConnectorComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    publishCallback_ = std::move(callback);
}

void BrokerConnectorComponent::run() {
    SourceLifecycleManager* lifecycle = nullptr;
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        if (running_) {
            return;
        }

        running_ = true;
        lifecycle = sourceLifecycle_.get();
    }

    if (lifecycle != nullptr) {
        lifecycle->run();
    }
}

void BrokerConnectorComponent::close() {
    std::unique_ptr<SourceLifecycleManager> lifecycle{};
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        running_ = false;
        lifecycle = std::move(sourceLifecycle_);
    }

    if (lifecycle != nullptr) {
        lifecycle->close();
    }
}

bool BrokerConnectorComponent::onIncomingPublish(const Message& message,
                                                 const SourcePublishMeta& sourceMeta) {
    PublishCallback callback{};
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        if (!running_ || !publishCallback_) {
            return false;
        }

        counters_.received += 1U;
        callback = publishCallback_;
    }

    const Message outgoingMessage = toForwardMessage(message, sourceMeta);

    std::uint32_t attempt = 0U;
    while (true) {
        try {
            callback(outgoingMessage);
            std::lock_guard<std::mutex> lock{relay_state_mutex_};
            counters_.forwarded += 1U;
            return true;
        } catch (const std::exception&) {
        } catch (...) {
        }

        if (attempt >= config_.maxPublishRetries) {
            std::lock_guard<std::mutex> lock{relay_state_mutex_};
            counters_.failed += 1U;
            return false;
        }

        if (config_.publishRetryBackoff.count() > 0) {
            std::this_thread::sleep_for(config_.publishRetryBackoff);
        }

        attempt += 1U;
    }

    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    counters_.failed += 1U;
    return false;
}

RelayCounters BrokerConnectorComponent::getStats() const {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    return counters_;
}

bool BrokerConnectorComponent::isRunning() const {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    return running_;
}

Message BrokerConnectorComponent::toForwardMessage(const Message& message,
                                                   const SourcePublishMeta& sourceMeta) const {
    constexpr std::string_view k_sys_topic_prefix{"$SYS/"};

    const std::string& sourceTopic = message.topic();
    std::string targetTopic = sourceTopic;
    if (sourceTopic == "$SYS") {
        targetTopic = "status";
    } else if (sourceTopic.starts_with(k_sys_topic_prefix)) {
        targetTopic = "status/" + sourceTopic.substr(k_sys_topic_prefix.size());
    }

    Qos targetQos = Qos::AtLeastOnce;
    if (config_.normalizeQosToAtLeastOnce) {
        targetQos = sourceMeta.qos == Qos::AtMostOnce ? Qos::AtMostOnce : Qos::AtLeastOnce;
    } else {
        targetQos = sourceMeta.qos;
    }

    const bool targetRetain = config_.retainPassthrough ? sourceMeta.retain : false;
    const bool targetDup = sourceMeta.dup && targetQos != Qos::AtMostOnce;

    Message mapped{targetTopic, message.value(), targetQos, targetRetain, targetDup};
    if (message.rawPayload().has_value()) {
        if (targetTopic == sourceTopic) {
            mapped.setRawPayload(*message.rawPayload());
        } else {
            const std::optional<std::string> rewrittenPayload =
                tryRewriteForwardedEnvelopeTopic(*message.rawPayload(), targetTopic);
            if (rewrittenPayload.has_value()) {
                mapped.setRawPayload(*rewrittenPayload);
            }
        }
    }
    for (const auto& reasonEntry : message.reason()) {
        mapped.addReason(reasonEntry.message, reasonEntry.timestamp);
    }

    return mapped;
}

} // namespace yaha
