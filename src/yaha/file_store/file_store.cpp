#include "yaha/file_store/file_store.h"

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace yaha {

namespace {

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
    (void)message;
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
    httpServer_->Options(R"(.*)", [](const httplib::Request&, httplib::Response& response) {
        handleHttpOptions(response);
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
                publishMonitoring("created", nullptr, currentEntry.first, "filesystem-watch", nullptr);
                continue;
            }
            if (previousIt->second != currentEntry.second) {
                publishMonitoring("changed", nullptr, currentEntry.first, "filesystem-watch", nullptr);
            }
        }

        for (const auto& previousEntry : previous) {
            if (current.find(previousEntry.first) == current.end()) {
                publishMonitoring("deleted", nullptr, previousEntry.first, "filesystem-watch", nullptr);
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
    const std::filesystem::path basePath = config_.directory / result.filename;
    const std::filesystem::path tempPath = config_.directory / (result.filename + ".tmp");

    std::error_code pathError{};
    std::filesystem::create_directories(config_.directory, pathError);
    if (pathError) {
        result.errorText = "failed to create directory";
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
            return result;
        }
        output << (payload.isJson ? 'J' : 'T') << '\n';
        output << payload.body;
        output.flush();
        if (!output.good()) {
            result.errorText = "failed to write payload";
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
    return result;
}

FileStore::ReadPayloadResult FileStore::readKeyPayload(const std::string& keyPath) const {
    ReadPayloadResult result{};
    const std::filesystem::path path = config_.directory / encodeKeyPathToFilename(keyPath);
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        result.errorText = "file not found";
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
        return result;
    }

    result.responseJson = std::format("\"{}\"", jsonEscape(stored));
    result.success = true;
    return result;
}

bool FileStore::validateJsonPayload(const std::string& jsonText) const {
    std::size_t index = 0U;
    while (index < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[index])) != 0) {
        index += 1U;
    }

    if (index >= jsonText.size()) {
        return false;
    }

    const char first = jsonText[index];
    if (first != '{' && first != '[' && first != '"' && first != '-' && (first < '0' || first > '9')
        && first != 't' && first != 'f' && first != 'n') {
        return false;
    }

    return true;
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

    try {
        callback(Message{topic, payload, config_.monitoring.qos, config_.monitoring.retain});
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
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
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

void FileStore::handleHttpOptions(httplib::Response& response) {
    response.status = 200;
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", "content-type");
    response.set_content("", "text/plain");
}

void FileStore::handleHttpPost(FileStore& store,
                               const httplib::Request& request,
                               httplib::Response& response) {
    const std::string keyPath = request.path;
    if (keyPath.size() > store.config_.maxKeyLength) {
        response.status = 400;
        response.set_content("Error: Key too long, a maximum of 100 characters are supported", "text/plain");
        return;
    }

    const std::string contentType = toLower(request.get_header_value("Content-Type"));
    const bool isJson = contentType == "application/json";

    StoredPayload payload{};
    payload.isJson = isJson;
    payload.body = request.body;

    if (isJson && !store.validateJsonPayload(payload.body)) {
        response.status = 400;
        response.set_content("Error in request", "text/plain");
        return;
    }

    const auto writeResult = store.writeKeyPayload(keyPath, payload);
    if (!writeResult.success) {
        response.status = 400;
        response.set_content("Error in request", "text/plain");
        return;
    }

    store.publishMonitoring("changed", &keyPath, writeResult.filename, "http-post", nullptr);

    response.status = 200;
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_content("", "text/plain");
}

void FileStore::handleHttpGet(FileStore& store,
                              const httplib::Request& request,
                              httplib::Response& response) {
    const std::string keyPath = request.path;
    if (keyPath.size() > store.config_.maxKeyLength) {
        response.status = 400;
        response.set_content("Error: Key too long, a maximum of 100 characters are suported", "text/plain");
        return;
    }

    const auto readResult = store.readKeyPayload(keyPath);
    if (!readResult.success) {
        response.status = 400;
        response.set_content("Error in request", "text/plain");
        return;
    }

    response.status = 200;
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_content(readResult.responseJson, "Application/Json");
}

} // namespace yaha
