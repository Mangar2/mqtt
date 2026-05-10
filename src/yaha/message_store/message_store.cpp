#include "yaha/message_store/message_store.h"
#include "yaha/message_store/message_store_json_parser.h"

#include "httplib.h"
#include "yaha/error_handling/yaha_error.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

namespace yaha {

namespace {

constexpr int k_hex_alpha_offset{10};
constexpr int k_http_status_ok{200};
constexpr int k_http_status_bad_request{400};
constexpr int k_http_status_not_found{404};
constexpr std::uint32_t k_default_level_amount{1U};
constexpr std::int64_t k_millis_per_second{1000};
constexpr int k_tm_year_offset{1900};

std::string trim(const std::string& value) {
    std::size_t begin = 0U;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        begin += 1U;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
        end -= 1U;
    }

    return value.substr(begin, end - begin);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char currentChar) {
        return static_cast<char>(std::tolower(currentChar));
    });
    return value;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.starts_with(prefix);
}

std::uint32_t parseUnsignedHeaderOrDefault(const std::string& text, std::uint32_t defaultValue);
bool parseBoolHeaderToken(const std::string& value, bool defaultValue);

bool isHexDigit(char currentChar) {
    return (currentChar >= '0' && currentChar <= '9')
        || (currentChar >= 'a' && currentChar <= 'f')
        || (currentChar >= 'A' && currentChar <= 'F');
}

int hexValue(char currentChar) {
    if (currentChar >= '0' && currentChar <= '9') {
        return currentChar - '0';
    }
    if (currentChar >= 'a' && currentChar <= 'f') {
        return k_hex_alpha_offset + (currentChar - 'a');
    }
    return k_hex_alpha_offset + (currentChar - 'A');
}

std::optional<std::string> decodePercentEncoding(const std::string& encoded) {
    std::string decoded{};
    decoded.reserve(encoded.size());

    for (std::size_t i = 0U; i < encoded.size(); ++i) {
        const char currentChar = encoded[i];
        if (currentChar != '%') {
            decoded.push_back(currentChar);
            continue;
        }

        if ((i + 2U) >= encoded.size()) {
            return std::nullopt;
        }

        const char highNibbleChar = encoded[i + 1U];
        const char lowNibbleChar = encoded[i + 2U];
        if (!isHexDigit(highNibbleChar) || !isHexDigit(lowNibbleChar)) {
            return std::nullopt;
        }

        const int value = (hexValue(highNibbleChar) << 4) | hexValue(lowNibbleChar);
        decoded.push_back(static_cast<char>(value));
        i += 2U;
    }

    return decoded;
}

