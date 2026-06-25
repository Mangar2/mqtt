#include "yaha/message_store/message_tree_persistence.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <ios>
#include <sstream>
#include <utility>

namespace yaha {

namespace {

constexpr std::string_view k_snapshot_magic_v2{"MTREE2"};
constexpr std::string_view k_snapshot_magic_v3{"MTREE3"};

std::int64_t wallClockMilliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] bool isSupportedSnapshotMagic(const std::string& magicText) {
    return magicText == k_snapshot_magic_v2 || magicText == k_snapshot_magic_v3;
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
    return persistNowWithPath(tree).has_value();
}

std::optional<std::filesystem::path>
MessageTreePersistence::persistNowWithPath(const MessageTree& tree) {
    std::error_code err{};
    std::filesystem::create_directories(config_.directory, err);
    if (err) {
        return std::nullopt;
    }

    std::filesystem::path path = makeSnapshotPath(wallClockMilliseconds());
    std::ofstream stream{path, std::ios::out | std::ios::trunc};
    if (!stream.is_open()) {
        return std::nullopt;
    }

    stream << k_snapshot_magic_v3 << '\n';
    if (!tree.writeCompressed(stream)) {
        return std::nullopt;
    }

    if (!stream.good()) {
        return std::nullopt;
    }

    enforceRetention();
    return std::move(path);
}

bool MessageTreePersistence::restoreLatest(MessageTree& tree) {
    const auto files = listSnapshotFilesNewestFirst();
    for (const auto& path : files) {
        std::ifstream stream{path};
        if (!stream.is_open()) {
            continue;
        }

        std::string magic{};
        if (!(stream >> magic)) {
            continue;
        }

        if (!isSupportedSnapshotMagic(magic)) {
            continue;
        }

        if (tree.readCompressed(stream)) {
            return true;
        }
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
        if (!fileName.starts_with(prefix)) {
            continue;
        }
        if (entry.path().extension().string() != ".mtree") {
            continue;
        }

        const std::string number = fileName.substr(prefix.size(),
            fileName.size() - prefix.size() - std::string{".mtree"}.size());

        try {
            const std::int64_t stamp = std::stoll(number);
            indexed.emplace_back(stamp, entry.path());
        } catch (...) {
            continue;
        }
    }

    std::ranges::sort(indexed,
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
