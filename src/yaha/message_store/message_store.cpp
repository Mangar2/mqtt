#include "yaha/message_store/message_store.h"
#include "yaha/message_store/message_store_json_parser.h"
#include "yaha/message_store/message_store_stats_printer.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/message_store/iso_timestamp_parser.h"

#include "helper/string_helper.h"
#include "httplib.h"
#include "json/json_value.h"
#include "yaha/error_handling/yaha_error.h"

#include <charconv>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

constexpr int k_hex_alpha_offset{10};
constexpr int k_http_status_ok{200};
constexpr int k_http_status_no_content{204};
constexpr int k_http_status_bad_request{400};
constexpr int k_http_status_not_found{404};
constexpr std::uint32_t k_default_level_amount{1U};
constexpr std::int64_t k_millis_per_second{1000};
constexpr int k_tm_year_offset{1900};
constexpr std::string_view k_store_cors_methods{"GET, POST, OPTIONS"};
constexpr std::string_view k_store_cors_headers{
    "Content-Type, Authorization, X-Requested-With, history, levelamount, reason, time"};
constexpr std::chrono::seconds k_compression_stats_interval{60};
constexpr std::chrono::milliseconds k_compression_stats_poll_interval{100};

void applyStoreCorsHeaders(httplib::Response& response, const bool includeMaxAge) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", std::string{k_store_cors_methods});
    response.set_header("Access-Control-Allow-Headers", std::string{k_store_cors_headers});
    if (includeMaxAge) {
        response.set_header("Access-Control-Max-Age", "86400");
    }
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
    if (!normalizedTopic.starts_with(prefixedPath)) {
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
    bool includeTime{true};
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
    query.includeTime = sensorRequest.includeTime;
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

    if (request.has_header("time")) {
        query.includeTime = parseBoolHeaderToken(request.get_header_value("time"), true);
    }

    query.useSnapshotMode = !request.body.empty();
    query.snapshotBody = request.body;
}

std::vector<MessageSnapshot> filterSnapshotByLevel(const std::vector<MessageSnapshot>& snapshot,
                                                           const std::string& topicPrefix,
                                                           std::uint32_t levelAmount) {
    std::vector<MessageSnapshot> filteredSnapshot{};
    filteredSnapshot.reserve(snapshot.size());
    for (const auto& snapshotNode : snapshot) {
        if (isWithinRequestedLevel(snapshotNode.topic, topicPrefix, levelAmount)) {
            filteredSnapshot.push_back(snapshotNode);
        }
    }
    return filteredSnapshot;
}

std::uint32_t parseUnsignedHeaderOrDefault(const std::string& text, std::uint32_t defaultValue) {
    const std::string cleaned = mqtt::helper::trim(text);
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
    const std::string normalized = mqtt::helper::toLower(mqtt::helper::trim(value));
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

    const auto rawTime = static_cast<std::time_t>(secondsSinceEpoch);
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

mqtt::json::JsonValue valueToJsonValue(const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return mqtt::json::JsonValue{std::get<std::string>(value)};
    }

    return mqtt::json::JsonValue{std::get<double>(value)};
}

mqtt::json::JsonValue reasonsToJsonValue(const ReasonList& reasons) {
    mqtt::json::JsonValue::Array reasonArray{};
    reasonArray.reserve(reasons.size());
    for (const auto& reasonEntry : reasons) {
        reasonArray.emplace_back(mqtt::json::JsonValue::Object{
            {"message", mqtt::json::JsonValue{reasonEntry.message}},
            {"timestamp", mqtt::json::JsonValue{reasonEntry.timestamp}},
        });
    }

    return mqtt::json::JsonValue{std::move(reasonArray)};
}

mqtt::json::JsonValue historyToJsonValue(const std::vector<MessageTreeHistoryEntry>& history,
                                         const bool includeReason,
                                         const bool includeTime) {
    mqtt::json::JsonValue::Array historyArray{};
    historyArray.reserve(history.size());
    for (const auto& historyEntry : history) {
        mqtt::json::JsonValue::Object historyObject{
            {"value", valueToJsonValue(historyEntry.value)},
        };

        if (includeTime) {
            historyObject["time"] = mqtt::json::JsonValue{toIsoTimestamp(historyEntry.timeMs)};
        }

        if (includeReason) {
            historyObject["reason"] = reasonsToJsonValue(historyEntry.reason());
        }

        historyArray.emplace_back(std::move(historyObject));
    }

    return mqtt::json::JsonValue{std::move(historyArray)};
}

