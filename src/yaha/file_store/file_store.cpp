#include "yaha/file_store/file_store.h"

#include "httplib.h"
#include "yaha/error_handling/yaha_error.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr int k_http_status_ok{200};
constexpr int k_http_status_bad_request{400};
constexpr int k_http_status_not_found{404};
constexpr int k_http_status_internal_server_error{500};

std::int64_t nowMilliseconds() {
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0U, prefix.size(), prefix) == 0;
}

std::string joinTopic(const std::string& prefix, const std::string& suffix) {
    if (prefix.empty()) {
        return suffix;
    }
    if (prefix.back() == '/') {
        return prefix + suffix;
    }
    return std::format("{}/{}", prefix, suffix);
}

[[nodiscard]] std::string valueToLogText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return textStream.str();
}

[[nodiscard]] std::string qosToLogText(const Qos qosValue) {
    switch (qosValue) {
        case Qos::AtMostOnce:
            return "0";
        case Qos::AtLeastOnce:
            return "1";
        case Qos::ExactlyOnce:
            return "2";
    }

    return "unknown";
}

void logMessage(const char* directionText, const Message& message) {
    std::cout << "file_store[" << directionText << "] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value());

    if (!message.reason().empty()) {
        std::cout << " reason=\"" << message.reason().front().message << '"';
    }

    std::cout << '\n' << std::flush;
}

void logFileIo(const std::string& operationText,
               const std::string& keyPath,
               const std::string& filename,
               const std::string& statusText,
               const std::string& detailText = "") {
    std::cout << "file_store[file-io] op=" << operationText
              << " keyPath=" << keyPath
              << " file=" << filename
              << " status=" << statusText;
    if (!detailText.empty()) {
        std::cout << " detail=\"" << detailText << '"';
    }
    std::cout << '\n' << std::flush;
}

void applyCorsHeaders(httplib::Response& response,
                      const std::string& allowHeaders = "content-type") {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", allowHeaders);
}

void setHttpErrorResponse(httplib::Response& response, int status, const YahaError& error) {
    response.status = status;
    applyCorsHeaders(response);
    response.set_content(error.buildMessage(), "text/plain");
}

} // namespace

FileStore::FileStore(FileStoreConfig config)
    : config_(std::move(config)) {}

FileStore::~FileStore() {
    close();
}

std::string FileStore::encodeKeyPathToFilename(const std::string& keyPath) {
    std::string result{};
    result.reserve(keyPath.size() * 3U);
    for (const char chr : keyPath) {
        result += std::to_string(static_cast<unsigned char>(chr));
    }
    return result;
}

SubscriptionMap FileStore::getSubscriptions() const {
    return {};
}

void FileStore::handleMessage(const Message& message) {
    logMessage("in", message);
}

void FileStore::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{publishMutex_};
    publishCallback_ = std::move(callback);
}

void FileStore::run() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
        stopRequested_ = false;
    }

    std::error_code error{};
    std::filesystem::create_directories(config_.directory, error);

    startHttpServer();
    watcherThread_ = std::thread([this]() {
        watcherLoop();
    });
}

void FileStore::close() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (!running_) {
            return;
        }
        stopRequested_ = true;
    }
    stopCondition_.notify_all();

    if (watcherThread_.joinable()) {
        watcherThread_.join();
    }

    stopHttpServer();

    std::lock_guard<std::mutex> lock{stateMutex_};
    running_ = false;
}

bool FileStore::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

void FileStore::startHttpServer() {
    if (config_.serverPort == 0U) {
        return;
    }

    httpServer_ = std::make_unique<httplib::Server>();
    httpServer_->Post(R"(.*)", [this](const httplib::Request& request, httplib::Response& response) {
        handleHttpPost(*this, request, response);
    });
    httpServer_->Get(R"(.*)", [this](const httplib::Request& request, httplib::Response& response) {
        handleHttpGet(*this, request, response);
    });
    httpServer_->Options(R"(.*)", [](const httplib::Request& request, httplib::Response& response) {
        handleHttpOptions(request, response);
    });

    const std::string host = config_.serverHost.empty() ? "127.0.0.1" : config_.serverHost;
    const int port = static_cast<int>(config_.serverPort);
    httpThread_ = std::thread([this, host, port]() {
        if (config_.httpStartCallback) {
            config_.httpStartCallback();
        }
        httpServer_->listen(host, port);
        if (config_.httpStopCallback) {
            config_.httpStopCallback();
        }
    });
}

void FileStore::stopHttpServer() {
    if (httpServer_) {
        httpServer_->stop();
    }
    if (httpThread_.joinable()) {
        httpThread_.join();
    }
    httpServer_.reset();
}

