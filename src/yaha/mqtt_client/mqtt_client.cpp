#include "yaha/mqtt_client/mqtt_client.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace yaha {

namespace {

std::vector<std::string> splitTopic(const std::string& text) {
    std::vector<std::string> segments{};
    std::string current{};
    for (const char chr : text) {
        if (chr == '/') {
            segments.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(chr);
    }
    segments.push_back(current);
    return segments;
}

} // namespace

YahaMqttClient::YahaMqttClient(Config config, IMqttComponent& component, Transport transport)
    : config_(std::move(config))
    , component_(component)
    , transport_(std::move(transport)) {}

YahaMqttClient::~YahaMqttClient() {
    close();
}

void YahaMqttClient::run() {
    std::lock_guard<std::mutex> lock{stateMutex_};
    if (running_) {
        return;
    }
    if (!transport_.connect || !transport_.disconnect || !transport_.publish ||
        !transport_.subscribe || !transport_.pollIncoming || !transport_.ping ||
        !transport_.isConnected) {
        throw std::invalid_argument{"YahaMqttClient transport callbacks must be set"};
    }

    component_.setPublishCallback([this](const Message& message) {
        this->publish(message);
    });

    running_ = true;
    workerThread_ = std::thread{&YahaMqttClient::workerLoop, this};
}

void YahaMqttClient::close() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (!running_ && !connected_) {
            return;
        }
        running_ = false;
    }

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    bool was_connected = false;
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        was_connected = connected_;
        connected_ = false;
    }
    if (was_connected) {
        transport_.disconnect();
    }
}

void YahaMqttClient::publish(const Message& message) {
    Message::validate(message);

    std::lock_guard<std::mutex> lock{stateMutex_};
    if (!connected_) {
        throw std::runtime_error{"YahaMqttClient publish requested while disconnected"};
    }
    transport_.publish(message);
}

bool YahaMqttClient::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

bool YahaMqttClient::isConnected() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return connected_;
}

void YahaMqttClient::workerLoop() {
    while (isRunning()) {
        if (!ensureConnected()) {
            std::this_thread::sleep_for(config_.reconnectDelay);
            continue;
        }

        processIncoming();
        processKeepAlive();

        if (!transport_.isConnected()) {
            std::lock_guard<std::mutex> lock{stateMutex_};
            connected_ = false;
        }

        std::this_thread::sleep_for(config_.loopSleep);
    }
}

bool YahaMqttClient::ensureConnected() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (connected_) {
            return true;
        }
    }

    if (!transport_.connect(config_)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        connected_ = true;
        lastPingAt_ = std::chrono::steady_clock::now();
    }

    replaySubscriptions();
    return true;
}

void YahaMqttClient::replaySubscriptions() {
    const SubscriptionMap subscriptions = component_.getSubscriptions();
    for (const auto& [topic_filter, qos_level] : subscriptions) {
        transport_.subscribe(topic_filter, qos_level);
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    activeSubscriptions_ = subscriptions;
}

void YahaMqttClient::processIncoming() {
    const std::optional<Message> maybe_message = transport_.pollIncoming();
    if (!maybe_message.has_value()) {
        return;
    }

    Message::validate(*maybe_message);
    if (!isTopicSubscribed(maybe_message->topic())) {
        return;
    }

    component_.handleMessage(*maybe_message);
}

void YahaMqttClient::processKeepAlive() {
    const auto now = std::chrono::steady_clock::now();
    bool should_ping = false;

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (connected_ && (now - lastPingAt_ >= config_.keepAliveInterval)) {
            should_ping = true;
            lastPingAt_ = now;
        }
    }

    if (should_ping) {
        transport_.ping();
    }
}

bool YahaMqttClient::isTopicSubscribed(const std::string& topic) const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    for (const auto& [topic_filter, qos_level] : activeSubscriptions_) {
        (void)qos_level;
        if (topicMatchesFilter(topic_filter, topic)) {
            return true;
        }
    }
    return false;
}

bool YahaMqttClient::topicMatchesFilter(const std::string& filter,
                                        const std::string& topic) {
    const std::vector<std::string> filter_segments = splitTopic(filter);
    const std::vector<std::string> topic_segments = splitTopic(topic);

    std::size_t idx = 0U;
    for (; idx < filter_segments.size(); ++idx) {
        const std::string& filter_segment = filter_segments[idx];

        if (filter_segment == "#") {
            return idx == filter_segments.size() - 1U;
        }

        if (idx >= topic_segments.size()) {
            return false;
        }

        if (filter_segment == "+") {
            continue;
        }

        if (filter_segment != topic_segments[idx]) {
            return false;
        }
    }

    return idx == topic_segments.size();
}

} // namespace yaha
