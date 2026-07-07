#include "yaha/message_store/message_store_stats_printer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

#if defined(__linux__) && defined(__GLIBC__)
#include <cmath>
#include <malloc.h>
#endif

namespace yaha {
namespace {

#if defined(__linux__) && defined(__GLIBC__)
constexpr double k_fragmentation_percentage_scale{10.0};
#endif

} // namespace

void printMessageStoreCompressionStatsLine(const MessageTree::CompressionStats& compressionStats,
                                           std::string_view phaseText) {
    struct CompressionStatsRow {
        std::string_view name;
        std::uint64_t value;
    };

    const std::array<CompressionStatsRow, 14U> rows{{
        {.name = "currentNodes", .value = compressionStats.currentNodeCount},
        {.name = "totalStoredMessages", .value = compressionStats.totalStoredMessageCount},
        {.name = "historyBuckets", .value = compressionStats.historyBucketCount},
        {.name = "reasonEntries.total", .value = compressionStats.totalReasonEntryCount},
        {.name = "directories.strings", .value = compressionStats.totalDirectoryStringCount},
        {.name = "buckets.single", .value = compressionStats.singleBucketCount},
        {.name = "buckets.timeValue", .value = compressionStats.timeValueBucketCount},
        {.name = "buckets.time", .value = compressionStats.timeBucketCount},
        {.name = "buckets.interval", .value = compressionStats.intervalBucketCount},
        {.name = "represented.single", .value = compressionStats.representedSingleCount},
        {.name = "represented.timeValue", .value = compressionStats.representedTimeValueCount},
        {.name = "represented.timeValue.string", .value = compressionStats.representedTimeValueStringCount},
        {.name = "represented.timeValue.double", .value = compressionStats.representedTimeValueDoubleCount},
        {.name = "represented.time", .value = compressionStats.representedTimeCount},
    }};

    const std::uint64_t representedIntervalCount = compressionStats.representedIntervalCount;
    std::size_t maxNameWidth = std::string_view{"represented.timeValue.string"}.size();
    std::size_t maxValueWidth = std::to_string(representedIntervalCount).size();
    for (const auto& row : rows) {
        maxNameWidth = std::max(maxNameWidth, row.name.size());
        maxValueWidth = std::max(maxValueWidth, std::to_string(row.value).size());
    }

    std::cout << "message_store[stats]"
              << " phase=" << phaseText
              << '\n';

    for (const auto& row : rows) {
        std::cout << "  "
                  << std::left << std::setw(static_cast<int>(maxNameWidth)) << row.name
                  << " : "
                  << std::right << std::setw(static_cast<int>(maxValueWidth)) << row.value
                  << '\n';
    }

    std::cout << "  "
              << std::left << std::setw(static_cast<int>(maxNameWidth)) << "represented.interval"
              << " : "
              << std::right << std::setw(static_cast<int>(maxValueWidth)) << representedIntervalCount
              << '\n';

    std::ostringstream ratioStream{};
    ratioStream << std::fixed << std::setprecision(3)
                << compressionStats.reasonEntriesPerDirectoryString;
    std::cout << "  "
              << std::left << std::setw(static_cast<int>(maxNameWidth)) << "ratio.reasonPerDirectoryString"
              << " : "
              << std::right << ratioStream.str()
              << '\n';

#if defined(__linux__) && defined(__GLIBC__)
    const struct mallinfo2 mallInfo = mallinfo2();
    const std::size_t arenaBytes = static_cast<std::size_t>(mallInfo.arena);
    const std::size_t usedBytes = static_cast<std::size_t>(mallInfo.uordblks);
    const std::size_t freeBytes = static_cast<std::size_t>(mallInfo.fordblks);
    const double fragmentationPercent =
        100.0 * static_cast<double>(freeBytes) / static_cast<double>(arenaBytes + 1U);

    const std::array<CompressionStatsRow, 4U> heapRows{{
        {.name = "heap.arenaKB", .value = arenaBytes / 1024U},
        {.name = "heap.inUseKB", .value = usedBytes / 1024U},
        {.name = "heap.freeKB", .value = freeBytes / 1024U},
        {.name = "heap.fragmentationPct_x10", .value = static_cast<std::uint64_t>(std::llround(fragmentationPercent * k_fragmentation_percentage_scale))},
    }};

    std::size_t maxHeapNameWidth = std::string_view{"heap.fragmentationPct_x10"}.size();
    std::size_t maxHeapValueWidth = std::string_view{"0.0"}.size();
    for (const auto& row : heapRows) {
        maxHeapNameWidth = std::max(maxHeapNameWidth, row.name.size());
        maxHeapValueWidth = std::max(maxHeapValueWidth, std::to_string(row.value).size());
    }

    for (const auto& row : heapRows) {
        if (row.name == "heap.fragmentationPct_x10") {
            std::ostringstream fragmentationStream{};
            fragmentationStream << std::fixed << std::setprecision(1)
                                << (static_cast<double>(row.value) / k_fragmentation_percentage_scale);
            std::cout << "  "
                      << std::left << std::setw(static_cast<int>(maxHeapNameWidth)) << "heap.fragmentationPct"
                      << " : "
                      << std::right << std::setw(static_cast<int>(maxHeapValueWidth)) << fragmentationStream.str()
                      << '\n';
            continue;
        }

        std::cout << "  "
                  << std::left << std::setw(static_cast<int>(maxHeapNameWidth)) << row.name
                  << " : "
                  << std::right << std::setw(static_cast<int>(maxHeapValueWidth)) << row.value
                  << '\n';
    }
#else
        std::cout << "  heap.stats.unavailable : platform_not_glibc_linux"
              << '\n';
#endif

    std::cout << std::flush;
}

} // namespace yaha