void FileStore::watcherLoop() {
    auto previousResult = buildSnapshot();
    if (!previousResult.success) {
        publishMonitoring("error", nullptr, "", "filesystem-watch", &previousResult.errorText);
    }
    auto previous = std::move(previousResult.snapshot);

    while (true) {
        std::unique_lock<std::mutex> stateLock{stateMutex_};
        const auto waitTime = std::chrono::milliseconds{config_.monitoring.watchIntervalMs};
        stopCondition_.wait_for(stateLock, waitTime, [this]() {
            return stopRequested_;
        });
        if (stopRequested_) {
            break;
        }
        stateLock.unlock();

        auto currentResult = buildSnapshot();
        if (!currentResult.success) {
            publishMonitoring("error", nullptr, "", "filesystem-watch", &currentResult.errorText);
            continue;
        }

        auto current = std::move(currentResult.snapshot);

        for (const auto& currentEntry : current) {
            const auto previousIt = previous.find(currentEntry.first);
            if (previousIt == previous.end()) {
                publishWatcherMonitoring("created", currentEntry.first);
                continue;
            }
            if (previousIt->second != currentEntry.second) {
                publishWatcherMonitoring("changed", currentEntry.first);
            }
        }

        for (const auto& previousEntry : previous) {
            if (current.find(previousEntry.first) == current.end()) {
                publishWatcherMonitoring("deleted", previousEntry.first);
                forgetKnownKeyPath(previousEntry.first);
            }
        }

        previous = std::move(current);
    }
}

FileStore::SnapshotBuildResult FileStore::buildSnapshot() const {
    SnapshotBuildResult result{};

    std::error_code directoryError{};
    std::filesystem::create_directories(config_.directory, directoryError);
    if (directoryError) {
        result.errorText = "create_directories failed";
        return result;
    }

    std::error_code iterateError{};
    for (const auto& directoryEntry : std::filesystem::directory_iterator{config_.directory, iterateError}) {
        if (iterateError) {
            result.errorText = "directory iteration failed";
            return result;
        }
        if (!directoryEntry.is_regular_file()) {
            continue;
        }

        const std::string filename = directoryEntry.path().filename().string();
        if (filename.find(".bak.") != std::string::npos || startsWith(filename, ".")) {
            continue;
        }

        const auto writeTime = std::filesystem::last_write_time(directoryEntry.path(), iterateError);
        if (iterateError) {
            result.errorText = "last_write_time failed";
            return result;
        }
        result.snapshot[filename] = writeTime;
    }

    result.success = true;
    return result;
}

FileStore::WritePayloadResult FileStore::writeKeyPayload(
    const std::string& keyPath,
    const StoredPayload& payload) const {
    WritePayloadResult result{};
    result.filename = encodeKeyPathToFilename(keyPath);
    logFileIo("write", keyPath, result.filename, "start");
    const std::filesystem::path basePath = config_.directory / result.filename;
    const std::filesystem::path tempPath = config_.directory / (result.filename + ".tmp");

    std::error_code pathError{};
    std::filesystem::create_directories(config_.directory, pathError);
    if (pathError) {
        result.errorText = "failed to create directory";
        logFileIo("write", keyPath, result.filename, "error", result.errorText);
        return result;
    }

    if (config_.keepFiles > 1U && std::filesystem::exists(basePath, pathError) && !pathError) {
        const std::filesystem::path backupPath = config_.directory /
            std::format("{}.bak.{}", result.filename, nowMilliseconds());
        std::filesystem::copy_file(
            basePath,
            backupPath,
            std::filesystem::copy_options::overwrite_existing,
            pathError);
    }

    {
        std::ofstream output{tempPath, std::ios::binary | std::ios::trunc};
        if (!output.is_open()) {
            result.errorText = "failed to open temp file";
            logFileIo("write", keyPath, result.filename, "error", result.errorText);
            return result;
        }
        output << (payload.isJson ? 'J' : 'T') << '\n';
        output << payload.body;
        output.flush();
        if (!output.good()) {
            result.errorText = "failed to write payload";
            logFileIo("write", keyPath, result.filename, "error", result.errorText);
            return result;
        }
    }

    std::filesystem::rename(tempPath, basePath, pathError);
    if (pathError) {
        std::filesystem::remove(basePath, pathError);
        pathError.clear();
        std::filesystem::rename(tempPath, basePath, pathError);
        if (pathError) {
            result.errorText = "failed to replace file";
            logFileIo("write", keyPath, result.filename, "error", result.errorText);
            return result;
        }
    }

    if (config_.keepFiles > 1U) {
        std::vector<std::filesystem::path> backups{};
        std::error_code iterateError{};
        for (const auto& directoryEntry : std::filesystem::directory_iterator{config_.directory, iterateError}) {
            if (iterateError) {
                break;
            }
            if (!directoryEntry.is_regular_file()) {
                continue;
            }
            const std::string backupName = directoryEntry.path().filename().string();
            const std::string prefix = result.filename + ".bak.";
            if (startsWith(backupName, prefix)) {
                backups.push_back(directoryEntry.path());
            }
        }

        std::sort(backups.begin(), backups.end());
        const std::size_t maxBackups = static_cast<std::size_t>(config_.keepFiles - 1U);
        while (backups.size() > maxBackups) {
            std::error_code removeError{};
            std::filesystem::remove(backups.front(), removeError);
            backups.erase(backups.begin());
        }
    }

    result.success = true;
    rememberKnownKeyPath(result.filename, keyPath);
    logFileIo("write", keyPath, result.filename, "ok");
    return result;
}

