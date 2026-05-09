#include "yaha/broker_connector/source_http_adapter.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::size_t k_escape_capacity_padding{8U};
constexpr unsigned int k_uint16_max_value{65535U};
constexpr int k_http_status_ok{200};
constexpr int k_http_status_no_content{204};
constexpr int k_http_status_bad_request{400};
constexpr int k_connect_retry_count{50};
constexpr int k_connect_retry_delay_ms{20};
constexpr int k_suback_qos_reject_code{128};
constexpr int k_decimal_base{10};

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
    escaped.reserve(text.size() + k_escape_capacity_padding);
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

bool tryFindArrayRange(const std::string& text,
                       const std::string& key,
                       std::size_t& arrayStart,
                       std::size_t& arrayEnd) {
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
    if (cursor >= text.size() || text[cursor] != '[') {
        return false;
    }

    int depth = 0;
    for (std::size_t pos = cursor; pos < text.size(); ++pos) {
        const char chr = text[pos];
        if (chr == '[') {
            ++depth;
        } else if (chr == ']') {
            --depth;
            if (depth == 0) {
                arrayStart = cursor;
                arrayEnd = pos;
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

    const std::string lowered = toLower(numericText);
    if (lowered == "true" || lowered == "false" || lowered == "null") {
        value = lowered;
        return true;
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(numericText.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return false;
    }

    value = parsedNumber;
    return true;
}

std::size_t skipReasonSeparators(const std::string& arrayText, std::size_t cursorPos) {
    while (cursorPos + 1U < arrayText.size() &&
           (std::isspace(static_cast<unsigned char>(arrayText[cursorPos])) != 0 ||
            arrayText[cursorPos] == ',')) {
        ++cursorPos;
    }

    return cursorPos;
}

std::optional<std::size_t> findObjectEnd(const std::string& text,
                                         const std::size_t objectStart) {
    if (objectStart >= text.size() || text[objectStart] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t cursorPos = objectStart; cursorPos < text.size(); ++cursorPos) {
        const char currentChar = text[cursorPos];
        if (currentChar == '{') {
            ++depth;
        } else if (currentChar == '}') {
            --depth;
            if (depth == 0) {
                return cursorPos;
            }
        }
    }

    return std::nullopt;
}

std::optional<ReasonEntry> tryParseReasonEntryObject(const std::string& objectText) {
    std::string messageText{};
    if (!tryExtractKeyStringValue(objectText, "message", messageText) || messageText.empty()) {
        return std::nullopt;
    }

    std::string timestampText{};
    if (!tryExtractKeyStringValue(objectText, "timestamp", timestampText)) {
        timestampText.clear();
    }

    return ReasonEntry{std::move(messageText), std::move(timestampText)};
}

std::optional<std::vector<ReasonEntry>> tryParseReasonArray(const std::string& arrayText) {
    if (arrayText.size() < 2U || arrayText.front() != '[' || arrayText.back() != ']') {
        return std::nullopt;
    }

    std::vector<ReasonEntry> reasonEntries{};
    std::size_t cursorPos = 1U;
    while (cursorPos + 1U < arrayText.size()) {
        cursorPos = skipReasonSeparators(arrayText, cursorPos);
        if (cursorPos + 1U >= arrayText.size() || arrayText[cursorPos] == ']') {
            break;
        }

        const std::optional<std::size_t> objectEnd = findObjectEnd(arrayText, cursorPos);
        if (!objectEnd.has_value()) {
            return std::nullopt;
        }

        const std::string objectText = arrayText.substr(cursorPos,
                                                        *objectEnd - cursorPos + 1U);
        const std::optional<ReasonEntry> reasonEntry = tryParseReasonEntryObject(objectText);
        if (!reasonEntry.has_value()) {
            return std::nullopt;
        }

        reasonEntries.push_back(*reasonEntry);
        cursorPos = *objectEnd + 1U;
    }

    return reasonEntries;
}

void appendReasonsPreservingOrder(const std::vector<ReasonEntry>& reasonEntries,
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

    std::size_t reasonArrayStart = 0U;
    std::size_t reasonArrayEnd = 0U;
    if (tryFindArrayRange(body, "reason", reasonArrayStart, reasonArrayEnd)) {
        const std::string reasonArray = body.substr(reasonArrayStart,
                                                    reasonArrayEnd - reasonArrayStart + 1U);
        const std::optional<std::vector<ReasonEntry>> reasonEntries =
            tryParseReasonArray(reasonArray);
        if (!reasonEntries.has_value()) {
            return false;
        }
        appendReasonsPreservingOrder(*reasonEntries, messageOut);
        return true;
    }

    if (tryExtractKeyValueToken(body, "reason", tokenStart, tokenEnd)) {
        Value reasonValue{};
        if (!tryParseValueToken(body, tokenStart, tokenEnd, reasonValue)) {
            return false;
        }
        if (std::holds_alternative<std::string>(reasonValue)) {
            const std::string& reasonText = std::get<std::string>(reasonValue);
            if (!reasonText.empty()) {
                messageOut.addReason(reasonText);
            }
        }
    }

    return true;
}

std::optional<std::vector<int>> tryParseQosArray(const std::string& payload) {
    const std::size_t qosKeyPos = payload.find("\"qos\"");
    if (qosKeyPos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t arrayStart = payload.find('[', qosKeyPos);
    if (arrayStart == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t arrayEnd = payload.find(']', arrayStart);
    if (arrayEnd == std::string::npos || arrayEnd < arrayStart) {
        return std::nullopt;
    }

    const std::string arrayBody = payload.substr(arrayStart + 1U, arrayEnd - arrayStart - 1U);

    std::vector<int> qosValues{};

    std::stringstream stream{arrayBody};
    std::string token{};
    while (std::getline(stream, token, ',')) {
        const std::string cleaned = trim(token);
        if (cleaned.empty()) {
            continue;
        }

        int parsedValue = 0;
        const char* begin = cleaned.data();
        const char* end = cleaned.data() + cleaned.size();
        const auto result = std::from_chars(begin, end, parsedValue, k_decimal_base);
        if (result.ec != std::errc{} || result.ptr != end) {
            return std::nullopt;
        }

        qosValues.push_back(parsedValue);
    }

    return qosValues;
}

std::string valueToText(const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    std::ostringstream stream{};
    stream << std::get<double>(value);
    return stream.str();
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
        const std::string payload = "{\"token\":\"" + escapeJson(sendToken_) + "\"}";
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
        const std::string cleanedPacketIdHeader = trim(rawPacketIdHeader);
        std::uint16_t packetId = 0U;
        if (parseUnsigned16(cleanedPacketIdHeader, packetId)) {
            meta.packetId = packetId;
        }

        if (meta.qos != Qos::AtMostOnce && !meta.packetId.has_value()) {
            std::cout << "  source: publish rejected invalid packetid header=\""
                      << escapeJson(rawPacketIdHeader) << "\" qos=" << static_cast<int>(meta.qos)
                      << '\n' << std::flush;
            response.status = k_http_status_bad_request;
            response.set_content("{\"error\":\"bad_publish_packetid\"}", "application/json");
            return;
        }

        Message message{"", std::string{}};
        if (!parseIncomingMessageBody(request.body, meta.qos, meta.retain, message)) {
            std::cout << "  source: publish rejected bad payload body=" << request.body
                      << '\n' << std::flush;
            response.status = k_http_status_bad_request;
            response.set_content("{\"error\":\"bad_publish_payload\"}", "application/json");
            return;
        }

        message.setRawPayload(request.body);

        std::cout << "  source: publish recv topic=" << message.topic()
                  << " qos=" << static_cast<int>(meta.qos)
                  << " retain=" << (meta.retain ? "1" : "0")
                  << " dup=" << (meta.dup ? "1" : "0")
                  << " value=" << valueToText(message.value());

        if (meta.packetId.has_value()) {
            std::cout << " packetid=" << *meta.packetId;
        }

        if (config_.logReason) {
            const std::string reasonText =
                message.reason().empty() ? "none" : message.reason().front().message;
            std::cout << " reason=\"" << escapeJson(reasonText) << '\"';
        }

        std::cout << '\n' << std::flush;

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

    const std::string payload = "{\"clientId\":\"" + escapeJson(config_.clientId) + "\"}";
    const auto response = client.Put("/disconnect", headers, payload, "application/json");
    return response && response->status == k_http_status_no_content;
}

std::string SourceHttpBrokerAdapter::buildConnectPayload(const SourceHttpBrokerConfig& config,
                                                         const std::uint16_t effectiveListenerPort) {
    const std::string host = config.listenerHost.empty() ? "127.0.0.1" : config.listenerHost;
    const std::uint64_t keepAliveMilliseconds =
        static_cast<std::uint64_t>(config.keepAliveSeconds) * 1000U;

    std::ostringstream stream{};
    stream << "{"
           << "\"clientId\":\"" << escapeJson(config.clientId) << "\"," 
           << "\"host\":\"" << escapeJson(host) << "\"," 
           << "\"port\":" << effectiveListenerPort << ','
           << "\"clean\":" << (config.clean ? "true" : "false") << ','
           << "\"keepAlive\":" << keepAliveMilliseconds
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
