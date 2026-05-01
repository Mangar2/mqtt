#include "yaha/broker_connector/source_http_adapter.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace yaha {

namespace {

std::string trim(const std::string& text) {
    std::size_t first = 0U;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1U])) != 0) {
        --last;
    }

    return text.substr(first, last - first);
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

std::string escapeJson(const std::string& text) {
    std::string escaped{};
    escaped.reserve(text.size() + 8U);
    for (const char chr : text) {
        switch (chr) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
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
                escaped.push_back(chr);
                break;
        }
    }
    return escaped;
}

bool parseBool(const std::string& text, const bool defaultValue) {
    const std::string cleaned = toLower(trim(text));
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
    if (result.ec != std::errc{} || result.ptr != end || parsed > 65535U) {
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
    for (std::size_t pos = cursor; pos < text.size(); ++pos) {
        const char chr = text[pos];
        if (chr == '{') {
            ++depth;
        } else if (chr == '}') {
            --depth;
            if (depth == 0) {
                objectStart = cursor;
                objectEnd = pos;
                return true;
            }
        }
    }

    return false;
}

bool tryExtractKeyStringValue(const std::string& objectText,
                              const std::string& key,
                              std::string& outValue) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = objectText.find(keyToken);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t cursor = objectText.find(':', keyPos + keyToken.size());
    if (cursor == std::string::npos) {
        return false;
    }
    ++cursor;

    while (cursor < objectText.size() &&
           std::isspace(static_cast<unsigned char>(objectText[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= objectText.size() || objectText[cursor] != '"') {
        return false;
    }

    ++cursor;
    std::string parsed{};
    while (cursor < objectText.size()) {
        const char chr = objectText[cursor];
        if (chr == '\\') {
            ++cursor;
            if (cursor >= objectText.size()) {
                return false;
            }
            parsed.push_back(objectText[cursor]);
            ++cursor;
            continue;
        }
        if (chr == '"') {
            outValue = std::move(parsed);
            return true;
        }
        parsed.push_back(chr);
        ++cursor;
    }

    return false;
}

bool tryExtractKeyValueToken(const std::string& objectText,
                             const std::string& key,
                             std::size_t& tokenStart,
                             std::size_t& tokenEnd) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = objectText.find(keyToken);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t cursor = objectText.find(':', keyPos + keyToken.size());
    if (cursor == std::string::npos) {
        return false;
    }
    ++cursor;

    while (cursor < objectText.size() &&
           std::isspace(static_cast<unsigned char>(objectText[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= objectText.size()) {
        return false;
    }

    tokenStart = cursor;
    if (objectText[cursor] == '"') {
        ++cursor;
        while (cursor < objectText.size()) {
            if (objectText[cursor] == '\\') {
                cursor += 2U;
                continue;
            }
            if (objectText[cursor] == '"') {
                tokenEnd = cursor;
                return true;
            }
            ++cursor;
        }
        return false;
    }

    while (cursor < objectText.size() && objectText[cursor] != ',' && objectText[cursor] != '}') {
        ++cursor;
    }
    tokenEnd = cursor == 0U ? 0U : cursor - 1U;
    return tokenEnd >= tokenStart;
}

bool tryParseValueToken(const std::string& objectText,
                        const std::size_t tokenStart,
                        const std::size_t tokenEnd,
                        Value& value) {
    if (tokenStart >= objectText.size() || tokenEnd >= objectText.size() || tokenStart > tokenEnd) {
        return false;
    }

    if (objectText[tokenStart] == '"') {
        const std::string token = objectText.substr(tokenStart, tokenEnd - tokenStart + 1U);
        std::string parsed{};
        if (!tryExtractKeyStringValue("{\"x\": " + token + "}", "x", parsed)) {
            return false;
        }
        value = parsed;
        return true;
    }

    const std::string numericText = trim(objectText.substr(tokenStart, tokenEnd - tokenStart + 1U));
    if (numericText.empty()) {
        return false;
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(numericText.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return false;
    }

    value = parsedNumber;
    return true;
}

bool parseIncomingMessageBody(const std::string& payload,
                              const Qos qos,
                              const bool retain,
                              Message& messageOut) {
    std::size_t messageStart = 0U;
    std::size_t messageEnd = 0U;
    std::string body = payload;
    if (tryFindObjectRange(payload, "message", messageStart, messageEnd)) {
        body = payload.substr(messageStart, messageEnd - messageStart + 1U);
    }

    std::string topic{};
    if (!tryExtractKeyStringValue(body, "topic", topic) || topic.empty()) {
        return false;
    }

    std::size_t tokenStart = 0U;
    std::size_t tokenEnd = 0U;
    if (!tryExtractKeyValueToken(body, "value", tokenStart, tokenEnd)) {
        return false;
    }

    Value value{};
    if (!tryParseValueToken(body, tokenStart, tokenEnd, value)) {
        return false;
    }

    messageOut = Message{topic, std::move(value), qos, retain};
    return true;
}

bool tryParseQosArray(const std::string& payload, std::vector<int>& qosValues) {
    const std::size_t qosKeyPos = payload.find("\"qos\"");
    if (qosKeyPos == std::string::npos) {
        return false;
    }

    std::size_t cursor = payload.find('[', qosKeyPos);
    if (cursor == std::string::npos) {
        return false;
    }
    ++cursor;

    while (cursor < payload.size()) {
        while (cursor < payload.size() &&
               std::isspace(static_cast<unsigned char>(payload[cursor])) != 0) {
            ++cursor;
        }

        if (cursor >= payload.size()) {
            return false;
        }
        if (payload[cursor] == ']') {
            return true;
        }

        bool negative = false;
        if (payload[cursor] == '-') {
            negative = true;
            ++cursor;
        }
        if (cursor >= payload.size() || !std::isdigit(static_cast<unsigned char>(payload[cursor]))) {
            return false;
        }

        int parsed = 0;
        while (cursor < payload.size() && std::isdigit(static_cast<unsigned char>(payload[cursor]))) {
            parsed = (parsed * 10) + (payload[cursor] - '0');
            ++cursor;
        }
        qosValues.push_back(negative ? -parsed : parsed);

        while (cursor < payload.size() &&
               std::isspace(static_cast<unsigned char>(payload[cursor])) != 0) {
            ++cursor;
        }

        if (cursor >= payload.size()) {
            return false;
        }
        if (payload[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (payload[cursor] == ']') {
            return true;
        }
        return false;
    }

    return false;
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

    httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
    const std::string payload = "{\"token\":\"" + escapeJson(receiveToken_) + "\"}";
    httplib::Headers headers = makeStandardJsonHeaders();

    const auto response = client.Put("/pingreq", headers, payload, "application/json");
    if (!response) {
        connected_ = false;
        errorMessage = "ping request failed";
        return false;
    }

    if (response->status != 204) {
        connected_ = false;
        errorMessage = "ping failed with status " + std::to_string(response->status);
        return false;
    }

    const std::optional<std::string> packet = responseHeader(*response, "packet");
    if (!packet.has_value() || *packet != "pingresp") {
        connected_ = false;
        errorMessage = "ping response missing packet=pingresp";
        return false;
    }

    return true;
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
        std::uint16_t packetId = 0U;
        if (parseUnsigned16(request.get_header_value("packetid"), packetId)) {
            meta.packetId = packetId;
        }

        Message message{"", std::string{}};
        if (!parseIncomingMessageBody(request.body, meta.qos, meta.retain, message)) {
            response.status = 400;
            response.set_content("{\"error\":\"bad_publish_payload\"}", "application/json");
            return;
        }

        SourcePublishCallback callback{};
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            callback = publishCallback_;
        }
        if (callback) {
            callback(message, meta);
        }

        response.status = 204;
        response.set_header("content-type", "application/json; charset=UTF-8");
        response.set_header("version", "1.0");
        response.set_header("qos", request.get_header_value("qos"));
        response.set_header("retain", request.get_header_value("retain"));
        if (meta.packetId.has_value()) {
            response.set_header("packetid", std::to_string(*meta.packetId));
        }
        if (meta.qos == Qos::AtLeastOnce) {
            response.set_header("packet", "puback");
        } else if (meta.qos == Qos::ExactlyOnce) {
            response.set_header("packet", "pubrec");
        }
    });

    server_->Put("/pubrel", [](const httplib::Request& request, httplib::Response& response) {
        response.status = 204;
        response.set_header("content-type", "application/json; charset=UTF-8");
        response.set_header("version", "1.0");
        response.set_header("packet", "pubcomp");
        const std::string packetId = request.get_header_value("packetid");
        if (!packetId.empty()) {
            response.set_header("packetid", packetId);
        }
    });

    std::string host = config_.listenerBindHost;
    if (host.empty()) {
        host = config_.listenerHost;
    }
    if (host.empty()) {
        host = "127.0.0.1";
    }
    int bound = 0;
    if (config_.listenerPort == 0U) {
        bound = server_->bind_to_any_port(host);
    } else {
        bound = server_->bind_to_port(host, static_cast<int>(config_.listenerPort));
    }
    if (bound <= 0) {
        errorMessage =
            "unable to bind source callback listener to " + host + ":" +
            std::to_string(config_.listenerPort);
        server_.reset();
        return false;
    }

    boundListenerPort_ = static_cast<std::uint16_t>(bound);
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
    for (int attempt = 0; attempt < 50; ++attempt) {
        httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
        response = client.Put("/connect", headers, payload, "application/json");
        if (response) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    if (!response) {
        errorMessage = "source connect request failed";
        return false;
    }
    if (response->status != 200) {
        errorMessage = "source connect failed with status " + std::to_string(response->status);
        return false;
    }

    const std::optional<std::string> packet = responseHeader(*response, "packet");
    if (!packet.has_value() || *packet != "connack") {
        errorMessage = "source connect response missing packet=connack";
        return false;
    }

    std::size_t tokenStart = 0U;
    std::size_t tokenEnd = 0U;
    if (!tryFindObjectRange(response->body, "token", tokenStart, tokenEnd)) {
        errorMessage = "source connect response missing token object";
        return false;
    }

    const std::string tokenObject = response->body.substr(tokenStart, tokenEnd - tokenStart + 1U);
    std::string sendToken{};
    std::string receiveToken{};
    if (!tryExtractKeyStringValue(tokenObject, "send", sendToken) ||
        !tryExtractKeyStringValue(tokenObject, "receive", receiveToken) ||
        sendToken.empty() || receiveToken.empty()) {
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
    if (response->status != 200) {
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

    std::vector<int> qosValues{};
    if (!tryParseQosArray(response->body, qosValues)) {
        errorMessage = "source subscribe response missing qos array";
        return false;
    }
    if (qosValues.size() != config_.subscribeTopics.size()) {
        errorMessage = "source subscribe response qos count mismatch";
        return false;
    }
    for (std::size_t idx = 0U; idx < qosValues.size(); ++idx) {
        const int qosValue = qosValues[idx];
        if (qosValue == 128) {
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
        " qos=" + qosValuesToText(qosValues) + " body=" + response->body;

    return true;
}

bool SourceHttpBrokerAdapter::sendDisconnect() {
    httplib::Client client{config_.brokerHost, static_cast<int>(config_.brokerPort)};
    httplib::Headers headers = makeStandardJsonHeaders();

    const std::string payload = "{\"clientId\":\"" + escapeJson(config_.clientId) + "\"}";
    const auto response = client.Put("/disconnect", headers, payload, "application/json");
    return response && response->status == 204;
}

std::string SourceHttpBrokerAdapter::buildConnectPayload(const SourceHttpBrokerConfig& config,
                                                         const std::uint16_t effectiveListenerPort) {
    const std::string host = config.listenerHost.empty() ? "127.0.0.1" : config.listenerHost;

    std::ostringstream stream{};
    stream << "{"
           << "\"clientId\":\"" << escapeJson(config.clientId) << "\"," 
           << "\"host\":\"" << escapeJson(host) << "\"," 
           << "\"port\":" << effectiveListenerPort << ','
           << "\"clean\":" << (config.clean ? "true" : "false") << ','
           << "\"keepAlive\":" << config.keepAliveSeconds
           << '}';
    return stream.str();
}

std::string SourceHttpBrokerAdapter::buildSubscribePayload(const SourceHttpBrokerConfig& config) {
    std::ostringstream stream{};
    stream << "{\"clientId\":\"" << escapeJson(config.clientId) << "\",\"topics\":{";

    bool first = true;
    for (const auto& [topicFilter, qos] : config.subscribeTopics) {
        if (!first) {
            stream << ',';
        }
        first = false;
        stream << '"' << escapeJson(topicFilter) << "\":" << static_cast<int>(qos);
    }

    stream << "}}";
    return stream.str();
}

} // namespace yaha