void FileStore::rememberKnownKeyPath(const std::string& filename,
                                     const std::string& keyPath) const {
    std::lock_guard<std::mutex> lock{knownFilesMutex_};
    knownFilenameToKeyPath_[filename] = keyPath;
}

std::optional<std::string> FileStore::lookupKnownKeyPath(const std::string& filename) const {
    std::lock_guard<std::mutex> lock{knownFilesMutex_};
    const auto found = knownFilenameToKeyPath_.find(filename);
    if (found == knownFilenameToKeyPath_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void FileStore::forgetKnownKeyPath(const std::string& filename) const {
    std::lock_guard<std::mutex> lock{knownFilesMutex_};
    knownFilenameToKeyPath_.erase(filename);
}

void FileStore::publishWatcherMonitoring(const std::string& eventType,
                                         const std::string& filename) const {
    const std::optional<std::string> knownKeyPath = lookupKnownKeyPath(filename);
    const std::string* keyPathPointer = knownKeyPath ? &*knownKeyPath : nullptr;
    publishMonitoring(eventType, keyPathPointer, filename, "filesystem-watch", nullptr);
}

FileStore::ReadPayloadResult FileStore::readKeyPayload(const std::string& keyPath) const {
    ReadPayloadResult result{};
    const std::string filename = encodeKeyPathToFilename(keyPath);
    logFileIo("read", keyPath, filename, "start");
    const std::filesystem::path path = config_.directory / filename;
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        result.errorText = "file not found";
        logFileIo("read", keyPath, filename, "error", result.errorText);
        return result;
    }

    std::ostringstream stream{};
    stream << input.rdbuf();
    const std::string stored = stream.str();
    if (stored.size() >= 2U && (stored[0] == 'J' || stored[0] == 'T') && stored[1] == '\n') {
        const std::string body = stored.substr(2U);
        if (stored[0] == 'J') {
            result.responseJson = body;
        } else {
            result.responseJson = std::format("\"{}\"", jsonEscape(body));
        }
        result.success = true;
        logFileIo("read", keyPath, filename, "ok");
        return result;
    }

    result.responseJson = std::format("\"{}\"", jsonEscape(stored));
    result.success = true;
    logFileIo("read", keyPath, filename, "ok");
    return result;
}

bool FileStore::validateJsonPayload(const std::string& jsonText) {
    std::size_t index = 0U;
    while (index < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[index])) != 0) {
        index += 1U;
    }

    if (index >= jsonText.size()) {
        return false;
    }

    const char first = jsonText[index];
    const bool startsLikeJson = first == '{' || first == '[' || first == '"' || first == '-'
        || (first >= '0' && first <= '9') || first == 't' || first == 'f' || first == 'n';

    return startsLikeJson;
}

void FileStore::publishMonitoring(const std::string& eventType,
                                  const std::string* keyPath,
                                  const std::string& filename,
                                  const std::string& source,
                                  const std::string* details) const {
    if (!config_.monitoring.enabled) {
        return;
    }

    PublishCallback callback{};
    {
        std::lock_guard<std::mutex> lock{publishMutex_};
        callback = publishCallback_;
    }
    if (!callback) {
        return;
    }

    const std::string prefix = trimTopicPrefix(config_.monitoring.topicPrefix);
    const std::string topic = joinTopic(prefix, eventType);

    std::string payload{"{"};
    payload += "\"keyPath\":";
    if (keyPath == nullptr) {
        payload += "null";
    } else {
        payload += std::format("\"{}\"", jsonEscape(*keyPath));
    }
    payload += std::format(",\"filename\":\"{}\"", jsonEscape(filename));
    payload += std::format(",\"directory\":\"{}\"", jsonEscape(config_.directory.string()));
    payload += std::format(",\"changeType\":\"{}\"", jsonEscape(eventType));
    payload += ",\"timestamp\":" + std::to_string(nowMilliseconds());
    payload += std::format(",\"source\":\"{}\"", jsonEscape(source));
    if (details != nullptr) {
        payload += std::format(",\"details\":\"{}\"", jsonEscape(*details));
    }
    payload += "}";

    Message monitoringMessage{topic, payload, config_.monitoring.qos, config_.monitoring.retain};
    logMessage("out", monitoringMessage);

    try {
        callback(monitoringMessage);
    } catch (...) {
    }
}

