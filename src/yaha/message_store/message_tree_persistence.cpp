#include "yaha/message_store/message_tree_persistence.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <utility>

namespace yaha {

namespace {

std::int64_t wallClockMilliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool writeValue(std::ofstream& stream, const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        stream << "S " << std::quoted(std::get<std::string>(value)) << '\n';
        return static_cast<bool>(stream);
    }

    stream << "N " << std::setprecision(17) << std::get<double>(value) << '\n';
    return static_cast<bool>(stream);
}

bool readValue(std::ifstream& stream, Value& value) {
    std::string kind{};
    if (!(stream >> kind)) {
        return false;
    }

    if (kind == "S") {
        std::string text{};
        if (!(stream >> std::quoted(text))) {
            return false;
        }
        value = text;
        return true;
    }

    if (kind == "N") {
        double number = 0.0;
        if (!(stream >> number)) {
            return false;
        }
        value = number;
        return true;
    }

    return false;
}

bool writeReasonList(std::ofstream& stream, const std::vector<ReasonEntry>& reasonList) {
    stream << reasonList.size() << '\n';
    for (const auto& reason : reasonList) {
        stream << std::quoted(reason.message) << ' '
               << std::quoted(reason.timestamp) << '\n';
    }
    return static_cast<bool>(stream);
}

bool readReasonList(std::ifstream& stream, std::vector<ReasonEntry>& reasonList) {
    std::size_t count = 0U;
    if (!(stream >> count)) {
        return false;
    }

    reasonList.clear();
    reasonList.reserve(count);
    for (std::size_t idx = 0U; idx < count; ++idx) {
        ReasonEntry reason{};
        if (!(stream >> std::quoted(reason.message) >> std::quoted(reason.timestamp))) {
            return false;
        }
        reasonList.push_back(std::move(reason));
    }

    return true;
}

bool writeNode(std::ofstream& stream, const MessageTreeNode& node) {
    stream << std::quoted(node.topic) << '\n';
    stream << node.timeMs << '\n';

    if (!writeValue(stream, node.value)) {
        return false;
    }

    if (!writeReasonList(stream, node.reason)) {
        return false;
    }

    stream << node.history.size() << '\n';
    for (const auto& historyEntry : node.history) {
        stream << historyEntry.timeMs << '\n';
        if (!writeValue(stream, historyEntry.value)) {
            return false;
        }
        if (!writeReasonList(stream, historyEntry.reason)) {
            return false;
        }
    }

    return static_cast<bool>(stream);
}

bool readNode(std::ifstream& stream, MessageTreeNode& node) {
    if (!(stream >> std::quoted(node.topic))) {
        return false;
    }

    if (!(stream >> node.timeMs)) {
        return false;
    }

    if (!readValue(stream, node.value)) {
        return false;
    }

    if (!readReasonList(stream, node.reason)) {
        return false;
    }

    std::size_t historyCount = 0U;
    if (!(stream >> historyCount)) {
        return false;
    }

    node.history.clear();
    node.history.reserve(historyCount);
    for (std::size_t idx = 0U; idx < historyCount; ++idx) {
        MessageTreeHistoryEntry entry{};
        if (!(stream >> entry.timeMs)) {
            return false;
        }
        if (!readValue(stream, entry.value)) {
            return false;
        }
        if (!readReasonList(stream, entry.reason)) {
            return false;
        }
        node.history.push_back(std::move(entry));
    }

    return true;
}

} // namespace

MessageTreePersistence::MessageTreePersistence()
    : MessageTreePersistence{Config{}} {}

MessageTreePersistence::MessageTreePersistence(Config config)
    : config_(std::move(config)) {}

MessageTreePersistence::~MessageTreePersistence() {
    stopPeriodic();
}