mqtt::json::JsonValue nodeToJsonValue(const MessageTreeNode& node,
                                      const bool includeHistory,
                                      const bool includeReason,
                                      const bool includeTime) {
    mqtt::json::JsonValue::Object nodeObject{
        {"topic", mqtt::json::JsonValue{node.topic}},
        {"value", valueToJsonValue(node.value)},
    };

    if (includeTime) {
        nodeObject["time"] = mqtt::json::JsonValue{toIsoTimestamp(node.timeMs)};
    }

    if (includeReason) {
        nodeObject["reason"] = reasonsToJsonValue(node.reason());
    }

    if (includeHistory) {
        nodeObject["history"] = historyToJsonValue(node.history(), includeReason, includeTime);
    }

    return mqtt::json::JsonValue{std::move(nodeObject)};
}

std::string nodesToJson(const std::vector<MessageTreeNode>& nodes,
                        const bool includeHistory,
                        const bool includeReason,
                        const bool includeTime) {
    mqtt::json::JsonValue::Array nodesArray{};
    nodesArray.reserve(nodes.size());
    for (const auto& node : nodes) {
        nodesArray.push_back(nodeToJsonValue(node, includeHistory, includeReason, includeTime));
    }

    return mqtt::json::JsonValue{std::move(nodesArray)}.stringify();
}

std::string wrapPayloadObject(const std::string& payloadJson) {
    const auto payloadValue = mqtt::json::JsonValue::try_parse(payloadJson);
    if (!payloadValue.has_value()) {
        return std::string{"{\"payload\":"} + payloadJson + '}';
    }

    return mqtt::json::JsonValue{mqtt::json::JsonValue::Object{{"payload", *payloadValue}}}.stringify();
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

    appendReplayIncomingMessage(message);

    if (message.topic() == config_.cleanupTopic) {
        const std::optional<std::uint32_t> days = parseCleanupDays(message.value());
        if (days.has_value()) {
            std::lock_guard<std::mutex> lock{treeStateMutex_};
            (void)tree_.cleanup(*days);
        } else {
            std::cout << "message_store[error] op=cleanup reason=invalid_payload topic="
                      << config_.cleanupTopic
                      << '\n' << std::flush;
        }
        return;
    }

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    tree_.addData(message);
}

void MessageStore::storeMessageDirect(const Message& message) {
    Message::validate(message);

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
        try {
            if (!persistence_.restoreLatest(tree_)) {
                std::cout << "message_store[error] op=restore_latest reason=no_valid_snapshot"
                          << '\n' << std::flush;
            }
        } catch (const std::exception& exceptionValue) {
            std::cout << "message_store[error] op=restore_latest reason=exception details=\""
                      << exceptionValue.what() << "\""
                      << '\n' << std::flush;
        } catch (...) {
            std::cout << "message_store[error] op=restore_latest reason=exception details=\"unknown\""
                      << '\n' << std::flush;
        }

        writeReplayLoadedStateLocked();
        logCompressionStatsLineLocked("start_after_restore");
        replayIncomingCaptureEnabled_.store(true);
    }

    startCompressionStatsLogging();

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

    stopCompressionStatsLogging();

    if (config_.httpStopCallback) {
        config_.httpStopCallback();
    }

    stopHttpServer();

    persistence_.stopPeriodic();

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    replayIncomingCaptureEnabled_.store(false);
    logCompressionStatsLineLocked("stop_after_signal");
    try {
        if (!persistence_.persistNow(tree_)) {
            std::cout << "message_store[error] op=persist_final reason=persist_failed"
                      << '\n' << std::flush;
        }
    } catch (const std::exception& exceptionValue) {
        std::cout << "message_store[error] op=persist_final reason=exception details=\""
                  << exceptionValue.what() << "\""
                  << '\n' << std::flush;
    } catch (...) {
        std::cout << "message_store[error] op=persist_final reason=exception details=\"unknown\""
                  << '\n' << std::flush;
    }
}

