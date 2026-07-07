#include "yaha/broker_connector/source_http_adapter.h"

#include "helper/string_helper.h"
#include "json/json_value.h"
#include "yaha/message/message_log_service.h"

#include <httplib.h>

#include <chrono>
#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr unsigned int k_uint16_max_value{65535U};
constexpr int k_http_status_ok{200};
constexpr int k_http_status_no_content{204};
constexpr int k_http_status_bad_request{400};
constexpr int k_connect_retry_count{50};
constexpr int k_connect_retry_delay_ms{20};
constexpr int k_suback_qos_reject_code{128};

[[nodiscard]] std::string buildSingleFieldJsonText(const std::string& fieldName, const std::string& fieldValue) {
    mqtt::json::JsonValue::Object rootObject{};
    rootObject.emplace(fieldName, mqtt::json::JsonValue{fieldValue});
    return mqtt::json::JsonValue{std::move(rootObject)}.stringify();
}

bool parseBool(const std::string& text, const bool defaultValue) {
    const std::string cleaned = mqtt::helper::toLower(mqtt::helper::trim(text));
    if (cleaned.empty()) {
        return defaultValue;
    }

    if (cleaned == "1" || cleaned == "true" || cleaned == "yes" || cleaned == "on") {
        return true;
    }
    if (cleaned == "0" || cleaned == "false" || cleaned == "no" || cleaned == "off") {
        return false;
    }

    return defaultValue;
}

bool parseUnsigned16(const std::string& text, std::uint16_t& outValue) {
    unsigned int parsed = 0U;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed > k_uint16_max_value) {
        return false;
    }

    outValue = static_cast<std::uint16_t>(parsed);
    return true;
}

Qos parseHeaderQos(const httplib::Request& request) {
    const std::string qosHeader = request.get_header_value("qos");
    if (qosHeader == "0") {
        return Qos::AtMostOnce;
    }
    if (qosHeader == "2") {
        return Qos::ExactlyOnce;
    }
    return Qos::AtLeastOnce;
}

bool tryParseJsonText(const std::string& jsonText, mqtt::json::JsonValue& parsedValue) {
    const auto parsed = mqtt::json::JsonValue::try_parse(jsonText);
    if (!parsed.has_value()) {
        return false;
    }

    parsedValue = *parsed;
    return true;
}

bool tryConvertJsonValueToMessageValue(const mqtt::json::JsonValue& valueNode, Value& valueOut) {
    if (valueNode.is_string()) {
        valueOut = valueNode.as_string();
        return true;
    }

    if (valueNode.is_number()) {
        valueOut = valueNode.as_number();
        return true;
    }

    if (valueNode.is_boolean()) {
        valueOut = valueNode.as_boolean() ? std::string{"true"} : std::string{"false"};
        return true;
    }

    if (valueNode.is_null()) {
        valueOut = std::string{"null"};
        return true;
    }

    return false;
}

bool tryParseReasonArray(const mqtt::json::JsonValue::Array& reasonArray,
                         ReasonList& reasonEntries) {
    reasonEntries.clear();
    reasonEntries.reserve(reasonArray.size());
    for (const auto& reasonNode : reasonArray) {
        if (!reasonNode.is_object() || !reasonNode.contains("message")) {
            return false;
        }

        const mqtt::json::JsonValue& messageNode = reasonNode.at("message");
        if (!messageNode.is_string()) {
            return false;
        }

        const std::string& messageText = messageNode.as_string();
        if (messageText.empty()) {
            return false;
        }

        std::string timestampText{};
        if (reasonNode.contains("timestamp")) {
            const mqtt::json::JsonValue& timestampNode = reasonNode.at("timestamp");
            if (!timestampNode.is_string()) {
                return false;
            }
            timestampText = timestampNode.as_string();
        }

        reasonEntries.push_back(ReasonEntry{.message = messageText, .timestamp = timestampText});
    }

    return true;
}

void appendReasonsPreservingOrder(const ReasonList& reasonEntries,
                                  Message& messageOut) {
    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        const ReasonEntry& entry = reasonEntries[reverseIndex - 1U];
        if (entry.timestamp.empty()) {
            messageOut.addReason(entry.message);
        } else {
            messageOut.addReason(entry.message, entry.timestamp);
        }
    }
}