std::string normalizeBasePath(const std::string& configuredPath) {
    std::string base = configuredPath.empty() ? "/store" : configuredPath;
    if (base.front() != '/') {
        base.insert(base.begin(), '/');
    }
    while (base.size() > 1U && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

std::string normalizeTopicPath(std::string topic) {
    while (!topic.empty() && topic.front() == '/') {
        topic.erase(topic.begin());
    }
    while (!topic.empty() && topic.back() == '/') {
        topic.pop_back();
    }
    return topic;
}

std::size_t countTopicLevels(const std::string& topicPath) {
    if (topicPath.empty()) {
        return 0U;
    }

    std::size_t levels = 1U;
    for (char currentChar : topicPath) {
        if (currentChar == '/') {
            levels += 1U;
        }
    }
    return levels;
}

bool isWithinRequestedLevel(const std::string& topic,
                            const std::string& topicPrefix,
                            std::uint32_t levelAmount) {
    const std::string normalizedTopic = normalizeTopicPath(topic);
    const std::string normalizedPrefix = normalizeTopicPath(topicPrefix);

    if (normalizedTopic.empty()) {
        return false;
    }

    if (normalizedPrefix.empty()) {
        return countTopicLevels(normalizedTopic) <= levelAmount;
    }

    if (normalizedTopic == normalizedPrefix) {
        return true;
    }

    const std::string prefixedPath = normalizedPrefix + "/";
    if (!startsWith(normalizedTopic, prefixedPath)) {
        return false;
    }

    const std::size_t topicDepth = countTopicLevels(normalizedTopic);
    const std::size_t prefixDepth = countTopicLevels(normalizedPrefix);
    return (topicDepth - prefixDepth) <= levelAmount;
}

struct ParsedHttpQuery {
    std::uint32_t levelAmount{k_default_level_amount};
    bool includeHistory{false};
    bool includeReason{true};
    bool useSnapshotMode{false};
    bool hasExplicitLevelAmount{false};
    std::string snapshotBody{};
    bool sensorPayloadParsed{false};
};

void applyPostQueryOptions(const httplib::Request& request,
                           std::string& topicPrefix,
                           ParsedHttpQuery& query) {
    message_store_json::SensorPostRequest sensorRequest{};
    if (!message_store_json::parseSensorPostBody(request.body, sensorRequest)) {
        return;
    }

    query.sensorPayloadParsed = true;
    topicPrefix = sensorRequest.topicPrefix.empty()
        ? topicPrefix
        : sensorRequest.topicPrefix;
    query.includeHistory = sensorRequest.includeHistory;
    query.includeReason = sensorRequest.includeReason;
    query.levelAmount = sensorRequest.levelAmount;
    query.hasExplicitLevelAmount = sensorRequest.hasLevelAmount;
    query.useSnapshotMode = sensorRequest.hasNodes;
    query.snapshotBody = sensorRequest.nodesJson;
}

void applyGetQueryOptions(const httplib::Request& request, ParsedHttpQuery& query) {
    if (request.has_header("levelamount")) {
        query.hasExplicitLevelAmount = true;
        query.levelAmount = parseUnsignedHeaderOrDefault(request.get_header_value("levelamount"),
                                                         k_default_level_amount);
    }

    if (request.has_header("history")) {
        query.includeHistory = parseBoolHeaderToken(request.get_header_value("history"), false);
    }

    if (request.has_header("reason")) {
        query.includeReason = parseBoolHeaderToken(request.get_header_value("reason"), true);
    }

    query.useSnapshotMode = !request.body.empty();
    query.snapshotBody = request.body;
}

std::vector<MessageTreeSnapshotNode> filterSnapshotByLevel(const std::vector<MessageTreeSnapshotNode>& snapshot,
                                                           const std::string& topicPrefix,
                                                           std::uint32_t levelAmount) {
    std::vector<MessageTreeSnapshotNode> filteredSnapshot{};
    filteredSnapshot.reserve(snapshot.size());
    for (const auto& snapshotNode : snapshot) {
        if (isWithinRequestedLevel(snapshotNode.topic, topicPrefix, levelAmount)) {
            filteredSnapshot.push_back(snapshotNode);
        }
    }
    return filteredSnapshot;
}

std::uint32_t parseUnsignedHeaderOrDefault(const std::string& text, std::uint32_t defaultValue) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return defaultValue;
    }

    std::uint32_t parsed = 0U;
    const auto [endPtr, errorCode] = std::from_chars(cleaned.data(),
                                                     cleaned.data() + cleaned.size(),
                                                     parsed,
                                                     10);
    if (errorCode != std::errc{} || endPtr != cleaned.data() + cleaned.size()) {
        return defaultValue;
    }

    return parsed;
}

bool parseBoolHeaderToken(const std::string& value, bool defaultValue) {
    const std::string normalized = toLower(trim(value));
    if (normalized.empty()) {
        return defaultValue;
    }

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return defaultValue;
}

std::string toIsoTimestamp(std::int64_t millisecondsSinceEpoch) {
    std::int64_t secondsSinceEpoch = millisecondsSinceEpoch / k_millis_per_second;
    std::int64_t millisecondPart = millisecondsSinceEpoch % k_millis_per_second;
    if (millisecondPart < 0) {
        millisecondPart += k_millis_per_second;
        secondsSinceEpoch -= 1;
    }

    const std::time_t rawTime = static_cast<std::time_t>(secondsSinceEpoch);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &rawTime) != 0) {
        return "1970-01-01T00:00:00.000Z";
    }
#else
    if (gmtime_r(&rawTime, &utc) == nullptr) {
        return "1970-01-01T00:00:00.000Z";
    }
#endif

    std::ostringstream stream{};
    stream << std::setfill('0')
            << std::setw(4) << (utc.tm_year + k_tm_year_offset)
           << '-' << std::setw(2) << (utc.tm_mon + 1)
           << '-' << std::setw(2) << utc.tm_mday
           << 'T' << std::setw(2) << utc.tm_hour
           << ':' << std::setw(2) << utc.tm_min
           << ':' << std::setw(2) << utc.tm_sec
           << '.' << std::setw(3) << millisecondPart
           << 'Z';
    return stream.str();
}