void MessageStore::startCompressionStatsLogging() {
    stopCompressionStatsLogging();

    compressionStatsStopRequested_.store(false);
    compressionStatsThread_ = std::thread([this]() {
        std::chrono::milliseconds elapsed{0};
        while (!compressionStatsStopRequested_.load()) {
            std::this_thread::sleep_for(k_compression_stats_poll_interval);
            if (compressionStatsStopRequested_.load()) {
                return;
            }

            elapsed += k_compression_stats_poll_interval;
            if (elapsed < std::chrono::duration_cast<std::chrono::milliseconds>(k_compression_stats_interval)) {
                continue;
            }

            elapsed = std::chrono::milliseconds{0};
            std::lock_guard<std::mutex> lock{treeStateMutex_};
            logCompressionStatsLineLocked("periodic_60s");
        }
    });
}

void MessageStore::stopCompressionStatsLogging() {
    compressionStatsStopRequested_.store(true);
    if (compressionStatsThread_.joinable()) {
        compressionStatsThread_.join();
    }
}

void MessageStore::logCompressionStatsLineLocked(std::string_view phaseText) const {
    const MessageTree::CompressionStats compressionStats = tree_.compressionStats();
    printMessageStoreCompressionStatsLine(compressionStats, phaseText);
}

void MessageStore::writeReplayLoadedStateLocked() {
    if (config_.replayLoadedStateFile.empty()) {
        return;
    }

    const std::vector<MessageTreeNode> nodes = tree_.getSection(
        "",
        std::numeric_limits<std::uint32_t>::max(),
        true,
        true);

    std::lock_guard<std::mutex> replayLock{replayFileMutex_};
    const std::filesystem::path replayPath = config_.replayLoadedStateFile;
    if (!replayPath.parent_path().empty()) {
        std::error_code createError;
        std::filesystem::create_directories(replayPath.parent_path(), createError);
        if (createError) {
            std::cout << "message_store[error] op=replay_dump_loaded_state reason=create_directories_failed file="
                      << replayPath.string() << " details=\"" << createError.message() << "\""
                      << '\n' << std::flush;
            return;
        }
    }

    std::ofstream output{replayPath, std::ios::trunc};
    if (!output.is_open()) {
        std::cout << "message_store[error] op=replay_dump_loaded_state reason=open_failed file="
                  << replayPath.string() << '\n' << std::flush;
        return;
    }

    std::uint64_t replayMessageCount = 0U;
    for (const MessageTreeNode& node : nodes) {
        const std::vector<ReplayRow> replayRows = buildReplayRowsForNode(node);
        for (const ReplayRow& replayRow : replayRows) {
            const Message replayMessage = buildReplayMessage(node.topic, replayRow);
            output << buildEnvelopePayload(replayMessage) << '\n';
            replayMessageCount += 1U;
        }
    }

    output.flush();
    if (!output.good()) {
        std::cout << "message_store[error] op=replay_dump_loaded_state reason=write_failed file="
                  << replayPath.string() << '\n' << std::flush;
        return;
    }

    std::cout << "message_store[replay] op=dump_loaded_state file=" << replayPath.string()
              << " messages=" << replayMessageCount << '\n' << std::flush;
}

void MessageStore::appendReplayIncomingMessage(const Message& message) {
    if (!replayIncomingCaptureEnabled_.load() || config_.replayIncomingMessagesFile.empty()) {
        return;
    }

    std::lock_guard<std::mutex> replayLock{replayFileMutex_};
    const std::filesystem::path replayPath = config_.replayIncomingMessagesFile;
    if (!replayPath.parent_path().empty()) {
        std::error_code createError;
        std::filesystem::create_directories(replayPath.parent_path(), createError);
        if (createError) {
            std::cout << "message_store[error] op=replay_dump_incoming reason=create_directories_failed file="
                      << replayPath.string() << " details=\"" << createError.message() << "\""
                      << '\n' << std::flush;
            return;
        }
    }

    std::ofstream output{replayPath, std::ios::app};
    if (!output.is_open()) {
        std::cout << "message_store[error] op=replay_dump_incoming reason=open_failed file="
                  << replayPath.string() << '\n' << std::flush;
        return;
    }

    output << buildEnvelopePayload(message) << '\n';
    output.flush();
    if (!output.good()) {
        std::cout << "message_store[error] op=replay_dump_incoming reason=write_failed file="
                  << replayPath.string() << '\n' << std::flush;
    }
}