bool parseIncomingMessageBody(const std::string& payload,
                              const Qos qos,
                              const bool retain,
                              Message& messageOut) {
    mqtt::json::JsonValue parsedRoot{};
    if (!tryParseJsonText(payload, parsedRoot) || !parsedRoot.is_object()) {
        return false;
    }

    const mqtt::json::JsonValue* messageNode = &parsedRoot;
    if (parsedRoot.contains("message")) {
        const mqtt::json::JsonValue& nestedMessage = parsedRoot.at("message");
        if (!nestedMessage.is_object()) {
            return false;
        }
        messageNode = &nestedMessage;
    }

    if (!messageNode->contains("topic") || !messageNode->contains("value")) {
        return false;
    }

    const mqtt::json::JsonValue& topicNode = messageNode->at("topic");
    if (!topicNode.is_string()) {
        return false;
    }

    const std::string topic = topicNode.as_string();
    if (topic.empty()) {
        return false;
    }

    Value value{};
    if (!tryConvertJsonValueToMessageValue(messageNode->at("value"), value)) {
        return false;
    }

    messageOut = Message{topic, std::move(value), qos, retain};

    if (!messageNode->contains("reason")) {
        return true;
    }

    const mqtt::json::JsonValue& reasonNode = messageNode->at("reason");
    if (reasonNode.is_array()) {
        ReasonList reasonEntries{};
        if (!tryParseReasonArray(reasonNode.as_array(), reasonEntries)) {
            return false;
        }

        appendReasonsPreservingOrder(reasonEntries, messageOut);
        return true;
    }

    if (reasonNode.is_object()) {
        return false;
    }

    Value reasonValue{};
    if (!tryConvertJsonValueToMessageValue(reasonNode, reasonValue)) {
        return false;
    }

    if (std::holds_alternative<std::string>(reasonValue)) {
        const std::string& reasonText = std::get<std::string>(reasonValue);
        if (!reasonText.empty()) {
            messageOut.addReason(reasonText);
        }
    }

    return true;
}

std::optional<std::vector<int>> tryParseQosArray(const std::string& payload) {
    mqtt::json::JsonValue parsedRoot{};
    if (!tryParseJsonText(payload, parsedRoot) || !parsedRoot.is_object() || !parsedRoot.contains("qos")) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& qosNode = parsedRoot.at("qos");
    if (!qosNode.is_array()) {
        return std::nullopt;
    }

    std::vector<int> qosValues{};
    qosValues.reserve(qosNode.as_array().size());
    for (const auto& qosValueNode : qosNode.as_array()) {
        if (!qosValueNode.is_number()) {
            return std::nullopt;
        }

        const double numericValue = qosValueNode.as_number();
        if (!std::isfinite(numericValue)
            || numericValue < static_cast<double>(std::numeric_limits<int>::min())
            || numericValue > static_cast<double>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }

        const int parsedValue = static_cast<int>(numericValue);
        if (std::fabs(numericValue - static_cast<double>(parsedValue)) > 0.0) {
            return std::nullopt;
        }

        qosValues.push_back(parsedValue);
    }

    return qosValues;
}

std::string qosValuesToText(const std::vector<int>& qosValues) {
    std::ostringstream stream{};
    stream << '[';
    bool first = true;
    for (const int qosValue : qosValues) {
        if (!first) {
            stream << ',';
        }
        first = false;
        stream << qosValue;
    }
    stream << ']';
    return stream.str();
}

std::optional<std::string> responseHeader(const httplib::Response& response,
                                          const std::string& key) {
    if (!response.has_header(key)) {
        return std::nullopt;
    }
    return response.get_header_value(key);
}

httplib::Headers makeStandardJsonHeaders() {
    return httplib::Headers{
        {"content-type", "application/json; charset=UTF-8"},
        {"accept", "application/json,text/plain"},
        {"accept-charset", "UTF-8"},
        {"version", "1.0"}
    };
}

bool tryBindListener(httplib::Server& server,
                     const SourceHttpBrokerConfig& config,
                     std::uint16_t& boundPort,
                     std::string& errorMessage) {
    std::string host = config.listenerBindHost;
    if (host.empty()) {
        host = config.listenerHost;
    }
    if (host.empty()) {
        host = "127.0.0.1";
    }

    if (config.listenerPort == 0U) {
        const int boundResult = server.bind_to_any_port(host);
        if (boundResult <= 0) {
            errorMessage = "unable to bind source callback listener to " + host + ":" +
                std::to_string(config.listenerPort);
            return false;
        }

        boundPort = static_cast<std::uint16_t>(boundResult);
        return true;
    }

    const bool isBound = server.bind_to_port(host, static_cast<int>(config.listenerPort));
    if (!isBound) {
        errorMessage = "unable to bind source callback listener to " + host + ":" +
            std::to_string(config.listenerPort);
        return false;
    }

    boundPort = config.listenerPort;
    return true;
}

} // namespace

