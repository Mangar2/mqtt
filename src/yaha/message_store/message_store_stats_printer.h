#pragma once

#include "yaha/message_store/message_tree.h"

#include <string_view>

namespace yaha {

void printMessageStoreCompressionStatsLine(const MessageTree::CompressionStats& compressionStats,
                                           std::string_view phaseText);

} // namespace yaha