bool MessageTreePersistence::persistNow(const MessageTree& tree) {
    std::error_code err{};
    std::filesystem::create_directories(config_.directory, err);
    if (err) {
        return false;
    }

    const std::vector<MessageTreeNode> nodes =
        tree.getSection("", std::numeric_limits<std::uint32_t>::max(), true, true);

    const std::filesystem::path path = makeSnapshotPath(wallClockMilliseconds());
    std::ofstream stream{path, std::ios::out | std::ios::trunc};
    if (!stream.is_open()) {
        return false;
    }

    stream << "MTREE1\n";
    stream << nodes.size() << '\n';
    for (const auto& node : nodes) {
        if (!writeNode(stream, node)) {
            return false;
        }
    }

    if (!stream.good()) {
        return false;
    }

    enforceRetention();
    return true;
}

bool MessageTreePersistence::restoreLatest(MessageTree& tree) {
    const auto files = listSnapshotFilesNewestFirst();
    for (const auto& path : files) {
        std::ifstream stream{path};
        if (!stream.is_open()) {
            continue;
        }

        std::string magic{};
        if (!(stream >> magic) || magic != "MTREE1") {
            continue;
        }

        std::size_t nodeCount = 0U;
        if (!(stream >> nodeCount)) {
            continue;
        }

        std::vector<MessageTreeNode> nodes{};
        nodes.reserve(nodeCount);

        bool parseOk = true;
        for (std::size_t idx = 0U; idx < nodeCount; ++idx) {
            MessageTreeNode node{};
            if (!readNode(stream, node)) {
                parseOk = false;
                break;
            }
            nodes.push_back(std::move(node));
        }

        if (!parseOk) {
            continue;
        }

        tree.replaceAllNodes(nodes);
        return true;
    }

    return false;
}

void MessageTreePersistence::startPeriodic(const MessageTree& tree) {
    if (config_.intervalMs == 0U || periodicRunning_.load()) {
        return;
    }

    periodicTree_ = &tree;
    periodicRunning_.store(true);
    periodicThread_ = std::thread{&MessageTreePersistence::periodicLoop, this};
}

void MessageTreePersistence::stopPeriodic() {
    periodicRunning_.store(false);
    if (periodicThread_.joinable()) {
        periodicThread_.join();
    }
}

std::filesystem::path MessageTreePersistence::makeSnapshotPath(std::int64_t timestampMs) const {
    std::ostringstream name{};
    name << config_.filename << '_' << timestampMs << ".mtree";
    return config_.directory / name.str();
}

std::vector<std::filesystem::path> MessageTreePersistence::listSnapshotFilesNewestFirst() const {
    std::vector<std::pair<std::int64_t, std::filesystem::path>> indexed{};
    const std::string prefix = config_.filename + "_";

    std::error_code err{};
    if (!std::filesystem::exists(config_.directory, err) || err) {
        return {};
    }

    for (const auto& entry : std::filesystem::directory_iterator{config_.directory}) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string fileName = entry.path().filename().string();
        if (fileName.rfind(prefix, 0U) != 0U) {
            continue;
        }
        if (entry.path().extension().string() != ".mtree") {
            continue;
        }

        const std::string number = fileName.substr(prefix.size(),
            fileName.size() - prefix.size() - std::string{".mtree"}.size());

        try {
            const std::int64_t stamp = std::stoll(number);
            indexed.push_back({stamp, entry.path()});
        } catch (...) {
            continue;
        }
    }

    std::sort(indexed.begin(), indexed.end(),
              [](const auto& left, const auto& right) {
        return left.first > right.first;
    });

    std::vector<std::filesystem::path> paths{};
    paths.reserve(indexed.size());
    for (const auto& item : indexed) {
        paths.push_back(item.second);
    }
    return paths;
}

void MessageTreePersistence::enforceRetention() {
    if (config_.keepFiles == 0U) {
        return;
    }

    const auto files = listSnapshotFilesNewestFirst();
    for (std::size_t idx = config_.keepFiles; idx < files.size(); ++idx) {
        std::error_code err{};
        std::filesystem::remove(files[idx], err);
    }
}

void MessageTreePersistence::periodicLoop() {
    const auto interval = std::chrono::milliseconds{config_.intervalMs};
    while (periodicRunning_.load()) {
        std::this_thread::sleep_for(interval);
        if (!periodicRunning_.load()) {
            break;
        }
        if (periodicTree_ != nullptr) {
            (void)persistNow(*periodicTree_);
        }
    }
}

} // namespace yaha