SourceHttpBrokerAdapter::SourceHttpBrokerAdapter(SourceHttpBrokerConfig config)
    : config_(std::move(config)) {}

SourceHttpBrokerAdapter::~SourceHttpBrokerAdapter() {
    close();
}

void SourceHttpBrokerAdapter::setIncomingPublishCallback(SourcePublishCallback callback) {
    std::lock_guard<std::mutex> lock{stateMutex_};
    publishCallback_ = std::move(callback);
}

bool SourceHttpBrokerAdapter::connectAndSubscribe(std::string& errorMessage) {
    std::lock_guard<std::mutex> lock{stateMutex_};

    if (!startListener(errorMessage)) {
        connected_ = false;
        return false;
    }

    std::string connectSummary{};
    if (!sendConnect(errorMessage, connectSummary)) {
        connected_ = false;
        return false;
    }

    std::string subscribeSummary{};
    if (!sendSubscribe(errorMessage, subscribeSummary)) {
        connected_ = false;
        return false;
    }

    connected_ = true;
    errorMessage = connectSummary + " | " + subscribeSummary;
    return true;
}

bool SourceHttpBrokerAdapter::ping(std::string& errorMessage) {
    std::lock_guard<std::mutex> lock{stateMutex_};
    if (!connected_) {
        errorMessage = "source adapter not connected";
        return false;
    }

    const auto tryPingWithSendToken = [this](std::string& failureText) -> bool {
        httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
        const std::string payload = buildSingleFieldJsonText("token", sendToken_);
        httplib::Headers headers = makeStandardJsonHeaders();

        const auto response = client.Put("/pingreq", headers, payload, "application/json");
        if (!response) {
            failureText = "ping request failed using send token";
            return false;
        }

        if (response->status != k_http_status_no_content) {
            failureText = "ping failed with status " + std::to_string(response->status) +
                " using send token";
            return false;
        }

        const std::optional<std::string> packet = responseHeader(*response, "packet");
        if (!packet.has_value() || *packet != "pingresp") {
            failureText = "ping response missing packet=pingresp using send token";
            return false;
        }

        return true;
    };

    std::string sendFailure{};
    if (tryPingWithSendToken(sendFailure)) {
        return true;
    }

    connected_ = false;
    errorMessage = sendFailure;
    return false;
}

void SourceHttpBrokerAdapter::close() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (connected_) {
            (void)sendDisconnect();
        }
        connected_ = false;
        sendToken_.clear();
        receiveToken_.clear();
    }

    stopListener();
}

bool SourceHttpBrokerAdapter::isConnected() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return connected_;
}