std::string jsonEscape(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char currentChar : text) {
        switch (currentChar) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(currentChar);
                break;
        }
    }
    return result;
}

std::string valueToJson(const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return "\"" + jsonEscape(std::get<std::string>(value)) + "\"";
    }

    std::ostringstream stream;
    stream << std::get<double>(value);
    return stream.str();
}

std::string reasonsToJson(const std::vector<ReasonEntry>& reasons) {
    std::string result{"["};
    for (std::size_t i = 0U; i < reasons.size(); ++i) {
        if (i > 0U) {
            result += ',';
        }
        result += "{\"message\":\"" + jsonEscape(reasons[i].message)
            + "\",\"timestamp\":\"" + jsonEscape(reasons[i].timestamp) + "\"}";
    }
    result += "]";
    return result;
}

std::string historyToJson(const std::vector<MessageTreeHistoryEntry>& history) {
    std::string result{"["};
    for (std::size_t i = 0U; i < history.size(); ++i) {
        if (i > 0U) {
            result += ',';
        }
        const MessageTreeHistoryEntry& item = history[i];
        result += "{\"time\":\"" + jsonEscape(toIsoTimestamp(item.timeMs)) + "\",\"value\":"
            + valueToJson(item.value) + ",\"reason\":" + reasonsToJson(item.reason) + '}';
    }
    result += "]";
    return result;
}

std::string nodeToJson(const MessageTreeNode& node) {
    std::string result{"{"};
    result += "\"topic\":\"" + jsonEscape(node.topic) + "\"";
    result += ",\"value\":" + valueToJson(node.value);
    result += ",\"time\":\"" + jsonEscape(toIsoTimestamp(node.timeMs)) + "\"";
    result += ",\"reason\":" + reasonsToJson(node.reason);
    result += ",\"history\":" + historyToJson(node.history);
    result += '}';
    return result;
}

std::string nodesToJson(const std::vector<MessageTreeNode>& nodes) {
    std::string result{"["};
    for (std::size_t i = 0U; i < nodes.size(); ++i) {
        if (i > 0U) {
            result += ',';
        }
        result += nodeToJson(nodes[i]);
    }
    result += "]";
    return result;
}

std::string wrapPayloadObject(const std::string& payloadJson) {
    return std::string{"{\"payload\":"} + payloadJson + '}';
}

void setHttpErrorResponse(httplib::Response& response, int status, const YahaError& error) {
    response.status = status;
    response.set_content(error.buildMessage(), "text/plain");
}

} // namespace

MessageStore::MessageStore(MessageStoreConfig config)
    : config_(std::move(config))
    , tree_(config_.treeConfig)
    , persistence_(config_.persistenceConfig) {}

MessageStore::~MessageStore() {
    close();
}

SubscriptionMap MessageStore::getSubscriptions() const {
    return config_.subscriptions;
}

void MessageStore::handleMessage(const Message& message) {
    Message::validate(message);

    if (message.topic() == config_.cleanupTopic) {
        const std::optional<std::uint32_t> days = parseCleanupDays(message.value());
        if (days.has_value()) {
            std::lock_guard<std::mutex> lock{treeStateMutex_};
            (void)tree_.cleanup(*days);
        }
        return;
    }

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    tree_.addData(message);
}

void MessageStore::run() {
    {
        std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
    }

    {
        std::lock_guard<std::mutex> lock{treeStateMutex_};
        (void)persistence_.restoreLatest(tree_);
    }

    startHttpServer();

    if (config_.httpStartCallback) {
        config_.httpStartCallback();
    }

    if (config_.persistenceConfig.intervalMs > 0U) {
        std::lock_guard<std::mutex> lock{treeStateMutex_};
        persistence_.startPeriodic(tree_);
    }
}

void MessageStore::close() {
    {
        std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
        if (!running_) {
            return;
        }
        running_ = false;
    }

    if (config_.httpStopCallback) {
        config_.httpStopCallback();
    }

    stopHttpServer();

    persistence_.stopPeriodic();

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    (void)persistence_.persistNow(tree_);
}

bool MessageStore::isRunning() const {
    std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
    return running_;
}

std::vector<MessageTreeNode>
MessageStore::querySection(const std::string& topicPrefix,
                           std::uint32_t levelAmount,
                           bool includeHistory,
                           bool includeReason) const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.getSection(topicPrefix, levelAmount, includeHistory, includeReason);
}