std::string FileStore::jsonEscape(const std::string& text) {
    std::string escaped{};
    escaped.reserve(text.size());
    for (const char chr : text) {
        switch (chr) {
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
                escaped.push_back(chr);
                break;
        }
    }
    return escaped;
}

std::string FileStore::toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char chr) {
        return static_cast<char>(std::tolower(chr));
    });
    return text;
}

std::string FileStore::trimTopicPrefix(std::string prefix) {
    if (prefix.empty()) {
        prefix = "$MONITOR/FileStore";
    }

    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }

    return prefix;
}

void FileStore::handleHttpOptions(const httplib::Request& request,
                                  httplib::Response& response) {
    std::string allowHeaders{"content-type"};
    std::string requestedHeaders = request.get_header_value("Access-Control-Request-Headers");
    if (requestedHeaders.empty()) {
        requestedHeaders = request.get_header_value("access-control-request-headers");
    }
    if (!requestedHeaders.empty()) {
        allowHeaders = requestedHeaders;
    }

    response.status = k_http_status_ok;
    applyCorsHeaders(response, allowHeaders);
    response.set_content("", "text/plain");
}

void FileStore::handleHttpPost(FileStore& store,
                               const httplib::Request& request,
                               httplib::Response& response) {
    const std::string keyPath = request.path;
    if (keyPath.size() > store.config_.maxKeyLength) {
        setHttpErrorResponse(response,
                             k_http_status_bad_request,
                             YahaError{"YAHA_FILE_STORE_KEY_TOO_LONG",
                                       "key_too_long",
                                       "The key is too long for this FileStore instance.",
                                       std::format("key_length={}, max_length={}",
                                                   keyPath.size(),
                                                   store.config_.maxKeyLength)});
        return;
    }

    const std::string contentType = toLower(request.get_header_value("Content-Type"));
    const bool isJson = contentType == "application/json";

    StoredPayload payload{};
    payload.isJson = isJson;
    payload.body = request.body;

    if (isJson && !FileStore::validateJsonPayload(payload.body)) {
        setHttpErrorResponse(response,
                             k_http_status_bad_request,
                             YahaError{"YAHA_FILE_STORE_INVALID_JSON_PAYLOAD",
                                       "invalid_json_payload",
                                       "The provided JSON payload is invalid."});
        return;
    }

    const auto writeResult = store.writeKeyPayload(keyPath, payload);
    if (!writeResult.success) {
        setHttpErrorResponse(response,
                             k_http_status_internal_server_error,
                             YahaError{"YAHA_FILE_STORE_PERSIST_FAILED",
                                       "failed_to_persist_key",
                                       "The key could not be persisted.",
                                       writeResult.errorText});
        return;
    }

    store.publishMonitoring("changed", &keyPath, writeResult.filename, "http-post", nullptr);

    response.status = k_http_status_ok;
    applyCorsHeaders(response);
    response.set_content("", "text/plain");
}

void FileStore::handleHttpGet(FileStore& store,
                              const httplib::Request& request,
                              httplib::Response& response) {
    const std::string keyPath = request.path;
    if (keyPath.size() > store.config_.maxKeyLength) {
        setHttpErrorResponse(response,
                             k_http_status_bad_request,
                             YahaError{"YAHA_FILE_STORE_KEY_TOO_LONG",
                                       "key_too_long",
                                       "The key is too long for this FileStore instance.",
                                       std::format("key_length={}, max_length={}",
                                                   keyPath.size(),
                                                   store.config_.maxKeyLength)});
        return;
    }

    const auto readResult = store.readKeyPayload(keyPath);
    if (!readResult.success) {
        if (readResult.errorText == "file not found") {
            setHttpErrorResponse(response,
                                 k_http_status_not_found,
                                 YahaError{"YAHA_FILE_STORE_KEY_NOT_FOUND",
                                           "key_not_found",
                                           "The requested key does not exist."});
        } else {
            setHttpErrorResponse(response,
                                 k_http_status_internal_server_error,
                                 YahaError{"YAHA_FILE_STORE_READ_FAILED",
                                           "failed_to_read_key",
                                           "The key could not be read.",
                                           readResult.errorText});
        }
        return;
    }

    response.status = k_http_status_ok;
    applyCorsHeaders(response);
    response.set_content(readResult.responseJson, "Application/Json");
}

} // namespace yaha