std::uint16_t SourceHttpBrokerAdapter::listenerPort() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return boundListenerPort_;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool SourceHttpBrokerAdapter::startListener(std::string& errorMessage) {
    if (server_ != nullptr) {
        return true;
    }

    server_ = std::make_unique<httplib::Server>();

    server_->Put("/publish", [this](const httplib::Request& request, httplib::Response& response) {
        SourcePublishMeta meta{};
        meta.qos = parseHeaderQos(request);
        meta.retain = parseBool(request.get_header_value("retain"), false);
        meta.dup = parseBool(request.get_header_value("dup"), false);
        const std::string rawPacketIdHeader = request.get_header_value("packetid");
        const std::string cleanedPacketIdHeader = mqtt::helper::trim(rawPacketIdHeader);
        std::uint16_t packetId = 0U;
        if (parseUnsigned16(cleanedPacketIdHeader, packetId)) {
            meta.packetId = packetId;
        }

        if (meta.qos != Qos::AtMostOnce && cleanedPacketIdHeader.empty()) {
            std::cout << "  source: publish rejected missing packetid header=\""
                      << rawPacketIdHeader << "\" qos=" << static_cast<int>(meta.qos)
                      << '\n' << std::flush;
            response.status = k_http_status_bad_request;
            response.set_content(buildSingleFieldJsonText("error", "bad_publish_packetid"), "application/json");
            return;
        }

        Message message{"", std::string{}};
        if (!parseIncomingMessageBody(request.body, meta.qos, meta.retain, message)) {
            std::cout << "  source: publish rejected bad payload body=" << request.body
                      << '\n' << std::flush;
            response.status = k_http_status_bad_request;
            response.set_content(buildSingleFieldJsonText("error", "bad_publish_payload"), "application/json");
            return;
        }

        message.setRawPayload(request.body);

        if (config_.logIncomingMessages) {
            constexpr MessageLogConfig k_log_config{
                .enableIncoming = true,
                .enableOutgoing = false,
                .includeReasonChain = true,
            };

            Message logMessage{message.topic(), message.value(), meta.qos, meta.retain, meta.dup};
            for (const auto& reasonEntry : std::views::reverse(message.reason())) {
                logMessage.addReason(reasonEntry.message, reasonEntry.timestamp);
            }
            if (message.rawPayload().has_value()) {
                logMessage.setRawPayload(*message.rawPayload());
            }

            if (const auto line = buildMessageLogLine(
                    "broker_connector_source",
                    MessageLogDirection::Incoming,
                    logMessage,
                    k_log_config);
                line.has_value()) {
                std::cout << *line;
                if (meta.packetId.has_value()) {
                    std::cout << " packetid=" << *meta.packetId;
                }
                std::cout << '\n' << std::flush;
            }
        }

        SourcePublishCallback callback{};
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            callback = publishCallback_;
        }
        if (callback) {
            callback(message, meta);
        }

        response.status = k_http_status_no_content;
        response.set_header("content-type", "application/json; charset=UTF-8");
        response.set_header("version", "1.0");
        response.set_header("qos", request.get_header_value("qos"));
        response.set_header("retain", request.get_header_value("retain"));
        if (!rawPacketIdHeader.empty()) {
            response.set_header("packetid", rawPacketIdHeader);
        }
        if (meta.qos == Qos::AtLeastOnce) {
            response.set_header("packet", "puback");
        } else if (meta.qos == Qos::ExactlyOnce) {
            response.set_header("packet", "pubrec");
        }
    });

    server_->Put("/pubrel", [](const httplib::Request& request, httplib::Response& response) {
        response.status = k_http_status_no_content;
        response.set_header("content-type", "application/json; charset=UTF-8");
        response.set_header("version", "1.0");
        response.set_header("packet", "pubcomp");
        const std::string packetId = request.get_header_value("packetid");
        if (!packetId.empty()) {
            response.set_header("packetid", packetId);
        }
    });

    if (!tryBindListener(*server_, config_, boundListenerPort_, errorMessage)) {
        server_.reset();
        return false;
    }

    listenerThread_ = std::thread([this]() {
        if (server_ != nullptr) {
            server_->listen_after_bind();
        }
    });

    return true;
}

void SourceHttpBrokerAdapter::stopListener() {
    std::unique_ptr<httplib::Server> server{};
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (server_ != nullptr) {
            server = std::move(server_);
            boundListenerPort_ = 0U;
        }
    }

    if (server != nullptr) {
        server->stop();
    }
    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }
}

bool SourceHttpBrokerAdapter::sendConnect(std::string& errorMessage, std::string& responseSummary) {
    httplib::Headers headers = makeStandardJsonHeaders();
    const std::string payload = buildConnectPayload(config_, boundListenerPort_);

    httplib::Result response{};
    for (int attempt = 0; attempt < k_connect_retry_count; ++attempt) {
        httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
        response = client.Put("/connect", headers, payload, "application/json");
        if (response) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_connect_retry_delay_ms});
    }

    if (!response) {
        errorMessage = "source connect request failed";
        return false;
    }
    if (response->status != k_http_status_ok) {
        errorMessage = "source connect failed with status " + std::to_string(response->status);
        return false;
    }

    const std::optional<std::string> packet = responseHeader(*response, "packet");
    if (!packet.has_value() || *packet != "connack") {
        errorMessage = "source connect response missing packet=connack";
        return false;
    }

    mqtt::json::JsonValue responseBody{};
    if (!tryParseJsonText(response->body, responseBody)
        || !responseBody.is_object()
        || !responseBody.contains("token")
        || !responseBody.at("token").is_object()) {
        errorMessage = "source connect response missing token object";
        return false;
    }

    const mqtt::json::JsonValue& tokenObject = responseBody.at("token");
    if (!tokenObject.contains("send")
        || !tokenObject.contains("receive")
        || !tokenObject.at("send").is_string()
        || !tokenObject.at("receive").is_string()) {
        errorMessage = "source connect response has invalid token fields";
        return false;
    }

    std::string sendToken = tokenObject.at("send").as_string();
    std::string receiveToken = tokenObject.at("receive").as_string();
    if (sendToken.empty() || receiveToken.empty()) {
        errorMessage = "source connect response has invalid token fields";
        return false;
    }

    sendToken_ = std::move(sendToken);
    receiveToken_ = std::move(receiveToken);
    responseSummary = "connect_response status=" + std::to_string(response->status) +
        " packet=" + *packet + " body=" + response->body;
    return true;
}

