#include "yaha/message_store/message_store.h"

#include "httplib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace yaha {

namespace {

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
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0U, prefix.size(), prefix) == 0;
}

bool isHexDigit(char c) {
    return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return 10 + (c - 'A');
}

bool tryDecodePercentEncoding(const std::string& encoded, std::string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());

    for (std::size_t i = 0U; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c != '%') {
            decoded.push_back(c);
            continue;
        }

        if ((i + 2U) >= encoded.size()) {
            return false;
        }

        const char hi = encoded[i + 1U];
        const char lo = encoded[i + 2U];
        if (!isHexDigit(hi) || !isHexDigit(lo)) {
            return false;
        }

        const int value = (hexValue(hi) << 4) | hexValue(lo);
        decoded.push_back(static_cast<char>(value));
        i += 2U;
    }

    return true;
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

bool tryParseUnsignedHeader(const std::string& text, std::uint32_t defaultValue, std::uint32_t& output) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        output = defaultValue;
        return true;
    }

    char* endPtr = nullptr;
    const unsigned long parsed = std::strtoul(cleaned.c_str(), &endPtr, 10);
    if (endPtr == nullptr || *endPtr != '\0') {
        output = defaultValue;
        return false;
    }

    output = static_cast<std::uint32_t>(parsed);
    return true;
}

std::string jsonEscape(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
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
                result.push_back(c);
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
        result += "{\"timeMs\":" + std::to_string(item.timeMs) + ",\"value\":"
            + valueToJson(item.value) + ",\"reason\":" + reasonsToJson(item.reason) + '}';
    }
    result += "]";
    return result;
}

