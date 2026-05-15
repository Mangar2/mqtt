#pragma once

/**
 * @file automation_publish_failure_text.h
 * @brief String mapping for publish failure categories.
 */

#include <string>

#include "yaha/mqtt_component/mqtt_component.h"

namespace yaha::automation_publish_failure_text {

[[nodiscard]] std::string toText(PublishFailureCategory categoryValue);

} // namespace yaha::automation_publish_failure_text