bool SourceHttpBrokerAdapter::sendSubscribe(std::string& errorMessage, std::string& responseSummary) {
    httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
    httplib::Headers headers = makeStandardJsonHeaders();

    const std::uint16_t packetId = nextPacketId_ == 0U ? 1U : nextPacketId_++;
    headers.emplace("packetid", std::to_string(packetId));

    const std::string payload = buildSubscribePayload(config_);
    const auto response = client.Put("/subscribe", headers, payload, "application/json");
    if (!response) {
        errorMessage = "source subscribe request failed";
        return false;
    }
    if (response->status != k_http_status_ok) {
        errorMessage = "source subscribe failed with status " + std::to_string(response->status);
        return false;
    }

    const std::optional<std::string> packet = responseHeader(*response, "packet");
    if (!packet.has_value() || *packet != "suback") {
        errorMessage = "source subscribe response missing packet=suback";
        return false;
    }

    std::uint16_t responsePacketId = 0U;
    if (!parseUnsigned16(response->get_header_value("packetid"), responsePacketId) ||
        responsePacketId != packetId) {
        errorMessage = "source subscribe returned wrong packet id";
        return false;
    }

    const std::optional<std::vector<int>> qosValues = tryParseQosArray(response->body);
    if (!qosValues.has_value()) {
        errorMessage = "source subscribe response missing qos array";
        return false;
    }
    if (qosValues->size() != config_.subscribeTopics.size()) {
        errorMessage = "source subscribe response qos count mismatch";
        return false;
    }
    for (std::size_t idx = 0U; idx < qosValues->size(); ++idx) {
        const int qosValue = (*qosValues)[idx];
        if (qosValue == k_suback_qos_reject_code) {
            errorMessage =
                "source subscribe rejected topic index " + std::to_string(idx);
            return false;
        }
        if (qosValue < 0 || qosValue > 2) {
            errorMessage = "source subscribe returned invalid qos value";
            return false;
        }
    }

    responseSummary = "subscribe_response status=" + std::to_string(response->status) +
        " packet=" + *packet + " packetid=" + std::to_string(responsePacketId) +
        " qos=" + qosValuesToText(*qosValues) + " body=" + response->body;

    return true;
}

bool SourceHttpBrokerAdapter::sendDisconnect() const {
    httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
    httplib::Headers headers = makeStandardJsonHeaders();

    const std::string payload = buildSingleFieldJsonText("clientId", config_.clientId);
    const auto response = client.Put("/disconnect", headers, payload, "application/json");
    return response && response->status == k_http_status_no_content;
}

std::string SourceHttpBrokerAdapter::buildConnectPayload(const SourceHttpBrokerConfig& config,
                                                         const std::uint16_t effectiveListenerPort) {
    const std::string host = config.listenerHost.empty() ? "127.0.0.1" : config.listenerHost;
    const std::uint64_t keepAliveMilliseconds =
        static_cast<std::uint64_t>(config.keepAliveSeconds) * 1000U;

    mqtt::json::JsonValue::Object rootObject{};
    rootObject.emplace("clientId", mqtt::json::JsonValue{config.clientId});
    rootObject.emplace("host", mqtt::json::JsonValue{host});
    rootObject.emplace("port", mqtt::json::JsonValue{static_cast<double>(effectiveListenerPort)});
    rootObject.emplace("clean", mqtt::json::JsonValue{config.clean});
    rootObject.emplace("keepAlive", mqtt::json::JsonValue{static_cast<double>(keepAliveMilliseconds)});
    return mqtt::json::JsonValue{std::move(rootObject)}.stringify();
}

std::string SourceHttpBrokerAdapter::buildSubscribePayload(const SourceHttpBrokerConfig& config) {
    mqtt::json::JsonValue::Object topicsObject{};
    for (const auto& [topicFilter, qos] : config.subscribeTopics) {
        topicsObject.emplace(topicFilter, mqtt::json::JsonValue{static_cast<double>(static_cast<int>(qos))});
    }

    mqtt::json::JsonValue::Object rootObject{};
    rootObject.emplace("clientId", mqtt::json::JsonValue{config.clientId});
    rootObject.emplace("topics", mqtt::json::JsonValue{std::move(topicsObject)});
    return mqtt::json::JsonValue{std::move(rootObject)}.stringify();
}

} // namespace yaha