std::vector<MessageTreeNode>
MessageStore::queryNodes(const std::vector<MessageTreeSnapshotNode>& snapshot,
                         bool includeHistory,
                         bool includeReason) const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.getNodes(snapshot, includeHistory, includeReason);
}

void MessageStore::startHttpServer() {
    stopHttpServer();

    if (config_.serverPort == 0U) {
        return;
    }

    httpServer_ = std::make_unique<httplib::Server>();
    const std::string basePath = normalizeBasePath(config_.serverPath);

    httpServer_->Get(R"(/.*)", [this, basePath](const httplib::Request& request, httplib::Response& response) {
        handleHttpRequest(*this, basePath, request, response);
    });
    httpServer_->Post(R"(/.*)", [this, basePath](const httplib::Request& request, httplib::Response& response) {
        handleHttpRequest(*this, basePath, request, response);
    });

    const std::string host = config_.serverHost.empty() ? "127.0.0.1" : config_.serverHost;
    const std::uint16_t port = config_.serverPort;
    httpThread_ = std::thread([this, host, port]() {
        if (httpServer_ != nullptr) {
            (void)httpServer_->listen(host, static_cast<int>(port));
        }
    });
}

void MessageStore::stopHttpServer() {
    if (httpServer_ != nullptr) {
        httpServer_->stop();
    }
    if (httpThread_.joinable()) {
        httpThread_.join();
    }
    httpServer_.reset();
}

void MessageStore::handleHttpRequest(MessageStore& store,
                                     const std::string& basePath,
                                     const httplib::Request& request,
                                     httplib::Response& response) {
    const bool isPostRequest = (request.method == "POST");

    std::string topicPrefixEncoded;
    if (request.path == basePath) {
        topicPrefixEncoded.clear();
    } else if (startsWith(request.path, basePath + "/")) {
        topicPrefixEncoded = request.path.substr(basePath.size() + 1U);
    } else {
        setHttpErrorResponse(response,
                             k_http_status_not_found,
                             YahaError{"YAHA_MESSAGE_STORE_HTTP_NOT_FOUND",
                                       "not_found",
                                       "The requested HTTP path was not found.",
                                       "path=" + request.path + ", base_path=" + basePath});
        return;
    }

    const std::optional<std::string> topicPrefixDecoded = decodePercentEncoding(topicPrefixEncoded);
    if (!topicPrefixDecoded.has_value()) {
        setHttpErrorResponse(response,
                             k_http_status_bad_request,
                             YahaError{"YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING",
                                       "invalid_percent_encoding",
                                       "The request path contains invalid percent encoding.",
                                       "encoded_prefix=" + topicPrefixEncoded});
        return;
    }
    std::string topicPrefix = *topicPrefixDecoded;

    ParsedHttpQuery query{};
    if (isPostRequest) {
        applyPostQueryOptions(request, topicPrefix, query);
    } else {
        applyGetQueryOptions(request, query);
    }

    std::vector<MessageTreeNode> nodes{};
    if (!query.useSnapshotMode) {
        nodes = store.querySection(topicPrefix,
                                   query.levelAmount,
                                   query.includeHistory,
                                   query.includeReason);
    } else {
        std::vector<MessageTreeSnapshotNode> snapshot{};
        if (message_store_json::parseSnapshotBody(query.snapshotBody, snapshot)) {
            if (query.hasExplicitLevelAmount) {
                snapshot = filterSnapshotByLevel(snapshot, topicPrefix, query.levelAmount);
            }
            nodes = store.queryNodes(snapshot, query.includeHistory, query.includeReason);
        }
    }

    response.status = k_http_status_ok;
    const std::string nodesJson = nodesToJson(nodes);
    if (isPostRequest && query.sensorPayloadParsed) {
        response.set_content(wrapPayloadObject(nodesJson), "application/json");
        return;
    }

    response.set_content(nodesJson, "application/json");
}

std::optional<std::uint32_t> MessageStore::parseCleanupDays(const Value& value) {
    if (std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (!std::isfinite(number) || number < 0.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(number);
    }

    const std::string& text = std::get<std::string>(value);
    if (text.empty()) {
        return std::nullopt;
    }

    char* endPtr = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &endPtr, 10);
    if (endPtr == nullptr || *endPtr != '\0') {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(parsed);
}

bool MessageStore::parseBoolHeaderValue(const std::string& value, bool defaultValue) {
    const std::string cleaned = toLower(trim(value));
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

} // namespace yaha