Message MessageStore::buildReplayMessage(const std::string& topicPath,
                                         const ReplayRow& replayRow) {
    Message replayMessage{topicPath, replayRow.value};
    if (replayRow.reason.empty()) {
        return replayMessage;
    }

    for (auto reverseIndex = replayRow.reason.rbegin(); reverseIndex != replayRow.reason.rend(); ++reverseIndex) {
        std::string timestamp = reverseIndex->timestamp;
        if (timestamp.empty()) {
            timestamp = toIsoTimestampMilliseconds(replayRow.timeMs);
        }
        replayMessage.addReason(reverseIndex->message, std::move(timestamp));
    }

    return replayMessage;
}

std::vector<MessageStore::ReplayRow>
MessageStore::buildReplayRowsForNode(const MessageTreeNode& node) {
    std::vector<ReplayRow> replayRows{};
    replayRows.reserve(node.history().size() + 1U);

    for (std::size_t reverseIndex = node.history().size(); reverseIndex > 0U; --reverseIndex) {
        const MessageTreeHistoryEntry& historyEntry = node.history()[reverseIndex - 1U];
        replayRows.push_back(ReplayRow{
            .value = historyEntry.value,
            .reason = historyEntry.reason(),
            .timeMs = historyEntry.timeMs,
        });
    }

    replayRows.push_back(ReplayRow{
        .value = node.value,
        .reason = node.reason(),
        .timeMs = node.timeMs,
    });
    return replayRows;
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
MessageStore::queryNodes(const std::vector<MessageSnapshot>& snapshot,
                         bool includeHistory,
                         bool includeReason) const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.getNodes(snapshot, includeHistory, includeReason);
}

MessageTree::CompressionStats MessageStore::queryCompressionStats() const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.compressionStats();
}

std::optional<std::filesystem::path> MessageStore::persistSnapshotNow() {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return persistence_.persistNowWithPath(tree_);
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
    httpServer_->Options(R"(/.*)", [basePath](const httplib::Request& request, httplib::Response& response) {
        handleHttpOptionsRequest(basePath, request, response);
    });

    const std::string host = config_.serverHost.empty() ? "127.0.0.1" : config_.serverHost;
    const std::uint16_t port = config_.serverPort;
    httpThread_ = std::thread([this, host, port]() {
        if (httpServer_ != nullptr) {
            if (!httpServer_->listen(host, static_cast<int>(port))) {
                std::cout << "message_store[error] op=http_listen host=" << host
                          << " port=" << port
                          << " reason=listen_failed"
                          << '\n' << std::flush;
            }
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
    applyStoreCorsHeaders(response, false);
    const bool isPostRequest = (request.method == "POST");

    std::string topicPrefixEncoded;
    if (request.path == basePath) {
        topicPrefixEncoded.clear();
    } else if (request.path.starts_with(basePath + "/")) {
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
        std::vector<MessageSnapshot> snapshot{};
        if (message_store_json::parseSnapshotBody(query.snapshotBody, snapshot)) {
            if (query.hasExplicitLevelAmount) {
                snapshot = filterSnapshotByLevel(snapshot, topicPrefix, query.levelAmount);
            }
            nodes = store.queryNodes(snapshot, query.includeHistory, query.includeReason);
        }
    }

    response.status = k_http_status_ok;
    const std::string nodesJson = nodesToJson(nodes,
                                              query.includeHistory,
                                              query.includeReason,
                                              query.includeTime);
    if (isPostRequest && query.sensorPayloadParsed) {
        response.set_content(wrapPayloadObject(nodesJson), "application/json");
        return;
    }

    response.set_content(nodesJson, "application/json");
}

void MessageStore::handleHttpOptionsRequest(const std::string& basePath,
                                            const httplib::Request& request,
                                            httplib::Response& response) {
    applyStoreCorsHeaders(response, true);

    if (request.path == basePath || request.path.starts_with(basePath + "/")) {
        response.status = k_http_status_no_content;
        response.set_content("", "text/plain");
        return;
    }

    setHttpErrorResponse(response,
                         k_http_status_not_found,
                         YahaError{"YAHA_MESSAGE_STORE_HTTP_NOT_FOUND",
                                   "not_found",
                                   "The requested HTTP path was not found.",
                                   "path=" + request.path + ", base_path=" + basePath});
}

std::optional<std::uint32_t> MessageStore::parseCleanupDays(const Value& value) {
    if (std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (!std::isfinite(number) || number < 0.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(number);
    }

    const auto& text = std::get<std::string>(value);
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
    const std::string cleaned = mqtt::helper::toLower(mqtt::helper::trim(value));
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