std::string nodeToJson(const MessageTreeNode& node) {
    std::string result{"{"};
    result += "\"topic\":\"" + jsonEscape(node.topic) + "\"";
    result += ",\"value\":" + valueToJson(node.value);
    result += ",\"timeMs\":" + std::to_string(node.timeMs);
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

class SnapshotJsonParser {
public:
    explicit SnapshotJsonParser(std::string json)
        : json_(std::move(json)) {}

    bool parse(std::vector<MessageTreeSnapshotNode>& out) {
        skipWs();
        if (!consume('[')) {
            return false;
        }
        skipWs();
        if (consume(']')) {
            return true;
        }

        while (true) {
            MessageTreeSnapshotNode node{};
            if (!parseObject(node)) {
                return false;
            }
            out.push_back(std::move(node));
            skipWs();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

private:
    bool parseObject(MessageTreeSnapshotNode& node) {
        if (!consume('{')) {
            return false;
        }

        bool hasTopic = false;
        bool hasValue = false;
        skipWs();
        if (consume('}')) {
            return false;
        }

        while (true) {
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            if (!consume(':')) {
                return false;
            }

            if (key == "topic") {
                if (!parseString(node.topic)) {
                    return false;
                }
                hasTopic = true;
            } else if (key == "value") {
                Value parsed{};
                if (!parseValue(parsed)) {
                    return false;
                }
                node.value = std::move(parsed);
                hasValue = true;
            } else {
                if (!skipValue()) {
                    return false;
                }
            }

            skipWs();
            if (consume('}')) {
                return hasTopic && hasValue;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    bool parseValue(Value& output) {
        skipWs();
        std::string text{};
        if (peek() == '"') {
            if (!parseString(text)) {
                return false;
            }
            output = std::move(text);
            return true;
        }

        double number = 0.0;
        if (!parseNumber(number)) {
            return false;
        }
        output = number;
        return true;
    }

    bool skipValue() {
        skipWs();
        const char current = peek();
        if (current == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (current == '{') {
            int depth = 0;
            do {
                const char c = peek();
                if (c == '\0') {
                    return false;
                }
                if (c == '{') {
                    depth += 1;
                }
                if (c == '}') {
                    depth -= 1;
                }
                pos_ += 1U;
            } while (depth > 0);
            return true;
        }
        if (current == '[') {
            int depth = 0;
            do {
                const char c = peek();
                if (c == '\0') {
                    return false;
                }
                if (c == '[') {
                    depth += 1;
                }
                if (c == ']') {
                    depth -= 1;
                }
                pos_ += 1U;
            } while (depth > 0);
            return true;
        }

        double ignored = 0.0;
        if (parseNumber(ignored)) {
            return true;
        }

        return parseLiteral("true") || parseLiteral("false") || parseLiteral("null");
    }

    bool parseNumber(double& out) {
        skipWs();
        std::size_t consumed = 0U;
        try {
            out = std::stod(json_.substr(pos_), &consumed);
        } catch (...) {
            return false;
        }
        pos_ += consumed;
        return consumed > 0U;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (!consume('"')) {
            return false;
        }

        std::string result;
        while (pos_ < json_.size()) {
            const char c = json_[pos_++];
            if (c == '"') {
                out = std::move(result);
                return true;
            }
            if (c == '\\') {
                if (pos_ >= json_.size()) {
                    return false;
                }
                const char escaped = json_[pos_++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        result.push_back(escaped);
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    default:
                        return false;
                }
                continue;
            }
            result.push_back(c);
        }

        return false;
    }

    bool parseLiteral(const std::string& literal) {
        skipWs();
        if (json_.substr(pos_, literal.size()) != literal) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool consume(char expected) {
        skipWs();
        if (peek() != expected) {
            return false;
        }
        pos_ += 1U;
        return true;
    }

    char peek() const {
        if (pos_ >= json_.size()) {
            return '\0';
        }
        return json_[pos_];
    }

    void skipWs() {
        while (pos_ < json_.size() && std::isspace(static_cast<unsigned char>(json_[pos_])) != 0) {
            pos_ += 1U;
        }
    }

    std::string json_;
    std::size_t pos_{0U};
};

bool parseSnapshotBody(const std::string& body, std::vector<MessageTreeSnapshotNode>& out) {
    SnapshotJsonParser parser{body};
    return parser.parse(out);
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
        std::uint32_t days = 0U;
        if (tryParseCleanupDays(message.value(), days)) {
            std::lock_guard<std::mutex> lock{treeStateMutex_};
            (void)tree_.cleanup(days);
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
MessageStore::queryNodes(const std::vector<MessageTreeSnapshotNode>& snapshot) const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.getNodes(snapshot);
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
    std::string topicPrefixEncoded;
    if (request.path == basePath) {
        topicPrefixEncoded.clear();
    } else if (startsWith(request.path, basePath + "/")) {
        topicPrefixEncoded = request.path.substr(basePath.size() + 1U);
    } else {
        response.status = 404;
        response.set_content("{\"error\":\"not_found\"}", "application/json");
        return;
    }

    std::string topicPrefix;
    if (!tryDecodePercentEncoding(topicPrefixEncoded, topicPrefix)) {
        response.status = 400;
        response.set_content("{\"error\":\"bad_request\",\"message\":\"invalid_percent_encoding\"}",
                             "application/json");
        return;
    }

    std::uint32_t levelAmount = 1U;
    if (request.has_header("levelamount")) {
        (void)tryParseUnsignedHeader(request.get_header_value("levelamount"), 1U, levelAmount);
    }

    bool includeHistory = false;
    if (request.has_header("history")) {
        includeHistory = parseBoolHeaderValue(request.get_header_value("history"), false);
    }

    bool includeReason = true;
    if (request.has_header("reason")) {
        includeReason = parseBoolHeaderValue(request.get_header_value("reason"), true);
    }

    std::vector<MessageTreeNode> nodes{};
    if (request.body.empty()) {
        nodes = store.querySection(topicPrefix, levelAmount, includeHistory, includeReason);
    } else {
        std::vector<MessageTreeSnapshotNode> snapshot{};
        if (parseSnapshotBody(request.body, snapshot)) {
            nodes = store.queryNodes(snapshot);
        }
    }

    response.status = 200;
    response.set_content(nodesToJson(nodes), "application/json");
}

bool MessageStore::tryParseCleanupDays(const Value& value, std::uint32_t& days) {
    if (std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (!std::isfinite(number) || number < 0.0) {
            return false;
        }
        days = static_cast<std::uint32_t>(number);
        return true;
    }

    const std::string& text = std::get<std::string>(value);
    if (text.empty()) {
        return false;
    }

    char* endPtr = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &endPtr, 10);
    if (endPtr == nullptr || *endPtr != '\0') {
        return false;
    }

    days = static_cast<std::uint32_t>(parsed);
    return true;
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
